#include "androidauto/PhoneWatch.h"
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <vector>

using namespace headunit;
void Check(bool isValid, const char* message);

namespace {
Logger& TestLogger() {
    static Logger logger(std::filesystem::temp_directory_path() / "headunit-phonewatch-tests.log");
    return logger;
}
AutoConnectResult Video() { AutoConnectResult result; result.hasVideo = true; result.message = "Android Auto stopped by user"; return result; }
using Bus = std::vector<std::string>;
std::vector<Bus> Repeat(const Bus& bus, int times) { return std::vector<Bus>(static_cast<std::size_t>(times), bus); }
std::vector<Bus> Join(std::vector<Bus> first, const std::vector<Bus>& second) { first.insert(first.end(), second.begin(), second.end()); return first; }

// Scripted world: every USB look takes the next entry of `bus` (the last one repeats), every wireless wait the next
// entry of `wireless` (-1 once it runs out). The watch stops after `rounds` waits.
struct World {
    std::vector<Bus> bus;
    std::vector<int> wireless;
    int rounds{30};
    bool hasWireless{true};
    int failingScan{-1};                   // this look throws
    std::function<void(World&)> onWait;    // runs in every wait, before the stop check
    std::function<void(World&)> duringUsb; // runs inside every USB attempt
    std::atomic_bool stop{false}, attemptStop{false}, usbRequested{false};
    int scanCalls{}, waitCalls{}, usbConnects{}, wirelessConnects{}, starts{}, ends{};
    std::vector<int> servedSockets;
    PhoneWatchDeps Deps() {
        PhoneWatchDeps deps;
        deps.usbPhones = [this] {
            const int call = scanCalls++;
            if (call == failingScan) throw std::runtime_error("libusb_init: LIBUSB_ERROR_OTHER");
            return bus.at(std::min<std::size_t>(static_cast<std::size_t>(call), bus.size() - 1));
        };
        deps.connectUsb = [this] { ++usbConnects; if (duringUsb) duringUsb(*this); return Video(); };
        const auto pace = [this] { if (onWait) onWait(*this); if (++waitCalls >= rounds) stop = true; };
        if (hasWireless) {
            deps.waitForWirelessPhone = [this, pace](std::chrono::milliseconds) {
                const auto index = static_cast<std::size_t>(waitCalls);
                const int socket = index < wireless.size() ? wireless[index] : -1;
                pace();
                return socket;
            };
            deps.connectWireless = [this](int socket) { ++wirelessConnects; servedSockets.push_back(socket); return Video(); };
        }
        deps.wait = [pace](std::chrono::milliseconds) { pace(); };
        deps.onAttemptStart = [this] { ++starts; };
        deps.onAttemptEnd = [this](const AutoConnectResult&) { ++ends; };
        return deps;
    }
    AutoConnectResult Run() {
        const auto deps = Deps();
        return RunPhoneWatch(deps, TestLogger(), stop, attemptStop, usbRequested, {});
    }
};
UsbDevice Device(std::uint16_t vendor, std::uint16_t product, std::string serial, std::string location) {
    UsbDevice device;
    device.vendorId = vendor;
    device.productId = product;
    device.serial = std::move(serial);
    device.location = std::move(location);
    return device;
}
}

void TestPhoneWatch() {
    const Bus phone{"serial R5GL65Y1FSY"};
    {   // Plugged in already at the start: one attempt, and no second one while the phone stays.
        World world;
        world.bus = {phone};
        const auto result = world.Run();
        Check(world.usbConnects == 1 && world.starts == 1 && world.ends == 1, "A plugged-in phone did not get exactly one attempt");
        Check(result.isStoppedByUser && !result.hasVideo, "The watch did not end as stopped");
    }
    {   // Unplugged and plugged in again after the attempt: a second attempt.
        World world;
        world.bus = Join(Join(Repeat(phone, 9), Repeat({}, 2)), {phone});
        world.Run();
        Check(world.usbConnects == 2, "Plugging the phone in again did not start Android Auto again");
    }
    {   // A phone without a serial number changes its identity when it leaves accessory mode: not a new phone.
        World world;
        world.bus = Join({{"usb:1-1.1 04E8:6860"}, {"usb:1-1 18D1:2D00"}, {}, {"usb:1-1.1 04E8:6860"}}, Repeat({"usb:1-1.1 04E8:6860"}, 1));
        world.Run();
        Check(world.usbConnects == 1, "The phone re-enumerating after its session was taken for a new phone");
    }
    {   // "Android Auto verbinden" connects the phone that stays plugged in once more.
        World world;
        world.bus = {phone};
        world.onWait = [](World& w) { if (w.waitCalls == 12) w.usbRequested = true; };
        world.Run();
        Check(world.usbConnects == 2, "The button did not start a new USB attempt");
    }
    {   // A phone over Bluetooth: its socket goes to the wireless flow.
        World world;
        world.bus = {Bus{}};
        world.wireless = {-1, -1, 7};
        world.Run();
        Check(world.wirelessConnects == 1 && world.servedSockets == std::vector<int>{7} && world.usbConnects == 0 && world.starts == 1,
            "The wireless phone was not served");
    }
    {   // A charging cable plugged in during a wireless session does not start USB afterwards.
        World world;
        world.bus = Join(Repeat({}, 3), {phone});
        world.wireless = {-1, -1, 7};
        world.Run();
        Check(world.wirelessConnects == 1 && world.usbConnects == 0, "A phone plugged in during a wireless session was taken over by USB");
    }
    {   // The stop request of the last attempt does not end the next one before it starts.
        World world;
        world.bus = {phone};
        world.attemptStop = true;
        bool wasCleared = false;
        world.duringUsb = [&wasCleared](World& w) { wasCleared = !w.attemptStop; };
        world.Run();
        Check(wasCleared, "The attempt started with the previous stop request still set");
    }
    {   // Without wireless the watch paces itself and still finds the phone.
        World world;
        world.hasWireless = false;
        world.bus = Join(Repeat({}, 3), {phone});
        world.Run();
        Check(world.usbConnects == 1 && world.waitCalls >= 3, "The USB-only watch did not find the phone");
    }
    {   // A failing scan changes nothing: the phone known before is not taken for new.
        World world;
        world.bus = {phone};
        world.failingScan = 12;
        world.Run();
        Check(world.usbConnects == 1, "A failed USB scan made a known phone look new");
    }
    {   // Identities: the serial number where there is one, the port and ids otherwise; other devices are left out.
        UsbScanResult scan;
        scan.devices.push_back(Device(0x04e8, 0x6860, "R5GL65Y1FSY", "usb:1-1.1"));
        scan.devices.push_back(Device(0x18d1, 0x2d00, "<not supplied>", "usb:1-1"));
        scan.devices.push_back(Device(0x0eef, 0x0005, "220211", "usb:1-1.3"));
        const auto phones = UsbPhoneIdentities(scan);
        Check(phones == std::vector<std::string>{"serial R5GL65Y1FSY", "usb:1-1 18D1:2D00"}, "USB phone identities are wrong");
    }
}
