#include "ui/MainWindow.h"
#include "androidauto/AndroidDeviceDetector.h"
#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QImage>
#include <QPixmap>
#include <QtConcurrent/QtConcurrentRun>
#include <exception>

namespace headunit {
namespace {
QString Text(const std::string& value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
QString Hex(unsigned value, int width = 4) { return QString::number(value, 16).rightJustified(width, '0').toUpper(); }
bool IsAndroid(const UsbDevice& device) { return DetectAndroidDevice(device).evidence != AndroidEvidence::None; }
// After a session the phone leaves or re-enters accessory mode; scanning immediately
// would race its re-enumeration and show a stale or empty device list.
constexpr int kRescanDelayMs = 1500;
}
MainWindow::MainWindow(IUsbBackend& backend, Logger& logger, bool isSmokeTest, bool isProjectionTest)
    : m_backend(backend), m_logger(logger), m_isSmokeTest(isSmokeTest), m_isProjectionTest(isProjectionTest)
{
    setWindowTitle("Android Auto Test Headunit");
    resize(1100, 950);
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    m_video = new QLabel("Android Auto: noch kein Videostream", central);
    m_video->setAlignment(Qt::AlignCenter);
    m_video->setMinimumSize(800, 480);
    m_video->setStyleSheet("background: #111; color: #ddd; font-size: 18px;");
    layout->addWidget(m_video);
    m_status = new QLabel("USB: not scanned | AA: disconnected", central);
    layout->addWidget(m_status);
    m_scanButton = new QPushButton("Scan USB devices", central);
    layout->addWidget(m_scanButton);
    m_probeButton = new QPushButton("Android Auto: USB-Zugriff pruefen", central);
    layout->addWidget(m_probeButton);
    m_accessoryButton = new QPushButton("Android Auto: Accessory-Modus starten", central);
    layout->addWidget(m_accessoryButton);
    m_connectButton = new QPushButton("Android Auto verbinden", central);
    layout->addWidget(m_connectButton);
    m_stopButton = new QPushButton("Verbindung beenden", central);
    layout->addWidget(m_stopButton);
    m_connectionStatus = new QLabel("Handy anschliessen und Android Auto verbinden. Hinweise auf dem Handy bestaetigen.", central);
    m_connectionStatus->setWordWrap(true);
    m_connectionStatus->setTextFormat(Qt::PlainText);
    m_connectionStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_connectionStatus);
    m_devices = new QTreeWidget(central);
    m_devices->setHeaderLabels({"Device / property", "Value / evidence"});
    m_devices->setColumnWidth(0, 340);
    layout->addWidget(m_devices, 1);
    setCentralWidget(central);
    SetState(State::Idle);
    connect(m_scanButton, &QPushButton::clicked, this, &MainWindow::StartScan);
    connect(m_probeButton, &QPushButton::clicked, this, [this] { StartUsbProbe(); });
    connect(m_accessoryButton, &QPushButton::clicked, this, [this] { StartUsbProbe(true); });
    connect(m_connectButton, &QPushButton::clicked, this, [this] { StartUsbProbe(true, true); });
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::RequestStop);
    auto* frameTimer = new QTimer(this);
    connect(frameTimer, &QTimer::timeout, this, [this] {
        std::optional<VideoFrame> frame;
        { std::lock_guard lock(m_frameMutex); frame.swap(m_latestFrame); }
        if (!frame) return;
        QImage image(frame->pixels.data(), frame->width, frame->height, frame->stride, QImage::Format_RGB888);
        m_video->setPixmap(QPixmap::fromImage(image).scaled(m_video->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (++m_displayedFrames == 1) m_logger.Write("INFO", "VIDEO", "First real Android Auto frame displayed in Qt");
        m_status->setText(QString("Android Auto: Video %1x%2 | %3 frames displayed").arg(frame->width).arg(frame->height).arg(m_displayedFrames));
        if (m_isProjectionTest && m_displayedFrames >= 10) { m_isStopRequested = true; QApplication::exit(0); }
    });
    frameTimer->start(33);
    connect(&m_probeWatcher, &QFutureWatcher<UsbProbeResult>::finished, this, [this] { FinishProbe(m_probeWatcher.result()); });
    connect(&m_watcher, &QFutureWatcher<UsbScanResult>::finished, this, &MainWindow::FinishScan);
    QTimer::singleShot(0, this, &MainWindow::StartScan);
}
MainWindow::~MainWindow()
{
    m_isStopRequested = true;
    // Keep backend and logger alive until the outstanding scan releases all handles.
    m_watcher.waitForFinished();
    m_probeWatcher.waitForFinished();
}
void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_state == State::Connecting || m_state == State::Stopping) {
        // Closing mid-session first lets the session say goodbye to the phone and release
        // the USB interface; FinishProbe closes the window once that has happened.
        m_isCloseRequested = true;
        RequestStop();
        m_connectionStatus->setText("Beende Android Auto, das Fenster schliesst danach ...");
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}
void MainWindow::SetState(State state)
{
    m_state = state;
    const bool isIdle = state == State::Idle;
    const bool hasPhone = isIdle && m_candidateCount > 0;
    m_scanButton->setEnabled(isIdle);
    m_probeButton->setEnabled(hasPhone);
    m_accessoryButton->setEnabled(hasPhone);
    m_connectButton->setEnabled(hasPhone);
    m_stopButton->setEnabled(state == State::Connecting);
    m_stopButton->setText(state == State::Stopping ? "Beende ..." : "Verbindung beenden");
}
void MainWindow::StartScan()
{
    if (m_state != State::Idle) return;
    BeginScan(false);
}
void MainWindow::BeginScan(bool isAutomatic)
{
    m_candidateCount = 0;
    SetState(State::Scanning);
    m_lastDevices.clear();
    // An automatic rescan follows a result the user has not read yet; keep that text.
    if (!isAutomatic) m_connectionStatus->setText("Nach dem Scan das Handy auswaehlen und Android Auto verbinden.");
    m_status->setText("USB: scanning | AA: disconnected");
    m_devices->clear();
    m_watcher.setFuture(QtConcurrent::run([this] {
        try { return m_backend.EnumerateDevices(); }
        catch (const std::exception& error) {
            m_logger.Write("ERROR", "USB", error.what());
            UsbScanResult result;
            result.errors.push_back(error.what());
            return result;
        }
    }));
}
void MainWindow::FinishScan()
{
    const auto result = m_watcher.result();
    m_lastDevices = result.devices;
    int candidateCount = 0;
    int deviceIndex = 0;
    for (const auto& device : result.devices) {
        const auto detection = DetectAndroidDevice(device);
        if (detection.evidence != AndroidEvidence::None) ++candidateCount;
        auto* row = new QTreeWidgetItem(m_devices, {Hex(device.vendorId) + ":" + Hex(device.productId) + " " + Text(device.product), Text(detection.reason)});
        row->setData(0, Qt::UserRole, deviceIndex++);
        if (candidateCount == 1 && detection.evidence != AndroidEvidence::None) m_devices->setCurrentItem(row);
        new QTreeWidgetItem(row, {"Manufacturer", Text(device.manufacturer)});
        new QTreeWidgetItem(row, {"Serial", Text(device.serial)});
        new QTreeWidgetItem(row, {"Location", Text(device.location)});
        new QTreeWidgetItem(row, {"Active configuration", QString::number(device.activeConfiguration)});
        for (const auto& config : device.configurations) {
            auto* configuration = new QTreeWidgetItem(row, {"Configuration", QString::number(config.value)});
            for (const auto& interface : config.interfaces) {
                auto* interfaceRow = new QTreeWidgetItem(configuration, {
                    QString("Interface %1 / alt %2").arg(interface.number).arg(interface.alternateSetting),
                    Hex(interface.classCode, 2) + "/" + Hex(interface.subclassCode, 2) + "/" + Hex(interface.protocolCode, 2)});
                for (const auto& endpoint : interface.endpoints)
                    new QTreeWidgetItem(interfaceRow, {"Endpoint 0x" + Hex(endpoint.address, 2),
                        QString("%1 type=%2 maxPacket=%3 interval=%4").arg((endpoint.address & 0x80) ? "IN" : "OUT")
                        .arg(endpoint.attributes & 3).arg(endpoint.maxPacketSize).arg(endpoint.interval)});
            }
        }
        for (const auto& diagnostic : device.diagnostics) new QTreeWidgetItem(row, {"Warning", Text(diagnostic)});
    }
    for (const auto& error : result.errors) new QTreeWidgetItem(m_devices, {"Scan error", Text(error)});
    m_status->setText(QString("USB: %1 devices, %2 Android candidates, %3 scan errors | AA: disconnected")
        .arg(result.devices.size()).arg(candidateCount).arg(result.errors.size()));
    m_candidateCount = candidateCount;
    SetState(State::Idle);
    m_logger.Write("INFO", "UI", "Scan result displayed in Qt window");
    if (m_isSmokeTest) QTimer::singleShot(250, this, [result] { QApplication::exit(result.errors.empty() ? 0 : 2); });
    if (m_isProjectionTest && !m_hasTestStarted) {
        m_hasTestStarted = true;
        if (candidateCount != 1 || !result.errors.empty()) { QApplication::exit(3); return; }
        StartUsbProbe(true, true);
    }
}
void MainWindow::FinishProbe(const UsbProbeResult& result)
{
    const bool wasProjection = m_state == State::Connecting || m_state == State::Stopping;
    SetState(State::Idle);
    if (m_isProjectionTest) { QApplication::exit(m_displayedFrames >= 10 ? 0 : 3); return; }
    if (m_isCloseRequested) { close(); return; }
    m_connectionStatus->setText(Text(result.stage + ": " + result.message));
    if (wasProjection) {
        { std::lock_guard lock(m_frameMutex); m_latestFrame.reset(); }
        m_video->clear();
        m_video->setText("Android Auto: Sitzung beendet");
        m_status->setText("Android Auto: Sitzung beendet");
        // Hold the controls back until the rescan has found the phone in its new mode.
        m_candidateCount = 0;
        m_lastDevices.clear();
        m_devices->clear();
        SetState(State::Scanning);
        QTimer::singleShot(kRescanDelayMs, this, [this] { BeginScan(true); });
    } else if (m_isAccessoryAttempt) {
        BeginScan(true);
    }
}
void MainWindow::RequestStop()
{
    if (m_state != State::Connecting) return;
    m_isStopRequested = true;
    SetState(State::Stopping);
    m_connectionStatus->setText("Beende Android Auto ...");
}
std::optional<UsbDevice> MainWindow::SelectedDevice() const
{
    if (const auto* row = m_devices->currentItem()) {
        while (row->parent()) row = row->parent();
        const auto data = row->data(0, Qt::UserRole);
        if (data.isValid()) {
            const auto index = data.toInt();
            if (index >= 0 && static_cast<std::size_t>(index) < m_lastDevices.size() && IsAndroid(m_lastDevices[index]))
                return m_lastDevices[index];
        }
    }
    // Nothing usable is selected: with exactly one Android phone attached there is no
    // real choice to make, so it is used directly.
    std::optional<UsbDevice> only;
    int candidates = 0;
    for (const auto& device : m_lastDevices)
        if (IsAndroid(device)) { only = device; ++candidates; }
    return candidates == 1 ? only : std::nullopt;
}
void MainWindow::StartUsbProbe(bool isStartAccessory, bool isProjection)
{
    if (m_state != State::Idle) return;
    const auto selected = SelectedDevice();
    if (!selected) { m_connectionStatus->setText("Bitte ein erkanntes Android-Geraet auswaehlen."); return; }
    const auto device = *selected;
    m_isStopRequested = false;
    SetState(isProjection ? State::Connecting : State::Probing);
    if (isProjection) {
        m_displayedFrames = 0;
        { std::lock_guard lock(m_frameMutex); m_latestFrame.reset(); }
        m_video->clear();
        m_video->setText("Warte auf echten Android-Auto-Videostream ...");
    }
    m_isAccessoryAttempt = isStartAccessory;
    m_connectionStatus->setText(isProjection ? "Verbinde Android Auto: Accessory-Modus, dann Protokollsitzung. Hinweise auf dem Handy bestaetigen ..."
        : isStartAccessory ? "Starte Accessory-Modus und warte auf die erneute USB-Anmeldung ..." : "Pruefe den USB-Zugriff und frage AOA-Unterstuetzung ab ...");
    m_probeWatcher.setFuture(QtConcurrent::run([this, device, isStartAccessory, isProjection] {
        if (isProjection) return ConnectAndroidAuto(device, m_logger, m_isStopRequested, {
            [this](const std::string& status) { QMetaObject::invokeMethod(this, [this, status] { m_connectionStatus->setText(Text(status)); }, Qt::QueuedConnection); },
            [this](VideoFrame frame) { std::lock_guard lock(m_frameMutex); m_latestFrame = std::move(frame); }
        });
        return isStartAccessory ? StartAndroidAccessory(device, m_logger) : ProbeAndroidUsb(device, m_logger);
    }));
}
}
