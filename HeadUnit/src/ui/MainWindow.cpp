#include "ui/MainWindow.h"
#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QImage>
#include <QPixmap>
#include <QtConcurrent/QtConcurrentRun>
#include <exception>

namespace headunit {
namespace {
QString Text(const std::string& value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
constexpr int kProjectionTestFrames = 10;
}
MainWindow::MainWindow(IUsbBackend& backend, Logger& logger, bool isSmokeTest, bool isProjectionTest)
    : m_backend(backend), m_logger(logger), m_isSmokeTest(isSmokeTest), m_isProjectionTest(isProjectionTest)
{
    setWindowTitle("Android Auto Headunit");
    resize(1000, 800);
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    m_video = new QLabel("Android Auto ist nicht verbunden.", central);
    m_video->setAlignment(Qt::AlignCenter);
    m_video->setMinimumSize(800, 480);
    m_video->setStyleSheet("background: #111; color: #ddd; font-size: 18px;");
    layout->addWidget(m_video, 1);
    m_button = new QPushButton(central);
    m_button->setMinimumHeight(48);
    layout->addWidget(m_button);
    m_step = new QLabel("Handy per USB-Kabel anschliessen, entsperren und auf Android Auto verbinden klicken.", central);
    m_step->setWordWrap(true);
    m_step->setTextFormat(Qt::PlainText);
    m_step->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_step);
    m_status = new QLabel(central);
    layout->addWidget(m_status);
    m_history = new QPlainTextEdit(central);
    m_history->setReadOnly(true);
    m_history->setMaximumBlockCount(500);
    m_history->setMaximumHeight(140);
    layout->addWidget(m_history);
    setCentralWidget(central);
    SetState(State::Idle);
    connect(m_button, &QPushButton::clicked, this, &MainWindow::OnButton);
    auto* frameTimer = new QTimer(this);
    connect(frameTimer, &QTimer::timeout, this, [this] {
        std::optional<VideoFrame> frame;
        { std::lock_guard lock(m_frameMutex); frame.swap(m_latestFrame); }
        if (!frame) return;
        QImage image(frame->pixels.data(), frame->width, frame->height, frame->stride, QImage::Format_RGB888);
        m_video->setPixmap(QPixmap::fromImage(image).scaled(m_video->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (++m_displayedFrames == 1) m_logger.Write("INFO", "VIDEO", "First real Android Auto frame displayed in Qt");
        m_status->setText(QString("Android Auto: Video %1x%2 | %3 Bilder angezeigt").arg(frame->width).arg(frame->height).arg(m_displayedFrames));
        if (m_isProjectionTest && m_displayedFrames >= kProjectionTestFrames) { m_isStopRequested = true; QApplication::exit(0); }
    });
    frameTimer->start(33);
    connect(&m_watcher, &QFutureWatcher<AutoConnectResult>::finished, this, [this] { FinishConnect(m_watcher.result()); });
    connect(&m_scanWatcher, &QFutureWatcher<UsbScanResult>::finished, this, [this] {
        const auto result = m_scanWatcher.result();
        QTimer::singleShot(250, this, [errors = result.errors.size()] { QApplication::exit(errors == 0 ? 0 : 2); });
    });
    if (m_isSmokeTest) {
        // Smoke test: real window plus one real USB scan, then exit.
        m_scanWatcher.setFuture(QtConcurrent::run([this] {
            try { return m_backend.EnumerateDevices(); }
            catch (const std::exception& error) { UsbScanResult result; result.errors.push_back(error.what()); return result; }
        }));
    }
    if (m_isProjectionTest) QTimer::singleShot(0, this, &MainWindow::StartConnect);
}
MainWindow::~MainWindow()
{
    m_isStopRequested = true;
    // Keep backend and logger alive until the worker has released the USB interface.
    m_watcher.waitForFinished();
    m_scanWatcher.waitForFinished();
}
void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_state != State::Idle) {
        // Closing mid-session first lets the session say goodbye to the phone and release
        // the USB interface; FinishConnect closes the window once that has happened.
        m_isCloseRequested = true;
        RequestStop();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}
void MainWindow::SetState(State state)
{
    m_state = state;
    m_button->setEnabled(state != State::Stopping);
    m_button->setText(state == State::Idle ? "Android Auto verbinden" : state == State::Connecting ? "Verbindung beenden" : "Beende ...");
}
void MainWindow::OnButton()
{
    if (m_state == State::Idle) StartConnect();
    else RequestStop();
}
void MainWindow::ShowStep(const QString& text)
{
    m_step->setText(text);
    m_history->appendPlainText(text);
}
void MainWindow::StartConnect()
{
    if (m_state != State::Idle) return;
    m_isStopRequested = false;
    m_displayedFrames = 0;
    { std::lock_guard lock(m_frameMutex); m_latestFrame.reset(); }
    m_video->clear();
    m_video->setText("Verbinde Android Auto ...");
    m_status->clear();
    m_history->clear();
    SetState(State::Connecting);
    m_watcher.setFuture(QtConcurrent::run([this] {
        return ConnectPhoneAutomatically(m_backend, m_logger, m_isStopRequested, {
            [this](const std::string& status) { QMetaObject::invokeMethod(this, [this, status] { ShowStep(Text(status)); }, Qt::QueuedConnection); },
            [this](VideoFrame frame) { std::lock_guard lock(m_frameMutex); m_latestFrame = std::move(frame); }
        });
    }));
}
void MainWindow::RequestStop()
{
    if (m_state != State::Connecting) return;
    m_isStopRequested = true;
    SetState(State::Stopping);
    ShowStep("Beende Android Auto ...");
}
void MainWindow::FinishConnect(const AutoConnectResult& result)
{
    SetState(State::Idle);
    if (m_isProjectionTest) { QApplication::exit(m_displayedFrames >= kProjectionTestFrames ? 0 : 3); return; }
    if (m_isCloseRequested) { close(); return; }
    { std::lock_guard lock(m_frameMutex); m_latestFrame.reset(); }
    m_video->clear();
    m_video->setText(result.hasVideo || result.isStoppedByUser ? "Android Auto beendet." : "Android Auto konnte nicht verbunden werden.");
    m_status->setText("Android Auto: nicht verbunden");
    ShowStep(Text(result.message));
}
}
