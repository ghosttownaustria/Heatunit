#include "ui/MainWindow.h"
#include "ui/CarWidgets.h"
#include <QApplication>
#include <QDir>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <array>
#include <exception>

namespace headunit {
namespace {
QString Text(const std::string& value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
constexpr unsigned kProjectionTestFrames = 10;
// Middle of the phone's screen, for the scripted touch test.
constexpr int kTestTouchX = kTouchWidth / 2;
constexpr int kTestTouchY = kTouchHeight / 2;
}
MainWindow::MainWindow(IUsbBackend& backend, Logger& logger, TestMode mode)
    : m_backend(backend), m_logger(logger), m_mode(mode),
      m_input(std::make_shared<ProjectionInput>()), m_audioState(std::make_shared<AudioState>()),
      m_audio(std::make_unique<WasapiAudioEngine>(m_audioState, logger))
{
    setWindowTitle("Android Auto Headunit");
    resize(1240, 800);
    auto* central = new QWidget(this);
    auto* root = new QHBoxLayout(central);
    auto* left = new QVBoxLayout();
    m_video = new VideoWidget(central);
    m_video->ClearFrame("Android Auto ist nicht verbunden.");
    m_video->onTouch = [this](TouchAction action, int x, int y) { m_input->Touch(action, x, y); };
    left->addWidget(m_video, 1);
    m_button = new QPushButton(central);
    m_button->setMinimumHeight(48);
    m_button->setFocusPolicy(Qt::NoFocus);
    left->addWidget(m_button);
    m_step = new QLabel("Handy per USB-Kabel anschliessen, entsperren und auf Android Auto verbinden klicken.", central);
    m_step->setWordWrap(true);
    m_step->setTextFormat(Qt::PlainText);
    m_step->setTextInteractionFlags(Qt::TextSelectableByMouse);
    left->addWidget(m_step);
    m_status = new QLabel(central);
    left->addWidget(m_status);
    m_history = new QPlainTextEdit(central);
    m_history->setReadOnly(true);
    m_history->setMaximumBlockCount(500);
    m_history->setMaximumHeight(120);
    m_history->setFocusPolicy(Qt::NoFocus);
    left->addWidget(m_history);
    root->addLayout(left, 1);
    m_panel = new CarPanel(central);
    m_panel->setFixedWidth(310);
    m_panel->onKey = [this](unsigned keycode, bool isDown) { m_input->Key(keycode, isDown); };
    m_panel->onRotate = [this](int detents) { m_input->Rotate(detents); };
    m_panel->onVolume = [this](int delta) { m_audioState->ChangeVolume(delta); };
    m_panel->onMute = [this] { m_audioState->ToggleMute(); };
    root->addWidget(m_panel);
    setCentralWidget(central);
    SetState(State::Idle);
    connect(m_button, &QPushButton::clicked, this, &MainWindow::OnButton);
    auto* tickTimer = new QTimer(this);
    connect(tickTimer, &QTimer::timeout, this, &MainWindow::Tick);
    tickTimer->start(33);
    connect(&m_watcher, &QFutureWatcher<AutoConnectResult>::finished, this, [this] { FinishConnect(m_watcher.result()); });
    connect(&m_scanWatcher, &QFutureWatcher<UsbScanResult>::finished, this, [this] {
        const auto result = m_scanWatcher.result();
        QTimer::singleShot(250, this, [errors = result.errors.size()] { QApplication::exit(errors == 0 ? 0 : 2); });
    });
    if (m_mode == TestMode::Smoke) {
        // Smoke test: real window plus one real USB scan, then exit.
        m_scanWatcher.setFuture(QtConcurrent::run([this] {
            try { return m_backend.EnumerateDevices(); }
            catch (const std::exception& error) { UsbScanResult result; result.errors.push_back(error.what()); return result; }
        }));
    } else if (m_mode != TestMode::None) {
        // Scripted runs must not blast the phone's music: they start quiet.
        m_audioState->SetVolume(5);
        QTimer::singleShot(0, this, &MainWindow::StartConnect);
    }
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
bool MainWindow::HandleKey(QKeyEvent* event, bool isDown)
{
    unsigned keycode = 0;
    bool isArrow = false;
    switch (event->key()) {
    case Qt::Key_Up: keycode = keys::DpadUp; isArrow = true; break;
    case Qt::Key_Down: keycode = keys::DpadDown; isArrow = true; break;
    case Qt::Key_Left: keycode = keys::DpadLeft; isArrow = true; break;
    case Qt::Key_Right: keycode = keys::DpadRight; isArrow = true; break;
    case Qt::Key_Return: case Qt::Key_Enter: keycode = keys::DpadCenter; break;
    case Qt::Key_Escape: case Qt::Key_Backspace: keycode = keys::Back; break;
    case Qt::Key_Home: keycode = keys::Home; break;
    case Qt::Key_Space: keycode = keys::MediaPlayPause; break;
    case Qt::Key_PageUp: keycode = keys::MediaPrevious; break;
    case Qt::Key_PageDown: keycode = keys::MediaNext; break;
    default: break;
    }
    if (keycode != 0) {
        if (event->isAutoRepeat()) { if (isDown && isArrow) m_input->Tap(keycode); }  // held arrows keep nudging
        else m_input->Key(keycode, isDown);
        return true;
    }
    if (!isDown) return false;
    switch (event->key()) {
    case Qt::Key_Plus: case Qt::Key_Equal: m_audioState->ChangeVolume(+1); return true;
    case Qt::Key_Minus: m_audioState->ChangeVolume(-1); return true;
    case Qt::Key_M: m_audioState->ToggleMute(); return true;
    default: return false;
    }
}
void MainWindow::keyPressEvent(QKeyEvent* event) { if (!HandleKey(event, true)) QMainWindow::keyPressEvent(event); }
void MainWindow::keyReleaseEvent(QKeyEvent* event) { if (!HandleKey(event, false)) QMainWindow::keyReleaseEvent(event); }
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
void MainWindow::Tick()
{
    std::optional<VideoFrame> frame;
    { std::lock_guard lock(m_frameMutex); frame.swap(m_latestFrame); }
    if (frame) {
        m_video->SetFrame(*frame);
        if (++m_displayedFrames == 1) m_logger.Write("INFO", "VIDEO", "First real Android Auto frame displayed in Qt");
        m_status->setText(QString("Android Auto: Video %1x%2 | %3 Bilder angezeigt").arg(frame->width).arg(frame->height).arg(m_displayedFrames));
        if (m_mode == TestMode::Projection && m_displayedFrames >= kProjectionTestFrames) { m_isStopRequested = true; QApplication::exit(0); }
    }
    std::array<AudioState::Meter, kAudioKindCount> meters;
    for (int i = 0; i < kAudioKindCount; ++i) meters[i] = m_audioState->ReadMeter(static_cast<AudioKind>(i));
    m_panel->Display()->SetState(m_audioState->Volume(), m_audioState->IsMuted(), meters);
    if (m_state == State::Connecting && !m_isTestFinished) {
        if (m_mode == TestMode::Input) RunInputTest();
        else if (m_mode == TestMode::Audio) RunAudioTest();
    }
}
void MainWindow::StartConnect()
{
    if (m_state != State::Idle) return;
    m_isStopRequested = false;
    m_displayedFrames = 0;
    { std::lock_guard lock(m_frameMutex); m_latestFrame.reset(); }
    m_video->ClearFrame("Verbinde Android Auto ...");
    m_status->clear();
    m_history->clear();
    SetState(State::Connecting);
    m_watcher.setFuture(QtConcurrent::run([this] {
        ProjectionCallbacks callbacks;
        callbacks.onStatus = [this](const std::string& status) { QMetaObject::invokeMethod(this, [this, status] { ShowStep(Text(status)); }, Qt::QueuedConnection); };
        callbacks.onFrame = [this](VideoFrame frame) { std::lock_guard lock(m_frameMutex); m_latestFrame = std::move(frame); };
        callbacks.input = m_input;
        callbacks.openAudio = m_audio->Opener();
        return ConnectPhoneAutomatically(m_backend, m_logger, m_isStopRequested, std::move(callbacks));
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
    if (m_mode == TestMode::Projection) { QApplication::exit(m_displayedFrames >= kProjectionTestFrames ? 0 : 3); return; }
    if (m_mode == TestMode::Input || m_mode == TestMode::Audio) { QApplication::exit(m_isTestFinished ? m_testExitCode : 3); return; }
    if (m_isCloseRequested) { close(); return; }
    { std::lock_guard lock(m_frameMutex); m_latestFrame.reset(); }
    m_video->ClearFrame(result.hasVideo || result.isStoppedByUser ? "Android Auto beendet." : "Android Auto konnte nicht verbunden werden.");
    m_status->setText("Android Auto: nicht verbunden");
    ShowStep(Text(result.message));
}
void MainWindow::FinishTest(int exitCode, const std::string& summary)
{
    m_logger.Write(exitCode == 0 ? "INFO" : "ERROR", "TEST", summary);
    m_testExitCode = exitCode;
    m_isTestFinished = true;
    m_isStopRequested = true;   // ends the session cleanly; FinishConnect then exits with the result
}
// Test runs can save what the phone drew (and the whole window) into the directory named by
// HEADUNIT_TEST_SHOTS, so a person can look at the evidence.
void MainWindow::SaveTestShot(const char* name, bool isWholeWindow)
{
    const QByteArray directory = qgetenv("HEADUNIT_TEST_SHOTS");
    if (directory.isEmpty()) return;
    QDir().mkpath(QString::fromLocal8Bit(directory));
    const QString path = QDir(QString::fromLocal8Bit(directory)).filePath(QString::fromLatin1(name));
    if (isWholeWindow) grab().save(path);
    else if (m_video->HasFrame()) m_video->Image().save(path);
}
// Sends rotary, key and touch input to the real phone and counts how many new picture frames it
// draws in response. A phone that ignores the input draws none.
void MainWindow::RunInputTest()
{
    const qint64 elapsed = m_testClock.isValid() ? m_testClock.elapsed() : 0;
    switch (m_testStage) {
    case 0:  // wait for the picture
        if (m_displayedFrames >= kProjectionTestFrames && m_input->IsAttached()) { m_testClock.restart(); m_testStage = 1; }
        break;
    case 1:  // let the phone settle
        if (elapsed >= 2500) { m_testFrames[0] = m_displayedFrames; m_testClock.restart(); m_testStage = 2; }
        break;
    case 2:  // idle baseline, then rotary + directional keys
        if (elapsed >= 1500) {
            m_testFrames[1] = m_displayedFrames;
            SaveTestShot("input-1-before.png");
            SaveTestShot("window.png", true);
            m_input->Rotate(+1); m_input->Rotate(+1); m_input->Rotate(-1);
            m_input->Tap(keys::DpadDown); m_input->Tap(keys::DpadUp);
            m_testClock.restart(); m_testStage = 3;
        }
        break;
    case 3:  // touch tap in the middle of the screen
        if (elapsed >= 2500) {
            m_testFrames[2] = m_displayedFrames;
            SaveTestShot("input-2-after-rotary.png");
            m_input->Touch(TouchAction::Down, kTestTouchX, kTestTouchY);
            m_testClock.restart(); m_testStage = 4;
        }
        break;
    case 4:
        if (elapsed >= 120) { m_input->Touch(TouchAction::Up, kTestTouchX, kTestTouchY); m_testClock.restart(); m_testStage = 5; }
        break;
    case 5:  // go back to the home screen
        if (elapsed >= 2500) { m_testFrames[3] = m_displayedFrames; SaveTestShot("input-3-after-touch.png"); m_input->Tap(keys::Home); m_testClock.restart(); m_testStage = 6; }
        break;
    case 6:
        if (elapsed >= 1200) {
            const unsigned idle = m_testFrames[1] - m_testFrames[0];
            const unsigned rotary = m_testFrames[2] - m_testFrames[1];
            const unsigned touch = m_testFrames[3] - m_testFrames[2];
            FinishTest(rotary + touch > 0 ? 0 : 4, "Input test: frames while idle=" + std::to_string(idle) + ", after rotary/keys=" +
                std::to_string(rotary) + ", after touch tap=" + std::to_string(touch));
        }
        break;
    default: break;
    }
}
// Starts playback on the phone and checks that PCM reaches the speaker output. Volume is set very low
// first, so a phone that starts music does not blast it.
void MainWindow::RunAudioTest()
{
    const qint64 elapsed = m_testClock.isValid() ? m_testClock.elapsed() : 0;
    constexpr std::uint64_t enoughBytes = 48000 * 2 * 2 * 3;  // three seconds of media audio
    switch (m_testStage) {
    case 0:
        if (m_displayedFrames >= 5 && m_input->IsAttached()) {
            m_audioState->SetMuted(false);
            m_audioState->SetVolume(5);
            m_input->Tap(keys::MediaPlay);
            m_testClock.restart(); m_testStage = 1;
        }
        break;
    case 1: {
        const auto media = m_audioState->BytesRendered(AudioKind::Media);
        const std::string counts = "media=" + std::to_string(media) + " (sent by phone: " + std::to_string(m_audioState->BytesPlayed(AudioKind::Media)) + ") guidance=" + std::to_string(m_audioState->BytesRendered(AudioKind::Guidance)) +
            " system=" + std::to_string(m_audioState->BytesRendered(AudioKind::System)) + " bytes played";
        if (media >= enoughBytes) {
            m_input->Tap(keys::MediaPause);
            FinishTest(0, "Audio test: PCM reached the speaker output (" + counts + ")");
        } else if (elapsed >= 25000) {
            m_input->Tap(keys::MediaPause);
            FinishTest(5, "Audio test: no media audio within 25 seconds (" + counts + "); the phone may have nothing to play");
        }
        break;
    }
    default: break;
    }
}
}
