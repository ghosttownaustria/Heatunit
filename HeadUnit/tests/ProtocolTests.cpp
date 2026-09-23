#include "androidauto/AndroidAutoSession.h"
#include "usb/ProjectionTransport.h"
#include <aasdk/Messenger/Cryptor.hpp>
#include <aasdk/Transport/SSLWrapper.hpp>
#include <aasdk/Transport/ITransport.hpp>
#include <openssl/err.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <utility>

namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
using Data = aasdk::common::Data;
Data ReadBio(BIO* bio) {
    Data data(BIO_ctrl_pending(bio));
    if (!data.empty()) Check(BIO_read(bio, data.data(), static_cast<int>(data.size())) == static_cast<int>(data.size()), "BIO read failed");
    return data;
}
void TestTls(int version) {
    // An ephemeral local peer verifies real TLS records; it is never used by the app.
    const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(EVP_RSA_gen(2048), EVP_PKEY_free);
    const std::unique_ptr<X509, decltype(&X509_free)> certificate(X509_new(), X509_free);
    Check(key && certificate, "TLS test allocation failed");
    X509_set_version(certificate.get(), 2);
    ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1);
    X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -60);
    X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 3600);
    X509_set_pubkey(certificate.get(), key.get());
    auto* subject = X509_get_subject_name(certificate.get());
    X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("Unit test peer"), -1, -1, 0);
    X509_set_issuer_name(certificate.get(), subject);
    Check(X509_sign(certificate.get(), key.get(), EVP_sha256()) > 0, "TLS test signing failed");
    const std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
    Check(context != nullptr, "TLS server context allocation failed");
    Check(SSL_CTX_set_min_proto_version(context.get(), version) == 1 && SSL_CTX_set_max_proto_version(context.get(), version) == 1, "TLS test version rejected");
    Check(SSL_CTX_use_certificate(context.get(), certificate.get()) == 1 && SSL_CTX_use_PrivateKey(context.get(), key.get()) == 1, "TLS test credentials rejected");
    const std::unique_ptr<SSL, decltype(&SSL_free)> server(SSL_new(context.get()), SSL_free);
    auto* input = BIO_new(BIO_s_mem());
    auto* output = BIO_new(BIO_s_mem());
    Check(server && input && output, "TLS peer BIO allocation failed");
    SSL_set_bio(server.get(), input, output);
    SSL_set_accept_state(server.get());
    aasdk::messenger::Cryptor client(std::make_shared<aasdk::transport::SSLWrapper>());
    struct Cleanup { aasdk::messenger::Cryptor& client; ~Cleanup() { client.deinit(); } } cleanup{client};
    client.init();
    bool isClientReady = false, isServerReady = false;
    for (int step = 0; step < 20 && !(isClientReady && isServerReady); ++step) {
        isClientReady = client.doHandshake();
        auto request = client.readHandshakeBuffer();
        if (!request.empty()) Check(BIO_write(input, request.data(), static_cast<int>(request.size())) == static_cast<int>(request.size()), "Client handshake forwarding failed");
        const auto result = SSL_do_handshake(server.get());
        const auto error = SSL_get_error(server.get(), result);
        Check(error == SSL_ERROR_NONE || error == SSL_ERROR_WANT_READ, "Server handshake failed");
        isServerReady = result == 1;
        auto response = ReadBio(output);
        if (!response.empty()) client.writeHandshakeBuffer(aasdk::common::DataConstBuffer(response));
    }
    Check(isClientReady && isServerReady, "TLS negotiation did not complete");
    for (const int size : {7, 4096, 32000}) {
        Data plaintext(size);
        for (int i = 0; i < size; ++i) plaintext[i] = static_cast<unsigned char>(i);
        Check(SSL_write(server.get(), plaintext.data(), size) == size, "TLS peer write failed");
        const auto encrypted = ReadBio(output);
        Data decoded;
        client.decrypt(decoded, aasdk::common::DataConstBuffer(encrypted), static_cast<int>(encrypted.size()));
        Check(decoded == plaintext, "TLS plaintext mismatch (cipher overhead/record split)");
        Data outgoing;
        client.encrypt(outgoing, aasdk::common::DataConstBuffer(plaintext));
        Check(BIO_write(input, outgoing.data(), static_cast<int>(outgoing.size())) == static_cast<int>(outgoing.size()), "Encrypted response forwarding failed");
        Data received(size);
        int offset = 0;
        while (offset < size) {
            const auto count = SSL_read(server.get(), received.data() + offset, size - offset);
            Check(count > 0, "Peer could not decrypt headunit response");
            offset += count;
        }
        Check(received == plaintext, "Headunit encrypted response mismatch");
    }
}
class StoppedTransport final : public aasdk::transport::ITransport {
public:
    void receive(std::size_t, ReceivePromise::Pointer promise) override { promise->reject(aasdk::error::Error(aasdk::error::ErrorCode::OPERATION_ABORTED)); }
    void send(Data, SendPromise::Pointer promise) override { promise->resolve(); }
    void stop() override { m_hasStopped = true; }
    bool m_hasStopped{};
};
// A phone that connected but never says anything: the read stays pending until stop().
class SilentTransport final : public aasdk::transport::ITransport {
public:
    void receive(std::size_t, ReceivePromise::Pointer promise) override { m_pending = std::move(promise); }
    void send(Data, SendPromise::Pointer promise) override { promise->resolve(); }
    void stop() override {
        m_hasStopped = true;
        if (m_pending) std::exchange(m_pending, nullptr)->reject(aasdk::error::Error(aasdk::error::ErrorCode::OPERATION_ABORTED));
    }
    ReceivePromise::Pointer m_pending;
    bool m_hasStopped{};
};
std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
// The user presses "Verbindung beenden" while the phone has not answered yet: the session
// must end promptly, stop its transport and leave no object behind.
void TestStopWhileWaitingForPhone() {
    const auto logPath = std::filesystem::temp_directory_path() / "headunit-protocol-stop-tests.log";
    std::filesystem::remove(logPath);
    headunit::Logger logger(logPath);
    std::atomic_bool isStopRequested{false};
    auto transport = std::make_shared<SilentTransport>();
    std::thread user([&] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); isStopRequested = true; });
    const auto begin = std::chrono::steady_clock::now();
    const auto result = headunit::RunAndroidAutoSession(transport, logger, isStopRequested, {});
    user.join();
    Check(std::chrono::steady_clock::now() - begin < std::chrono::seconds(5), "Stopping a waiting session took too long");
    Check(transport->m_hasStopped && !result.hasVideo, "Stopped session kept its transport or reported video");
    Check(result.message.find("stopped by user") != std::string::npos, "Stop reason was not reported");
    Check(ReadFile(logPath).find("ownership cycle") == std::string::npos, "Session leaked after being stopped");
}
// stop() must be repeatable and must reject later requests instead of dropping them,
// otherwise a protocol layer waits forever for a completion that never comes.
void TestTransportStop() {
    boost::asio::io_context io;
    auto transport = std::make_shared<headunit::ProjectionTransport>(nullptr, 0x81, 0x01);
    transport->stop();
    transport->stop();
    int rejected = 0;
    auto receive = aasdk::transport::ITransport::ReceivePromise::defer(io);
    receive->then([](Data) {}, [&](const aasdk::error::Error& error) { rejected += error.getCode() == aasdk::error::ErrorCode::OPERATION_ABORTED; });
    transport->receive(16, receive);
    auto send = aasdk::transport::ITransport::SendPromise::defer(io);
    send->then([] {}, [&](const aasdk::error::Error& error) { rejected += error.getCode() == aasdk::error::ErrorCode::OPERATION_ABORTED; });
    transport->send(Data(4), send);
    io.run();
    Check(rejected == 2, "Requests after stop() were not rejected");
}
}
void RunInputAudioTests();
#ifdef HEADUNIT_WIRELESS_TESTS
void RunWirelessTests();
#endif
int main() {
    try {
        TestTls(TLS1_2_VERSION);
        TestTls(TLS1_3_VERSION);
        headunit::Logger logger(std::filesystem::temp_directory_path() / "headunit-protocol-tests.log");
        std::atomic_bool isStopRequested{true};
        auto transport = std::make_shared<StoppedTransport>();
        const auto result = headunit::RunAndroidAutoSession(transport, logger, isStopRequested, {});
        Check(!result.hasVideo && transport->m_hasStopped, "Cancelled session reported video or retained transport");
        TestStopWhileWaitingForPhone();
        TestTransportStop();
        RunInputAudioTests();
#ifdef HEADUNIT_WIRELESS_TESTS
        RunWirelessTests();
#endif
        std::cout << "TLS 1.2/1.3 variable record sizes, session cancellation and transport shutdown passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
