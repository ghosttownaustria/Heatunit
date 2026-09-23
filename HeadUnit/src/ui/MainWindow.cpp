#include "ui/MainWindow.h"
#include "ui/CarWidgets.h"
#ifdef HEADUNIT_WIRELESS
#include "wireless/WirelessConnect.h"
#endif
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <array>
#include <charconv>
#include <cstdlib>
#include <exception>
#include <iterator>
#include <system_error>

namespace headunit {
namespace {
QString Text(const std::string& value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
// The touch step of the --test-keys script, "t:X:Y".
bool ParseTapStep(const std::string& step, int& x, int& y)
{
    const char* const end = step.data() + step.size();
    const auto first = std::from_chars(step.data() + 2, end, x);
    if (first.ec != std::errc{} || first.ptr == end || *first.ptr != ':') return false;
    return std::from_chars(first.ptr + 1, end, y).ec == std::errc{};
}
constexpr unsigned kProjectionTestFrames = 10;
// The chosen display size is remembered between runs, per user (QSettings: registry on Windows, ~/.config on Linux).
constexpr const char* kSettingsOrganization = "HeadUnit";
constexpr const char* kSettingsApplication = "HeadUnit";
constexpr const char* kDisplaySetting = "display";
QString DisplayChoiceText(const DisplayConfig& display)
{
    QString text = Text(DisplayText(display));
    if (display.height == 720) text += " (HD)";
    else if (display.height == 1080) text += " (Full HD)";
    else if (display.height == 600) text += " (Ultrawide)";
    return text;
}
}
MainWindow::MainWindow(IUsbBackend& backend, Logger& logger, TestMode mode)
    : m_backend(backend), m_logger(logger), m_mode(mode),
      m_input(std::make_shared<ProjectionInput>()), m_audioState(std::make_shared<AudioState>()),
      m_audio(CreateAudioEngine(m_audioState, logger))
{
    setWindowTitle("Android Auto Headunit");
    resize(1240, 800);
    auto* central = new QWidget(this);
    auto* root = new QHBoxLayout(central);
    auto* left = new QVBoxLayout();
    m_video = new VideoWidget(central);
    m_video->ClearFrame("Android Auto ist nicht verbunden.");
    m_video->onTouch = [this](TouchAction action, int x, int y) {
        if (action == TouchAction::Down) m_console.NoteTouch();
        m_input->Touch(action, x, y);
    };
    left->addWidget(m_video, 1);
    // The display size sits next to the button that connects: it is fixed once the connection starts.
    auto* controls = new QHBoxLayout();
    auto* displayLabel = new QLabel("Displaygroesse:", central);
    m_displayChoice = new QComboBox(central);
    m_displayChoice->setMinimumHeight(48);
    m_displayChoice->setMinimumWidth(190);
    m_displayChoice->setFocusPolicy(Qt::NoFocus);
    m_displayChoice->setToolTip("Groesse des Android-Auto-Bildes (Aufloesung). Das Handy erfaehrt sie beim Verbinden, "
        "darum laesst sie sich nur einstellen, solange keine Verbindung besteht.");
    for (const auto& display : kDisplays) m_displayChoice->addItem(DisplayChoiceText(display));
    displayLabel->setBuddy(m_displayChoice);
    m_button = new QPushButton(central);
    m_button->setMinimumHeight(48);
    m_button->setFocusPolicy(Qt::NoFocus);
    controls->addWidget(displayLabel);
    controls->addWidget(m_displayChoice);
    controls->addWidget(m_button, 1);
#ifdef HEADUNIT_WIRELESS
    m_wirelessButton = new QPushButton("Android Auto kabellos", central);
    m_wirelessButton->setMinimumHeight(48);
    m_wirelessButton->setFocusPolicy(Qt::NoFocus);
    m_wirelessButton->setToolTip("Ohne Kabel: HeadUnit schaltet Bluetooth ein und ist als HEATUNIT sichtbar; das Handy wird einmal gekoppelt. "
        "Das WLAN startet erst, wenn das Handy Android Auto aufbaut, und geht danach wieder aus.");
    controls->addWidget(m_wirelessButton, 1);
#endif
    left->addLayout(controls);
#ifdef HEADUNIT_WIRELESS
    m_step = new QLabel("USB: Handy per Kabel anschliessen, entsperren und auf Android Auto verbinden klicken. "
        "Kabellos: auf Android Auto kabellos klicken und das Handy per Bluetooth mit HEATUNIT koppeln.", central);
#else
    m_step = new QLabel("Handy per USB-Kabel anschliessen, entsperren und auf Android Auto verbinden klicken.", central);
#endif
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
    m_panel->onConsole = [this](ConsoleKey key) { PressConsole(key); };
    m_panel->onKey = [this](unsigned keycode, bool isDown) { m_console.NoteKey(keycode, isDown); m_input->Key(keycode, isDown); };
    m_panel->onRotate = [this](int detents) { m_input->Rotate(detents); };
    m_panel->onVolume = [this](int delta) { m_audioState->ChangeVolume(delta); };
    m_panel->onMute = [this] { m_audioState->ToggleMute(); };
    root->addWidget(m_panel);
    setCentralWidget(central);
    SetState(State::Idle);
    if (m_mode == TestMode::None || m_mode == TestMode::Smoke) {
        // The scripted runs against the phone always start from the default (or --display), whatever was chosen last.
        const QSettings settings(kSettingsOrganization, kSettingsApplication);
        if (const auto saved = ParseDisplay(settings.value(kDisplaySetting).toString().toStdString())) SetDisplay(*saved);
    }
    // `activated` only fires for a choice made by the user, not for SetDisplay.
    connect(m_displayChoice, &QComboBox::activated, this, [this](int index) {
        if (m_state != State::Idle || index < 0 || index >= static_cast<int>(std::size(kDisplays))) return;
        SetDisplay(kDisplays[index]);
        QSettings(kSettingsOrganization, kSettingsApplication).setValue(kDisplaySetting, Text(DisplayText(m_display)));
        const std::string message = "Displaygroesse " + DisplayText(m_display) + ": gilt ab der naechsten Verbindung";
        m_logger.Write("INFO", "UI", message);
        ShowStep(Text(message));
    });
    connect(m_button, &QPushButton::clicked, this, &MainWindow::OnButton);
    if (m_wirelessButton) connect(m_wirelessButton, &QPushButton::clicked, this, &MainWindow::StartWirelessConnect);
    auto* tickTimer = new QTimer(this);
    connect(tickTimer, &QTimer::timeout, this, &MainWindow::Tick);
    tickTimer->start(33);
    connect(&m_watcher, &QFutureWatcher<AutoConnectResult>::finished, this, [this] { FinishConnect(m_watcher.result()); });
    connect(&m_scanWatcher, &QFutureWatcher<UsbScanResult>::finished, this, [this] {
        const auto result = m_scanWatcher.result();
        SaveTestShot("window-idle.png", true);
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
    // Controller keys: the console decides what they mean (see ConsoleController).
    std::optional<ConsoleKey> console;
    switch (event->key()) {
    case Qt::Key_Home: console = ConsoleKey::Home; break;
    case Qt::Key_Escape: case Qt::Key_Backspace: console = ConsoleKey::Back; break;
    case Qt::Key_F1: console = ConsoleKey::Menu; break;
    case Qt::Key_F2: console = ConsoleKey::Option; break;
    case Qt::Key_F3: console = ConsoleKey::Media; break;
    case Qt::Key_F4: console = ConsoleKey::Radio; break;
    case Qt::Key_F5: console = ConsoleKey::Tel; break;
    case Qt::Key_F6: console = ConsoleKey::Nav; break;
    case Qt::Key_F7: console = ConsoleKey::Map; break;
    case Qt::Key_F8: console = ConsoleKey::Projection; break;
    default: break;
    }
    if (console) {
        if (isDown && !event->isAutoRepeat()) PressConsole(*console);
        return true;
    }
    // Keys that go straight to the phone.
    unsigned keycode = 0;
    bool isArrow = false;
    switch (event->key()) {
    case Qt::Key_Up: keycode = keys::DpadUp; isArrow = true; break;
    case Qt::Key_Down: keycode = keys::DpadDown; isArrow = true; break;
    case Qt::Key_Left: keycode = keys::DpadLeft; isArrow = true; break;
    case Qt::Key_Right: keycode = keys::DpadRight; isArrow = true; break;
    case Qt::Key_Return: case Qt::Key_Enter: keycode = keys::DpadCenter; break;
    case Qt::Key_Space: keycode = keys::MediaPlayPause; break;
    case Qt::Key_PageUp: keycode = keys::MediaPrevious; break;
    case Qt::Key_PageDown: keycode = keys::MediaNext; break;
    default: break;
    }
    if (keycode != 0) {
        if (event->isAutoRepeat()) { if (isDown && isArrow) m_input->Tap(keycode); }  // held arrows keep nudging
        else { m_console.NoteKey(keycode, isDown); m_input->Key(keycode, isDown); }
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
// What the phone shows right now, read from the last picture (unknown without one).
PhoneScreen MainWindow::CurrentPhoneScreen() const
{
    if (!m_video->HasFrame()) return PhoneScreen::Unknown;
    const QImage& image = m_video->Image();
    if (image.format() != QImage::Format_RGB888) return PhoneScreen::Unknown;
    return DetectPhoneScreen(image.constBits(), image.width(), image.height(), static_cast<int>(image.bytesPerLine()));
}
void MainWindow::PressConsole(ConsoleKey key)
{
    // Only Home depends on where the phone is.
    PhoneScreen phone = PhoneScreen::Unknown;
    if (key == ConsoleKey::Home && m_console.IsProjectionConnected()) {
        phone = CurrentPhoneScreen();
        m_logger.Write("INFO", "CONSOLE", std::string("Phone screen read from the picture: ") +
            (phone == PhoneScreen::Dashboard ? "dashboard" : phone == PhoneScreen::Other ? "other (app or launcher)" : "unknown"));
    }
    ApplyConsoleEffect(m_console.Press(key, phone));
}
// Keys and taps go to the phone, the message (if any) becomes a line in the window log, and the
// projection key may start the connection.
void MainWindow::ApplyConsoleEffect(const ConsoleEffect& effect)
{
    for (const unsigned keycode : effect.phoneKeys) m_input->Tap(keycode);
    for (const auto& [x, y] : effect.phoneTaps) TapPhone(x, y);
    if (effect.retryDashboard) {
        // The home key opened the app launcher, whose button leads to the dashboard: look again once the
        // phone has drawn it.
        QTimer::singleShot(1000, this, [this] {
            if (!m_console.IsProjectionConnected() || CurrentPhoneScreen() != PhoneScreen::Other) return;
            const auto [x, y] = DashboardButtonPosition(m_display);
            TapPhone(x, y);
        });
    }
    if (!effect.message.empty()) {
        m_logger.Write("INFO", "CONSOLE", effect.message);
        ShowStep(Text(effect.message));
    }
    if (effect.connect) StartConnect();
}
// A short touch at a fixed spot of the phone's screen.
void MainWindow::TapPhone(int x, int y)
{
    m_input->Touch(TouchAction::Down, x, y);
    QTimer::singleShot(60, this, [this, x, y] { m_input->Touch(TouchAction::Up, x, y); });
}
void MainWindow::keyPressEvent(QKeyEvent* event) { if (!HandleKey(event, true)) QMainWindow::keyPressEvent(event); }
void MainWindow::keyReleaseEvent(QKeyEvent* event) { if (!HandleKey(event, false)) QMainWindow::keyReleaseEvent(event); }
void MainWindow::SetState(State state)
{
    m_state = state;
    m_button->setEnabled(state != State::Stopping);
    m_button->setText(state == State::Idle ? "Android Auto verbinden" : state == State::Connecting ? "Verbindung beenden" : "Beende ...");
    if (m_wirelessButton) m_wirelessButton->setEnabled(state == State::Idle);
    // The phone learns the display size when the connection starts; afterwards it can no longer change.
    m_displayChoice->setEnabled(state == State::Idle);
}
void MainWindow::SetDisplay(const DisplayConfig& display)
{
    if (m_state != State::Idle || !IsSupportedDisplay(display)) return;
    m_display = display;
    m_console.SetDisplay(display);
    m_video->SetDisplay(display);
    for (int index = 0; index < static_cast<int>(std::size(kDisplays)); ++index)
        if (kDisplays[index] == display) m_displayChoice->setCurrentIndex(index);
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
        if (++m_displayedFrames == 1) {
            m_logger.Write("INFO", "VIDEO", "First real Android Auto frame displayed in Qt");
            m_console.SetProjectionConnected(true);
        }
        // A display of another shape than the phone's frame shows only part of it: name both.
        const QString shownArea = VideoLayoutOf(m_display).HasMargins() ? ", Anzeige " + Text(DisplayText(m_display)) : QString();
        m_status->setText(QString("Android Auto: Video %1x%2%3 | %4 Bilder angezeigt").arg(frame->width).arg(frame->height).arg(shownArea).arg(m_displayedFrames));
        if (m_mode == TestMode::Projection && m_displayedFrames >= kProjectionTestFrames) { m_isStopRequested = true; QApplication::exit(0); }
    }
    std::array<AudioState::Meter, kAudioKindCount> meters;
    for (int i = 0; i < kAudioKindCount; ++i) meters[i] = m_audioState->ReadMeter(static_cast<AudioKind>(i));
    m_panel->Display()->SetState(m_audioState->Volume(), m_audioState->IsMuted(), meters);
    if (m_state == State::Connecting && !m_isTestFinished) {
        if (m_mode == TestMode::Input) RunInputTest();
        else if (m_mode == TestMode::Audio) RunAudioTest();
        else if (m_mode == TestMode::Console) RunConsoleTest();
        else if (m_mode == TestMode::Keys) RunKeysTest();
    }
}
void MainWindow::StartConnect() { BeginConnect(false); }
void MainWindow::StartWirelessConnect()
{
#ifdef HEADUNIT_WIRELESS
    BeginConnect(true);
#endif
}
void MainWindow::BeginConnect(bool isWireless)
{
    if (m_state != State::Idle) return;
    m_isStopRequested = false;
    m_displayedFrames = 0;
    { std::lock_guard lock(m_frameMutex); m_latestFrame.reset(); }
    m_console.SetProjectionConnected(false);
    m_video->ClearFrame("Verbinde Android Auto ...");
    m_status->clear();
    m_history->clear();
    SetState(State::Connecting);
    m_logger.Write("INFO", "UI", "Display size for this connection: " + DisplayText(m_display));
    m_watcher.setFuture(QtConcurrent::run([this, display = m_display, isWireless] {
        ProjectionCallbacks callbacks;
        callbacks.display = display;
        callbacks.onStatus = [this](const std::string& status) { QMetaObject::invokeMethod(this, [this, status] { ShowStep(Text(status)); }, Qt::QueuedConnection); };
        callbacks.onFrame = [this](VideoFrame frame) { std::lock_guard lock(m_frameMutex); m_latestFrame = std::move(frame); };
        callbacks.input = m_input;
        callbacks.openAudio = m_audio->Opener();
#ifdef HEADUNIT_WIRELESS
        if (isWireless) return ConnectWirelessAndroidAuto(m_logger, m_isStopRequested, std::move(callbacks));
#else
        (void)isWireless;
#endif
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
    m_console.SetProjectionConnected(false);
    SetState(State::Idle);
    if (m_mode == TestMode::Projection) { QApplication::exit(m_displayedFrames >= kProjectionTestFrames ? 0 : 3); return; }
    if (m_mode == TestMode::Input || m_mode == TestMode::Audio || m_mode == TestMode::Console || m_mode == TestMode::Keys) { QApplication::exit(m_isTestFinished ? m_testExitCode : 3); return; }
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
            m_input->Touch(TouchAction::Down, VideoLayoutOf(m_display).width / 2, VideoLayoutOf(m_display).height / 2);
            m_testClock.restart(); m_testStage = 4;
        }
        break;
    case 4:
        if (elapsed >= 120) { m_input->Touch(TouchAction::Up, VideoLayoutOf(m_display).width / 2, VideoLayoutOf(m_display).height / 2); m_testClock.restart(); m_testStage = 5; }
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
        } else if (media == 0 && elapsed >= 5000 * static_cast<qint64>(m_testFrames[0] + 1)) {
            // The first Play can arrive before the phone's media app is ready for it (or find it paused
            // by the last run): ask again every 5 seconds. Play while already playing does nothing.
            // (m_testFrames[0] counts the repeats here; the audio test has no use for frame counts.)
            ++m_testFrames[0];
            m_input->Tap(keys::MediaPlay);
        }
        break;
    }
    default: break;
    }
}
// Presses controller keys on the real phone and checks the Home logic step by step, looking at what the
// phone really shows (not just at the controller's own state): Media opens the media app, Home brings the
// phone to its dashboard, Home again reports the radio menu (a log line) and leaves the phone alone, Media
// leaves the dashboard again, Home returns to it. Nav comes last and only has to leave the radio menu: on a
// wide display the phone shows the map in its dashboard, on a narrow one as an app, so the picture is saved
// and read but not judged. Pictures of the phone are saved for a person to look at.
void MainWindow::RunConsoleTest()
{
    const qint64 elapsed = m_testClock.isValid() ? m_testClock.elapsed() : 0;
    const auto expectScreen = [&](ConsoleController::Screen screen, const char* what) {
        if (m_console.CurrentScreen() != screen) m_testProblems += std::string(what) + "; ";
    };
    const auto expectPhone = [&](PhoneScreen phone, const char* what) {
        if (CurrentPhoneScreen() != phone) m_testProblems += std::string(what) + "; ";
    };
    const auto historyHas = [&](const char* text) { return m_history->toPlainText().contains(QString::fromUtf8(text)); };
    switch (m_testStage) {
    case 0:
        if (m_displayedFrames >= kProjectionTestFrames && m_input->IsAttached()) {
            PressConsole(ConsoleKey::Media);
            m_testClock.restart(); m_testStage = 1;
        }
        break;
    case 1:  // the media app is showing; Home must bring the phone to its dashboard
        if (elapsed >= 3000) {
            SaveTestShot("console-1-media.png");
            expectPhone(PhoneScreen::Other, "the phone did not show an app after Media");
            PressConsole(ConsoleKey::Home);
            m_testClock.restart(); m_testStage = 2;
        }
        break;
    case 2:  // first Home: the phone shows its dashboard, no radio menu yet
        if (elapsed >= 2500) {
            SaveTestShot("console-2-home.png");
            expectPhone(PhoneScreen::Dashboard, "first Home did not bring the phone to its dashboard");
            expectScreen(ConsoleController::Screen::ProjectionHome, "first Home did not end on the phone's dashboard");
            if (historyHas("Radio-Startmenue")) m_testProblems += "first Home already opened the radio menu; ";
            if (!historyHas("Android-Auto-Startbildschirm")) m_testProblems += "first Home was not reported; ";
            PressConsole(ConsoleKey::Home);
            m_testClock.restart(); m_testStage = 3;
        }
        break;
    case 3:  // second Home: the radio menu, only a log line; the phone must stay where it is
        if (elapsed >= 800) {
            SaveTestShot("console-3-second-home.png");
            expectScreen(ConsoleController::Screen::RadioHome, "second Home did not open the radio menu");
            expectPhone(PhoneScreen::Dashboard, "second Home changed what the phone shows");
            if (!historyHas("Home: Radio-Startmenue")) m_testProblems += "radio menu line missing in the window log; ";
            PressConsole(ConsoleKey::Media);
            m_testClock.restart(); m_testStage = 4;
        }
        break;
    case 4:  // Media launches something on the phone, so Home must go to the phone first again
        if (elapsed >= 3000) {
            SaveTestShot("console-4-media-again.png");
            expectScreen(ConsoleController::Screen::Projection, "Media did not leave the dashboard");
            expectPhone(PhoneScreen::Other, "the phone did not show an app after Media");
            PressConsole(ConsoleKey::Home);
            m_testClock.restart(); m_testStage = 5;
        }
        break;
    case 5:
        if (elapsed >= 2500) {
            SaveTestShot("console-5-home-again.png");
            expectScreen(ConsoleController::Screen::ProjectionHome, "Home after Media did not end on the phone's dashboard");
            expectPhone(PhoneScreen::Dashboard, "Home after Media did not bring the phone to its dashboard");
            PressConsole(ConsoleKey::Radio);   // only reported
            if (!historyHas("Radio: noch keine Belegung")) m_testProblems += "radio key was not reported; ";
            expectScreen(ConsoleController::Screen::RadioHome, "the radio key did not open the radio menu");
            PressConsole(ConsoleKey::Nav);
            m_testClock.restart(); m_testStage = 6;
        }
        break;
    case 6:  // Nav leaves the radio menu for the phone; where the phone puts the map depends on the display
        if (elapsed >= 3000) {
            SaveTestShot("console-6-nav.png");
            expectScreen(ConsoleController::Screen::Projection, "Nav did not leave the radio menu");
            const PhoneScreen phone = CurrentPhoneScreen();
            m_logger.Write("INFO", "TEST", std::string("After Nav the phone shows ") + (phone == PhoneScreen::Dashboard ? "its dashboard (map card)" :
                phone == PhoneScreen::Other ? "an app (map)" : "an unknown screen"));
            if (phone == PhoneScreen::Unknown) m_testProblems += "the phone's picture could not be read after Nav; ";
            FinishTest(m_testProblems.empty() ? 0 : 6, m_testProblems.empty() ? "Console test: Home, Media, Radio and Nav behave as specified"
                : "Console test: " + m_testProblems);
        }
        break;
    default: break;
    }
}
// Diagnostic: plays the script in HEADUNIT_TEST_KEYS on the real phone and saves a picture after every
// step. Steps are separated by commas: a number taps that key code, "t:X:Y" taps the touchscreen at X,Y
// (0..799, 0..479), "c:name" presses a controller key (home, media, ...). Used to find out what a key does
// on a given phone.
void MainWindow::RunKeysTest()
{
    const qint64 elapsed = m_testClock.isValid() ? m_testClock.elapsed() : 0;
    if (m_testStage == 0) {
        if (m_displayedFrames < kProjectionTestFrames || !m_input->IsAttached()) return;
        const QByteArray script = qgetenv("HEADUNIT_TEST_KEYS");
        for (const auto& part : script.split(',')) if (!part.trimmed().isEmpty()) m_testSteps.push_back(part.trimmed().toStdString());
        m_testStage = 1;
        m_testClock.restart();
        SaveTestShot("keys-0-start.png");
        return;
    }
    const std::size_t index = static_cast<std::size_t>(m_testStage - 1);
    if (elapsed < 2800) return;
    if (index > 0) SaveTestShot(("keys-" + std::to_string(index) + ".png").c_str());
    if (index >= m_testSteps.size()) { FinishTest(0, "Keys test: " + std::to_string(m_testSteps.size()) + " steps played"); return; }
    const std::string& step = m_testSteps[index];
    m_logger.Write("INFO", "TEST", "Keys test step " + std::to_string(index + 1) + ": " + step);
    if (step.rfind("t:", 0) == 0) {
        int x = 0, y = 0;
        if (ParseTapStep(step, x, y)) {
            m_input->Touch(TouchAction::Down, x, y);
            m_input->Touch(TouchAction::Up, x, y);
        }
    } else if (step.rfind("c:", 0) == 0) {
        static const std::pair<const char*, ConsoleKey> names[] = {{"home", ConsoleKey::Home}, {"menu", ConsoleKey::Menu}, {"option", ConsoleKey::Option},
            {"media", ConsoleKey::Media}, {"radio", ConsoleKey::Radio}, {"tel", ConsoleKey::Tel}, {"nav", ConsoleKey::Nav}, {"map", ConsoleKey::Map},
            {"back", ConsoleKey::Back}, {"projection", ConsoleKey::Projection}};
        for (const auto& [name, key] : names) if (step.substr(2) == name) PressConsole(key);
    } else {
        m_input->Tap(static_cast<unsigned>(std::strtoul(step.c_str(), nullptr, 10)));
    }
    ++m_testStage;
    m_testClock.restart();
}
}
