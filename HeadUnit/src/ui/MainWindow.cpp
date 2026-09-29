#include "ui/MainWindow.h"
#include "remote/RemoteServer.h"
#include "androidauto/PhoneWatch.h"
#include "androidauto/ProjectionKeys.h"
#include "audio/AudioClock.h"
#include "audio/WatchedOutput.h"
#include "platform/Environment.h"
#include "platform/BuildProfile.h"
#include "ui/BluetoothPage.h"
#include "ui/CarPanel.h"
#include "ui/HomeMenu.h"
#include "ui/MultimediaPage.h"
#include "ui/PairingPage.h"
#include "ui/RadioPage.h"
#include "ui/SettingsPage.h"
#include "ui/VideoWidget.h"
#include "ui/VolumeOverlay.h"
#include "usb/LibusbUsbBackend.h"
#ifdef HEADUNIT_WIRELESS
#include "wireless/WirelessStation.h"
#endif
#include <QApplication>
#include <QCloseEvent>
#include <QLineEdit>
#include <QDir>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QScreen>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <array>
#include <exception>
#include <iterator>
#include <utility>

namespace headunit {
namespace {
// Remembered between runs, per user (QSettings: registry on Windows, ~/.config on Linux).
constexpr const char* kDisplaySetting = "display";
constexpr const char* kTilesSetting = "home/tiles";
constexpr int kTickIntervalMs = 33;
// Scripted runs must not blast the phone's music: they start quiet.
constexpr int kTestVolume = 5;

// The folder the music player plays from: HEADUNIT_MUSIC_DIR, else "HeadUnit" in the user's music folder.
QString MusicFolder()
{
    if (const auto folder = GetEnv("HEADUNIT_MUSIC_DIR"); folder && !folder->empty()) return QString::fromStdString(*folder);
    return QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + "/HeadUnit";
}

// The controller's own keys: they work the radio's page in front (the media keys do not).
bool IsControllerKey(unsigned keycode)
{
    return keycode == keys::DpadLeft || keycode == keys::DpadRight || keycode == keys::DpadUp || keycode == keys::DpadDown || keycode == keys::DpadCenter;
}

// The controller key a keyboard key stands for.
std::optional<ConsoleKey> ConsoleKeyFor(int key)
{
    switch (key) {
    case Qt::Key_Home: return ConsoleKey::Home;
    case Qt::Key_Escape:
    case Qt::Key_Backspace: return ConsoleKey::Back;
    case Qt::Key_F1: return ConsoleKey::Menu;
    case Qt::Key_F2: return ConsoleKey::Option;
    case Qt::Key_F3: return ConsoleKey::Media;
    case Qt::Key_F4: return ConsoleKey::Radio;
    case Qt::Key_F5: return ConsoleKey::Tel;
    case Qt::Key_F6: return ConsoleKey::Nav;
    case Qt::Key_F7: return ConsoleKey::Map;
    case Qt::Key_F8: return ConsoleKey::Projection;
    default: return std::nullopt;
    }
}

// The phone key a keyboard key stands for (0 for none).
unsigned PhoneKeyFor(int key)
{
    switch (key) {
    case Qt::Key_Up: return keys::DpadUp;
    case Qt::Key_Down: return keys::DpadDown;
    case Qt::Key_Left: return keys::DpadLeft;
    case Qt::Key_Right: return keys::DpadRight;
    case Qt::Key_Return:
    case Qt::Key_Enter: return keys::DpadCenter;
    case Qt::Key_Space: return keys::MediaPlayPause;
    case Qt::Key_PageUp: return keys::MediaPrevious;
    case Qt::Key_PageDown: return keys::MediaNext;
    default: return 0;
    }
}

// The scripted test a test mode runs, if any.
std::optional<ScriptedPhoneTest::Kind> ScriptedTestOf(MainWindow::TestMode mode)
{
    switch (mode) {
    case MainWindow::TestMode::Input: return ScriptedPhoneTest::Kind::Input;
    case MainWindow::TestMode::Audio: return ScriptedPhoneTest::Kind::Audio;
    case MainWindow::TestMode::Console: return ScriptedPhoneTest::Kind::Console;
    case MainWindow::TestMode::Keys: return ScriptedPhoneTest::Kind::Keys;
    default: return std::nullopt;
    }
}
}

// Builds the window (the picture and the radio's pages, the controls under them, the console at the right) and starts
// the mode: the automatic mode, a scripted test, or the smoke test.
MainWindow::MainWindow(IUsbBackend& backend, Logger& logger, TestMode mode)
    : m_backend(backend), m_logger(logger), m_mode(mode), m_input(std::make_shared<ProjectionInput>()), m_audioState(std::make_shared<AudioState>()),
      m_audio(m_audioState, logger), m_player(m_audio.Opener(), logger)
{
    setWindowTitle("Android Auto Headunit");
    resize(1240, 800);
    auto* central = new QWidget(this);
    auto* root = new QHBoxLayout(central);
    auto* left = new QVBoxLayout();
    left->addWidget(BuildScreens(central), 1);
    BuildControls(central, left);
    root->addLayout(left, 1);
    BuildPanel(central, root);
    setCentralWidget(central);
    SetTiles(ParseTileSetup(QSettings().value(kTilesSetting).toString().toStdString()));
    SetState(State::Idle);
    ShowScreen();
    if (m_mode == TestMode::None || m_mode == TestMode::Smoke) {
        // The scripted runs against the phone always start from the default (or --display), whatever was chosen last.
        if (const auto saved = ParseDisplay(QSettings().value(kDisplaySetting).toString().toStdString())) SetDisplay(*saved);
    }
    // ScriptedTestHost is a private base, so the conversion has to happen here and not inside make_unique.
    if (const auto kind = ScriptedTestOf(m_mode)) m_scriptedTest = std::make_unique<ScriptedPhoneTest>(*kind, static_cast<ScriptedTestHost&>(*this));
    ConnectSignals();
    StartMode();
}

// Ends the automatic mode and waits for its worker: the backend and the logger stay alive until the worker has released
// the USB interface.
MainWindow::~MainWindow()
{
    m_isWatchStopRequested = true;
    m_isStopRequested = true;
    m_watcher.waitForFinished();
    m_scanWatcher.waitForFinished();
}

// Selects the display size for the next connection (ignored while a session is running or for a size outside the accepted
// range). Not remembered: only a size typed into the window is.
void MainWindow::SetDisplay(const DisplayConfig& display)
{
    if (m_state == State::Connecting || m_state == State::Stopping || !IsValidDisplay(display)) return;
    {
        std::lock_guard lock(m_displayMutex);
        m_display = display;
    }
    m_console.SetDisplay(display);
    m_video->SetDisplay(display);
    for (MenuPage* page : MenuPages()) page->SetDisplay(display);
    m_volumeBar->SetDisplay(display);
    m_displayInput->setText(QString::fromStdString(DisplayText(display)));
}

// The car's window: the picture and the radio's pages over the whole screen, shown for the size of that screen, without
// the simulated console, the display choice, the button and the history. It has no frame, and the window manager's own
// requests to close it (Alt+F4, a task switcher) are refused: the way out is the Quit button in the settings. Called
// before the window is shown; a display given on the command line still overrides the size found here.
void MainWindow::EnterKioskMode()
{
    m_isKiosk = true;
    m_panel->hide();
    m_controls->hide();
    QLayout* root = centralWidget()->layout();
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    if (QLayout* left = root->itemAt(0)->layout()) {
        left->setContentsMargins(0, 0, 0, 0);
        left->setSpacing(0);
    }
    setWindowFlag(Qt::FramelessWindowHint);
    setCursor(Qt::BlankCursor);
    const QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    const QSize pixels = screen->geometry().size() * screen->devicePixelRatio();
    SetDisplay(DisplayForScreen(pixels.width(), pixels.height()));
    m_logger.Write(LogLevel::Info, "UI", "Car mode: full screen " + std::to_string(pixels.width()) + "x" + std::to_string(pixels.height()) +
        ", display " + DisplayText(m_display));
}

// Lets other programs work everything the simulated console offers, over TCP on 127.0.0.1 (docs/api.md): the console
// keys, the knob, the media keys and the volume run the same actions as the panel's buttons, also when the panel is hidden.
void MainWindow::StartRemoteApi(int port)
{
    RemoteCommandDeps deps;
    deps.pressConsole = [this](ConsoleKey key) { PressConsole(key); };
    deps.sendKey = [this](unsigned keycode, bool isDown) { SendKey(keycode, isDown); };
    deps.rotate = [this](int detents) { Rotate(detents); };
    deps.changeVolume = [this](int delta) { m_audioState->ChangeVolume(delta); };
    deps.toggleMute = [this] { m_audioState->ToggleMute(); };
    deps.readStatus = [this] {
        return RemoteStatus{m_audioState->Volume(), m_audioState->IsMuted(), RemotePageName(m_console.CurrentScreen()), m_console.IsProjectionConnected()};
    };
    m_remote = new RemoteServer(std::move(deps), m_logger, this);
    m_remote->Listen(port);
}

// Closing first lets a session say goodbye to the phone and release the USB interface, and takes the hotspot and
// Bluetooth down; FinishConnect closes the window once that has happened. The watch's flag goes first (see PhoneWatch).
void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_isKiosk && event->spontaneous()) {
        m_logger.Write(LogLevel::Info, "UI", "Car mode: a close request of the window manager was refused");
        event->ignore();
        return;
    }
    if (m_state == State::Idle) {
        QMainWindow::closeEvent(event);
        return;
    }
    m_isCloseRequested = true;
    m_isWatchStopRequested = true;
    m_isStopRequested = true;
    if (m_state != State::Stopping) {
        SetState(State::Stopping);
        ShowStep("Beende ...");
    }
    event->ignore();
}

// A key goes down: controller, phone and extra keys are the window's.
void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (!HandleKey(event, true)) QMainWindow::keyPressEvent(event);
}

// A key goes up.
void MainWindow::keyReleaseEvent(QKeyEvent* event)
{
    if (!HandleKey(event, false)) QMainWindow::keyReleaseEvent(event);
}

// The phone's picture and the radio's pages share one place; ShowScreen picks the one in front. A phone that pairs over
// Bluetooth asks there too, in front of everything else, until it is answered. The volume bar shows over all of them
// when the volume changes; a touch on it sets the volume (and ends a mute).
QWidget* MainWindow::BuildScreens(QWidget* parent)
{
    m_screens = new QStackedWidget(parent);
    m_video = new VideoWidget(m_screens);
    m_video->ClearFrame("Android Auto ist nicht verbunden.");
    m_video->SetTouchHandler([this](TouchAction action, int x, int y) {
        if (action == TouchAction::Down) m_console.NoteTouch();
        m_input->Touch(action, x, y);
    });
    m_homeMenu = new HomeMenu(m_screens);
    m_homeMenu->SetOpenHandler([this](HomeMenuEntry entry) { OpenMenuEntry(entry); });
    // Left and right on a tile move it along the row; hidden tiles are passed over.
    m_homeMenu->SetShiftHandler([this](HomeMenuEntry entry, int direction) {
        HomeTileSetup setup = m_tiles;
        if (MoveTile(setup, entry, direction, true)) SetTiles(setup);
    });
    m_settings = new SettingsPage(m_screens);
    m_settings->SetChangeHandler([this](const HomeTileSetup& setup) { SetTiles(setup); });
    m_settings->SetQuitHandler([this] { Quit(); });
    m_bluetoothPage = new BluetoothPage(m_screens);
#ifdef HEADUNIT_WIRELESS
    m_bluetoothPage->SetAvailable(true);
#endif
    m_bluetoothPage->SetSwitchHandler([this](const BluetoothPhone& phone) { SwitchToPhone(phone); });
    m_pairing = new PairingPage(m_screens);
    m_pairing->SetChangeHandler([this] { ShowScreen(); });
    m_music = new MultimediaPage(m_player, MusicFolder(), m_screens);
    m_radio = new RadioPage(m_player, m_screens);
    for (PlayerPage* page : {static_cast<PlayerPage*>(m_music), static_cast<PlayerPage*>(m_radio)}) page->SetWillPlayHandler([this] { PausePhoneMedia(); });
    for (QWidget* screen : {static_cast<QWidget*>(m_video), static_cast<QWidget*>(m_homeMenu), static_cast<QWidget*>(m_music),
             static_cast<QWidget*>(m_radio), static_cast<QWidget*>(m_settings), static_cast<QWidget*>(m_bluetoothPage),
             static_cast<QWidget*>(m_pairing)})
        m_screens->addWidget(screen);
    for (MenuPage* page : MenuPages()) page->SetStatusHandler([this](StatusButton button) { PressStatus(button); });
    m_volumeBar = new VolumeOverlay(m_screens);
    m_volumeBar->SetVolumeHandler([this](int volume) {
        m_audioState->SetMuted(false);
        m_audioState->SetVolume(volume);
    });
    m_shownVolume = m_audioState->Volume();
    m_wasMuted = m_audioState->IsMuted();
    return m_screens;
}

// The display size (typed in as pixels) next to the button that quits (the size is fixed once the connection starts), the
// current step, the status line and the history of steps.
void MainWindow::BuildControls(QWidget* parent, QVBoxLayout* layout)
{
    m_controls = new QWidget(parent);
    auto* box = new QVBoxLayout(m_controls);
    box->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_controls);
    auto* controls = new QHBoxLayout();
    auto* displayLabel = new QLabel("Displaygroesse:", parent);
    m_displayInput = new QLineEdit(parent);
    m_displayInput->setMinimumHeight(48);
    m_displayInput->setMinimumWidth(190);
    m_displayInput->setPlaceholderText("Breite x Hoehe, z. B. 1024x600");
    m_displayInput->setToolTip("Groesse des Android-Auto-Bildes in Pixeln (Aufloesung des Displays), mit Enter uebernehmen. "
        "Das Handy erfaehrt sie beim Verbinden, darum laesst sie sich nur einstellen, solange keine Verbindung besteht.");
    displayLabel->setBuddy(m_displayInput);
    m_button = new QPushButton(parent);
    m_button->setMinimumHeight(48);
    m_button->setFocusPolicy(Qt::NoFocus);
#ifdef HEADUNIT_WIRELESS
    m_button->setToolTip("Trennt das Handy, schaltet Bluetooth und WLAN aus und beendet HeadUnit.");
#else
    m_button->setToolTip("Trennt das Handy und beendet HeadUnit.");
#endif
    controls->addWidget(displayLabel);
    controls->addWidget(m_displayInput);
    controls->addWidget(m_button, 1);
    box->addLayout(controls);
#ifdef HEADUNIT_WIRELESS
    m_step = new QLabel("Android Auto startet von selbst: Handy per USB-Kabel anstecken und entsperren, oder kabellos "
        "(einmal in den Bluetooth-Einstellungen des Handys mit HEATUNIT koppeln).", parent);
#else
    m_step = new QLabel("Android Auto startet von selbst, sobald ein Handy per USB-Kabel angesteckt wird (Handy entsperren).", parent);
#endif
    m_step->setWordWrap(true);
    m_step->setTextFormat(Qt::PlainText);
    m_step->setTextInteractionFlags(Qt::TextSelectableByMouse);
    box->addWidget(m_step);
    m_status = new QLabel(parent);
    box->addWidget(m_status);
    m_history = new QPlainTextEdit(parent);
    m_history->setReadOnly(true);
    m_history->setMaximumBlockCount(500);
    m_history->setMaximumHeight(120);
    m_history->setFocusPolicy(Qt::NoFocus);
    box->addWidget(m_history);
}

// The centre console at the right: its keys go through the console or straight to the phone, its volume keys to the
// car's audio.
void MainWindow::BuildPanel(QWidget* parent, QHBoxLayout* layout)
{
    m_panel = new CarPanel(parent);
    m_panel->setFixedWidth(310);
    m_panel->SetConsoleHandler([this](ConsoleKey key) { PressConsole(key); });
    m_panel->SetKeyHandler([this](unsigned keycode, bool isDown) { SendKey(keycode, isDown); });
    m_panel->SetRotateHandler([this](int detents) { Rotate(detents); });
    m_panel->SetVolumeHandler([this](int delta) { m_audioState->ChangeVolume(delta); });
    m_panel->SetMuteHandler([this] { m_audioState->ToggleMute(); });
    layout->addWidget(m_panel);
}

// The display input (only a size typed by the user is remembered: `editingFinished` does not fire for SetDisplay), the
// button, the tick and the ends of the workers.
void MainWindow::ConnectSignals()
{
    connect(m_displayInput, &QLineEdit::editingFinished, this, &MainWindow::ApplyTypedDisplay);
    connect(m_button, &QPushButton::clicked, this, &MainWindow::Quit);
    auto* tickTimer = new QTimer(this);
    connect(tickTimer, &QTimer::timeout, this, &MainWindow::Tick);
    tickTimer->start(kTickIntervalMs);
    connect(&m_watcher, &QFutureWatcher<AutoConnectResult>::finished, this, [this] { FinishConnect(m_watcher.result()); });
    connect(&m_scanWatcher, &QFutureWatcher<UsbScanResult>::finished, this, [this] {
        const auto result = m_scanWatcher.result();
        SaveTestShot("window-idle.png", true);
        QTimer::singleShot(250, this, [errors = result.errors.size()] { QApplication::exit(errors == 0 ? 0 : 2); });
    });
}

// Takes the size typed into the display input for the next connection and remembers it. Text that is not a size in range
// is put back to the size in use.
void MainWindow::ApplyTypedDisplay()
{
    if (m_state == State::Connecting || m_state == State::Stopping) return;
    const auto typed = ParseDisplay(m_displayInput->text().toStdString());
    if (!typed) {
        m_displayInput->setText(QString::fromStdString(DisplayText(m_display)));
        ShowStep(QString("Displaygroesse: Breite x Hoehe in Pixeln, von %1 bis %2").arg(QString::fromStdString(DisplayText(kMinDisplay)),
            QString::fromStdString(DisplayText(kMaxDisplay))));
        return;
    }
    if (*typed == m_display) return;
    SetDisplay(*typed);
    QSettings().setValue(kDisplaySetting, QString::fromStdString(DisplayText(m_display)));
    const std::string message = "Displaygroesse " + DisplayText(m_display) + ": gilt ab der naechsten Verbindung";
    m_logger.Write(LogLevel::Info, "UI", message);
    ShowStep(QString::fromStdString(message));
}

// The smoke test is the real window plus one real USB scan, then exit (for the window picture, HEADUNIT_TEST_PAIRING
// shows the pairing question of a made-up phone, HEADUNIT_TEST_VOLUME the volume bar, and HEADUNIT_TEST_PAGE=<tile>
// opens that tile's page, the Bluetooth page with made-up phones). Everything else starts connecting at once, after --display has been
// applied; nobody has to press anything.
void MainWindow::StartMode()
{
    if (m_mode == TestMode::Smoke) {
        if (qEnvironmentVariableIsSet("HEADUNIT_TEST_PAIRING")) {
            m_pairing->Ask("Galaxy Z Flip5", "123456", [this](bool isAccepted) {
                m_logger.Write(LogLevel::Info, "UI", std::string("Test pairing answered: ") + (isAccepted ? "pair" : "cancel"));
            });
        }
        if (qEnvironmentVariableIsSet("HEADUNIT_TEST_VOLUME")) m_volumeBar->ShowVolume(m_audioState->Volume(), m_audioState->IsMuted());
        const std::string testPage = qEnvironmentVariable("HEADUNIT_TEST_PAGE").toStdString();
        for (const HomeMenuEntry entry : kHomeMenuEntries) {
            if (const auto page = HomeMenuPage(entry); page && testPage == HomeMenuId(entry)) ApplyConsoleEffect(m_console.Open(*page));
        }
        if (testPage == HomeMenuId(HomeMenuEntry::Bluetooth)) {
            m_bluetoothPage->SetAvailable(true);
            m_bluetoothPage->SetPhones({{"/test/1", "Galaxy S24", true, false}, {"/test/2", "Jakob's Flip 8", true, true}, {"/test/3", "Pixel 8", false, false}});
        }
        m_scanWatcher.setFuture(QtConcurrent::run([this] {
            try {
                return m_backend.EnumerateDevices();
            } catch (const std::exception& error) {
                UsbScanResult result;
                result.errors.push_back(error.what());
                return result;
            }
        }));
        return;
    }
    if (m_mode != TestMode::None) m_audioState->SetVolume(kTestVolume);
    QTimer::singleShot(0, this, &MainWindow::StartConnect);
}

// Shows the state on the button; the display size can only change while no connection runs.
void MainWindow::SetState(State state)
{
    m_state = state;
    m_button->setEnabled(state != State::Stopping);
    m_button->setText(state == State::Stopping ? "Beende ..." : "Beenden");
    m_displayInput->setEnabled(state == State::Idle || state == State::Watching);
}

// The button (and the one in the settings) quits like switching the car off: the phone gets its goodbye, Bluetooth and the Wi-Fi are switched off
// (WatchPhones does that once the session is over) and the window closes (see closeEvent).
void MainWindow::Quit()
{
    m_logger.Write(LogLevel::Info, "UI", "Quit: ending the connection, switching Bluetooth and the Wi-Fi off, closing");
    m_isRadioOffRequested = true;
    close();
}

// The Android Auto tile and the projection key. Normally the automatic mode runs already; then this connects
// the phone on the USB cable once more (after a session, the phone stays plugged in and is not connected again by
// itself). The scripted test modes connect once over USB and report the result.
void MainWindow::StartConnect()
{
    if (m_mode != TestMode::None) {
        BeginConnect();
        return;
    }
    if (m_state == State::Idle) {
        BeginWatch();
        return;
    }
    if (m_state != State::Watching) return;
    m_isUsbRequested = true;
#ifdef HEADUNIT_WIRELESS
    ShowStep("Suche ein Handy am USB-Kabel, sonst kabellos ...");
#else
    ShowStep("Suche ein Handy am USB-Kabel ...");
#endif
}

// One connection over USB on a worker (the test modes).
void MainWindow::BeginConnect()
{
    if (m_state != State::Idle) return;
    m_isStopRequested = false;
    OnAttemptStart();
    m_history->clear();
    SetState(State::Connecting);
    m_watcher.setFuture(QtConcurrent::run([this] { return ConnectPhoneAutomatically(m_backend, m_logger, m_isStopRequested, MakeCallbacks()); }));
}

// The automatic mode: a worker that watches USB and wireless until the window closes. A future that ends in an
// exception would take the window down with it, so a failure becomes a result.
void MainWindow::BeginWatch()
{
    if (m_state != State::Idle) return;
    m_isWatchStopRequested = false;
    m_isStopRequested = false;
    m_isUsbRequested = false;
    m_history->clear();
    SetState(State::Watching);
    m_watcher.setFuture(QtConcurrent::run([this]() -> AutoConnectResult {
        try {
            return WatchPhones();
        } catch (const std::exception& error) {
            m_logger.Write(LogLevel::Error, "WATCH", std::string("Automatic mode failed: ") + error.what());
            AutoConnectResult result;
            result.message = std::string("Die automatische Verbindung ist ausgefallen: ") + error.what();
            return result;
        }
    }));
}

// The worker of the automatic mode. It looks at the bus every second, so it keeps quiet in the log; the connection
// itself uses the platform's backend. Where wireless Android Auto is built, Wi-Fi and Bluetooth stay up for as long as
// the watch runs; a pairing phone's question goes to the pairing page, and its answer goes back from the GUI thread.
// The paired phones go to the Bluetooth page; a phone chosen there is handed to the station between two waits, on this
// thread, which owns the station.
AutoConnectResult MainWindow::WatchPhones()
{
    const auto onStatus = [this](const std::string& status) {
        QMetaObject::invokeMethod(this, [this, status] { ShowStep(QString::fromStdString(status)); }, Qt::QueuedConnection);
    };
    LibusbUsbBackend usb(m_logger, true);
    PhoneWatchDeps deps;
    deps.usbPhones = [&usb] { return UsbPhoneIdentities(usb.EnumerateDevices()); };
    deps.connectUsb = [this] { return ConnectPhoneAutomatically(m_backend, m_logger, m_isStopRequested, MakeCallbacks()); };
    deps.wait = [this](std::chrono::milliseconds duration) { SleepUnlessStopped(duration, m_isWatchStopRequested); };
    deps.onAttemptStart = [this] { QMetaObject::invokeMethod(this, [this] { OnAttemptStart(); }, Qt::QueuedConnection); };
    deps.onAttemptEnd = [this](const AutoConnectResult& result) {
        QMetaObject::invokeMethod(this, [this, result] { OnAttemptEnd(result); }, Qt::QueuedConnection);
    };
#ifdef HEADUNIT_WIRELESS
    BluetoothEvents events;
    events.onStatus = onStatus;
    events.onPairingRequest = [this](const PairingRequest& request) {
        QMetaObject::invokeMethod(this, [this, request] {
            m_pairing->Ask(QString::fromStdString(request.phone), QString::fromStdString(request.code), request.answer);
        }, Qt::QueuedConnection);
    };
    events.onPairingEnd = [this] { QMetaObject::invokeMethod(this, [this] { m_pairing->End(); }, Qt::QueuedConnection); };
    events.onPhonesChanged = [this](const std::vector<BluetoothPhone>& phones) {
        QMetaObject::invokeMethod(this, [this, phones] { m_bluetoothPage->SetPhones(phones); }, Qt::QueuedConnection);
    };
    WirelessStation wireless(m_logger, events);
    wireless.Start();
    deps.waitForWirelessPhone = [this, &wireless](std::chrono::milliseconds timeout) {
        if (const std::string phone = TakePhoneSwitch(); !phone.empty()) wireless.SwitchToPhone(phone);
        return wireless.WaitForPhone(timeout);
    };
    deps.connectWireless = [this, &wireless](int phone) { return wireless.Serve(phone, m_isStopRequested, MakeCallbacks()); };
    deps.requestWireless = [&wireless] { return wireless.ReconnectPhones(); };
    const AutoConnectResult result = RunPhoneWatch(deps, m_logger, m_isWatchStopRequested, m_isStopRequested, m_isUsbRequested, onStatus);
    if (m_isRadioOffRequested) wireless.SwitchRadiosOff();
    return result;
#else
    return RunPhoneWatch(deps, m_logger, m_isWatchStopRequested, m_isStopRequested, m_isUsbRequested, onStatus);
#endif
}

// What a connection needs from the window; called on the worker for every connection. The phone's music is watched, so
// that the radio's own player can give way when the phone starts playing.
ProjectionCallbacks MainWindow::MakeCallbacks()
{
    ProjectionCallbacks callbacks;
    {
        std::lock_guard lock(m_displayMutex);
        callbacks.display = m_display;
    }
    m_logger.Write(LogLevel::Info, "UI", "Display size for this connection: " + DisplayText(callbacks.display));
    callbacks.onStatus = [this](const std::string& status) {
        QMetaObject::invokeMethod(this, [this, status] { ShowStep(QString::fromStdString(status)); }, Qt::QueuedConnection);
    };
    callbacks.onPhoneName = [this](const std::string& name) {
        QMetaObject::invokeMethod(this, [this, name] { m_phoneName = QString::fromStdString(name); }, Qt::QueuedConnection);
    };
    callbacks.onNativeScreen = [this] {
        QMetaObject::invokeMethod(this, [this] { ApplyConsoleEffect(m_console.Open(ConsoleController::Screen::RadioHome)); }, Qt::QueuedConnection);
    };
    callbacks.onFrame = [this](VideoFrame frame) {
        std::lock_guard lock(m_frameMutex);
        m_latestFrame = std::move(frame);
    };
    callbacks.input = m_input;
    callbacks.openAudio = [opener = m_audio.Opener(), activity = m_phoneMedia](AudioKind kind, const PcmFormat& format) -> std::shared_ptr<IPcmOutput> {
        auto output = opener(kind, format);
        if (kind != AudioKind::Media || !output) return output;
        return std::make_shared<WatchedOutput>(std::move(output), activity);
    };
    return callbacks;
}

// A connection starts: the picture area waits for the phone.
void MainWindow::OnAttemptStart()
{
    m_displayedFrames = 0;
    m_console.SetProjectionConnected(false);
    ClearPicture("Verbinde Android Auto ...");
    m_status->clear();
    ShowScreen();
    if (m_state == State::Watching) SetState(State::Connecting);
}

// A connection of the automatic mode has ended; the mode itself goes on and the radio's pages come back.
void MainWindow::OnAttemptEnd(const AutoConnectResult& result)
{
    m_console.SetProjectionConnected(false);
    m_phoneName.clear();
    ClearPicture(result.hasVideo || result.isStoppedByUser ? "Android Auto beendet." : "Android Auto ist nicht verbunden.");
    m_status->setText("Android Auto: nicht verbunden");
    ShowScreen();
    if (!m_isCloseRequested && (m_state == State::Connecting || m_state == State::Stopping)) SetState(State::Watching);
}

// The worker has ended (the automatic mode or a test connection): a test exits with its result, a close request closes.
void MainWindow::FinishConnect(const AutoConnectResult& result)
{
    m_console.SetProjectionConnected(false);
    ShowScreen();
    SetState(State::Idle);
    if (m_mode == TestMode::Projection) {
        QApplication::exit(m_displayedFrames >= kProjectionTestFrames ? 0 : 3);
        return;
    }
    if (m_scriptedTest) {
        QApplication::exit(m_isTestFinished ? m_testExitCode : 3);
        return;
    }
    if (m_isCloseRequested) {
        close();
        return;
    }
    ClearPicture(result.hasVideo || result.isStoppedByUser ? "Android Auto beendet." : "Android Auto konnte nicht verbunden werden.");
    m_status->setText("Android Auto: nicht verbunden");
    ShowStep(QString::fromStdString(result.message));
}

// Drops the last frame and shows `message` in the picture area.
void MainWindow::ClearPicture(const QString& message)
{
    {
        std::lock_guard lock(m_frameMutex);
        m_latestFrame.reset();
    }
    m_video->ClearFrame(message);
}

// The step as the current line and in the history.
void MainWindow::ShowStep(const QString& text)
{
    m_step->setText(text);
    m_history->appendPlainText(text);
}

// The window's tick (every 33 ms): the newest frame, the audio display, the volume bar and the status bar, who has the
// sound, and the scripted test.
void MainWindow::Tick()
{
    ShowLatestFrame();
    UpdateAudioDisplay();
    UpdateVolumeBar();
    UpdateStatusBar();
    KeepOneSound();
    if (m_state == State::Connecting && !m_isTestFinished && m_scriptedTest) m_scriptedTest->Tick();
}

// Shows the newest frame of the worker, if there is one. The first frame brings the phone's picture to the front; the
// projection test ends after a few.
void MainWindow::ShowLatestFrame()
{
    std::optional<VideoFrame> frame;
    {
        std::lock_guard lock(m_frameMutex);
        frame.swap(m_latestFrame);
    }
    if (!frame) return;
    m_video->SetFrame(*frame);
    if (++m_displayedFrames == 1) {
        m_logger.Write(LogLevel::Info, "VIDEO", "First real Android Auto frame displayed in Qt");
        m_console.SetProjectionConnected(true);
        ShowScreen();
    }
    // A display of another shape than the phone's frame shows only part of it: both are named.
    const QString shownArea = VideoLayoutOf(m_display).HasMargins() ? ", Anzeige " + QString::fromStdString(DisplayText(m_display)) : QString();
    m_status->setText(QString("Android Auto: Video %1x%2%3 | %4 Bilder angezeigt").arg(frame->width).arg(frame->height).arg(shownArea).arg(m_displayedFrames));
    if (m_mode == TestMode::Projection && m_displayedFrames >= kProjectionTestFrames) {
        m_isStopRequested = true;
        QApplication::exit(0);
    }
}

// The audio display shows the volume and the streams' levels.
void MainWindow::UpdateAudioDisplay()
{
    std::array<AudioState::Meter, kAudioKindCount> meters;
    for (int kind = 0; kind < kAudioKindCount; ++kind) meters[static_cast<std::size_t>(kind)] = m_audioState->ReadMeter(static_cast<AudioKind>(kind));
    m_panel->Display()->SetState(m_audioState->Volume(), m_audioState->IsMuted(), meters);
}

// Whatever changed the volume or the mute state (the console's keys, the keyboard, a touch on the bar itself), the
// screen shows the bar.
void MainWindow::UpdateVolumeBar()
{
    const int volume = m_audioState->Volume();
    const bool isMuted = m_audioState->IsMuted();
    if (volume == m_shownVolume && isMuted == m_wasMuted) return;
    m_shownVolume = volume;
    m_wasMuted = isMuted;
    m_volumeBar->ShowVolume(volume, isMuted);
}

// The pages' status bar: the source of the sound (the radio's own player while it sounds, else the projected phone),
// and whether the sound and the microphone are muted. Handed to the pages only when it changed.
void MainWindow::UpdateStatusBar()
{
    StatusState status;
    status.isMuted = m_audioState->IsMuted();
    status.isMicrophoneMuted = m_audioState->IsMicrophoneMuted();
    QString local = m_radio->SoundingName();
    if (local.isEmpty()) local = m_music->SoundingName();
    status.source = StatusSourceText(local.toStdString(), m_console.IsProjectionConnected(), m_phoneName.toStdString());
    if (status == m_shownStatus) return;
    m_shownStatus = status;
    for (MenuPage* page : MenuPages()) page->SetStatus(status);
}

// A touch on the status bar: the speaker mutes the sound, the microphone mutes the microphone, home is the Home key.
void MainWindow::PressStatus(StatusButton button)
{
    switch (button) {
    case StatusButton::Speaker:
        m_audioState->ToggleMute();
        break;
    case StatusButton::Microphone: {
        m_audioState->ToggleMicrophoneMute();
        const std::string message = m_audioState->IsMicrophoneMuted() ? "Mikrofon stumm" : "Mikrofon an";
        m_logger.Write(LogLevel::Info, "AUDIO", message);
        ShowStep(QString::fromStdString(message));
        break;
    }
    case StatusButton::Home:
        PressConsole(ConsoleKey::Home);
        break;
    }
    UpdateStatusBar();
}

// The radio's pages, which share the display's shape and the status bar.
std::vector<MenuPage*> MainWindow::MenuPages() const
{
    return {m_homeMenu, m_music, m_radio, m_settings, m_bluetoothPage, m_pairing};
}

// The keyboard: controller keys go through the console (see ConsoleController), phone keys to the radio's side or the
// phone, and a few extra keys work the volume and the knob. False for a key the window does not use.
bool MainWindow::HandleKey(QKeyEvent* event, bool isDown)
{
    if (const auto console = ConsoleKeyFor(event->key())) {
        if (isDown && !event->isAutoRepeat()) PressConsole(*console);
        return true;
    }
    if (const unsigned keycode = PhoneKeyFor(event->key()); keycode != 0) {
        const bool isArrow = keycode == keys::DpadUp || keycode == keys::DpadDown || keycode == keys::DpadLeft || keycode == keys::DpadRight;
        if (!event->isAutoRepeat()) {
            SendKey(keycode, isDown);
        } else if (isDown && isArrow) {
            // Held arrows keep nudging, wherever their press went.
            MenuPage* page = FrontPage();
            if (!m_localKeys.contains(keycode)) m_input->Tap(keycode);
            else if (page) page->Nudge(keycode);
        }
        return true;
    }
    return isDown && HandleExtraKey(event->key());
}

// Volume, mute and turning the controller (the arrow keys are its arrows, so turning needs keys of its own).
bool MainWindow::HandleExtraKey(int key)
{
    switch (key) {
    case Qt::Key_Plus:
    case Qt::Key_Equal: m_audioState->ChangeVolume(+1); return true;
    case Qt::Key_Minus: m_audioState->ChangeVolume(-1); return true;
    case Qt::Key_M: m_audioState->ToggleMute(); return true;
    case Qt::Key_Comma: Rotate(-1); return true;
    case Qt::Key_Period: Rotate(+1); return true;
    default: return false;
    }
}

// The radio's page in front, or nothing when the phone's picture is: a pairing question first, else where the console is.
MenuPage* MainWindow::FrontPage() const
{
    if (m_pairing->IsAsking()) return m_pairing;
    switch (m_console.CurrentScreen()) {
    case ConsoleController::Screen::RadioHome: return m_homeMenu;
    case ConsoleController::Screen::Multimedia: return m_music;
    case ConsoleController::Screen::Radio: return m_radio;
    case ConsoleController::Screen::Settings: return m_settings;
    case ConsoleController::Screen::Bluetooth: return m_bluetoothPage;
    default: return nullptr;
    }
}

// Brings the front page, or the phone's picture, to the front.
void MainWindow::ShowScreen()
{
    MenuPage* page = FrontPage();
    m_screens->setCurrentWidget(page ? static_cast<QWidget*>(page) : m_video);
    if (m_volumeBar->isVisible()) m_volumeBar->raise();
}

// Keys go to the radio's side when it takes them (PressLocally), otherwise to the phone. A release goes where its press
// went, so that neither side sees half a key press.
void MainWindow::SendKey(unsigned keycode, bool isDown)
{
    if (!isDown && m_localKeys.erase(keycode) > 0) return;
    if (isDown && PressLocally(keycode)) {
        m_localKeys.insert(keycode);
        return;
    }
    m_console.NoteKey(keycode, isDown);
    m_input->Key(keycode, isDown);
}

// The controller's arrows and push work the radio's page in front; the media keys (play, track skip) work the radio's
// own player while it plays or is paused. Otherwise both belong to the phone.
bool MainWindow::PressLocally(unsigned keycode)
{
    if (IsControllerKey(keycode)) {
        MenuPage* page = FrontPage();
        if (!page) return false;
        if (keycode == keys::DpadCenter) page->Push();
        else page->Nudge(keycode);
        return true;
    }
    const bool isTaken = m_music->MediaKey(keycode) || m_radio->MediaKey(keycode);
    if (isTaken) m_logger.Write(LogLevel::Info, "MEDIA", "Media key " + std::to_string(keycode) + " for the radio's own player");
    return isTaken;
}

// Turning the controller works the radio's page in front, otherwise the phone.
void MainWindow::Rotate(int detents)
{
    if (MenuPage* page = FrontPage()) page->Turn(detents);
    else m_input->Rotate(detents);
}

// A tile opens one of the radio's pages or does what its controller key does; the others have nothing behind them yet.
void MainWindow::OpenMenuEntry(HomeMenuEntry entry)
{
    if (const auto page = HomeMenuPage(entry)) {
        ApplyConsoleEffect(m_console.Open(*page));
        return;
    }
    if (const auto key = HomeMenuKey(entry)) {
        PressConsole(*key);
        return;
    }
    const std::string message = std::string(HomeMenuTitle(entry)) + ": noch keine Funktion";
    m_logger.Write(LogLevel::Info, "CONSOLE", message);
    ShowStep(QString::fromStdString(message));
}

// The radio's player is about to start or resume: the phone's music, if any, pauses, as when a car changes its source.
void MainWindow::PausePhoneMedia()
{
    m_localStartMs = SteadyNowMs();
    if (m_console.IsProjectionConnected()) m_input->Tap(keys::MediaPause);
}

// The home menu's tiles changed (moved on the menu, shown or hidden in the settings): shown everywhere and remembered.
void MainWindow::SetTiles(const HomeTileSetup& setup)
{
    m_tiles = setup;
    m_homeMenu->SetTiles(ShownTiles(m_tiles));
    m_settings->SetSetup(m_tiles);
    QSettings().setValue(kTilesSetting, QString::fromStdString(TileSetupText(m_tiles)));
}

// A phone chosen on the Bluetooth page becomes the Android Auto phone: the running session ends (its phone gets the
// goodbye), and the watch's worker has the chosen phone connect anew over Bluetooth, which starts Android Auto on it.
void MainWindow::SwitchToPhone(const BluetoothPhone& phone)
{
    {
        std::lock_guard lock(m_phoneSwitchMutex);
        m_phoneSwitch = phone.id;
    }
    if (m_state == State::Connecting) m_isStopRequested = true;
    const std::string message = "Bluetooth: Android Auto wechselt zu " + phone.name + " ...";
    m_logger.Write(LogLevel::Info, "BT", message);
    ShowStep(QString::fromStdString(message));
}

// The phone chosen on the Bluetooth page, once (watch's worker); empty when none was chosen since.
std::string MainWindow::TakePhoneSwitch()
{
    std::lock_guard lock(m_phoneSwitchMutex);
    return std::exchange(m_phoneSwitch, std::string());
}

// Only one source sounds: when the phone starts its music after the radio's player (connecting Android Auto often
// resumes the phone's last music), the radio's player falls silent (music pauses, radio stops).
void MainWindow::KeepOneSound()
{
    const std::int64_t now = SteadyNowMs();
    const AudioPlayer::State state = m_player.CurrentStatus().state;
    const bool isLocalSounding = state == AudioPlayer::State::Opening || state == AudioPlayer::State::Playing;
    if (!IsPhoneTakingOver(isLocalSounding, m_localStartMs, m_phoneMedia->IsSounding(now), m_phoneMedia->StartMs())) return;
    m_logger.Write(LogLevel::Info, "MEDIA", "The phone started its music: the radio's own player gives way");
    m_music->GiveWay();
    m_radio->GiveWay();
}

// A short touch at a fixed spot of the phone's screen.
void MainWindow::TapPhone(int x, int y)
{
    m_input->Touch(TouchAction::Down, x, y);
    QTimer::singleShot(60, this, [this, x, y] { m_input->Touch(TouchAction::Up, x, y); });
}

// Keys and taps go to the phone, the message (if any) becomes a line in the window log, and the projection key may
// start the connection. After the home key the app launcher shows, whose button leads to the dashboard: the picture is
// read again once the phone has drawn it.
void MainWindow::ApplyConsoleEffect(const ConsoleEffect& effect)
{
    for (const unsigned keycode : effect.phoneKeys) m_input->Tap(keycode);
    for (const auto& [x, y] : effect.phoneTaps) TapPhone(x, y);
    if (effect.shouldRetryDashboard) {
        QTimer::singleShot(1000, this, [this] {
            if (!m_console.IsProjectionConnected() || CurrentPhoneScreen() != PhoneScreen::Other) return;
            const auto [x, y] = DashboardButtonPosition(m_display);
            TapPhone(x, y);
        });
    }
    if (!effect.message.empty()) {
        m_logger.Write(LogLevel::Info, "CONSOLE", effect.message);
        ShowStep(QString::fromStdString(effect.message));
    }
    ShowScreen();
    if (effect.shouldConnect) StartConnect();
}

// The frames the window has shown in this connection.
unsigned MainWindow::DisplayedFrames() const
{
    return m_displayedFrames;
}

// The input bus to the phone.
ProjectionInput& MainWindow::PhoneInput()
{
    return *m_input;
}

// The car's audio state.
AudioState& MainWindow::Audio()
{
    return *m_audioState;
}

// The display announced to the phone.
DisplayConfig MainWindow::AnnouncedDisplay() const
{
    return m_display;
}

// What the phone shows right now, read from the last picture (unknown without one).
PhoneScreen MainWindow::CurrentPhoneScreen() const
{
    if (!m_video->HasFrame()) return PhoneScreen::Unknown;
    const QImage& image = m_video->Image();
    if (image.format() != QImage::Format_RGB888) return PhoneScreen::Unknown;
    return DetectPhoneScreen(image.constBits(), image.width(), image.height(), static_cast<int>(image.bytesPerLine()));
}

// Where the console is.
ConsoleController::Screen MainWindow::ConsoleScreen() const
{
    return m_console.CurrentScreen();
}

// Which view is in front.
ScriptedTestHost::FrontView MainWindow::FrontViewShown() const
{
    const QWidget* shown = m_screens->currentWidget();
    if (shown == m_video) return FrontView::PhonePicture;
    if (shown == m_homeMenu) return FrontView::HomeMenu;
    if (shown == m_radio) return FrontView::Tuner;
    return FrontView::Other;
}

// Whether the window's log shows a line containing `text`.
bool MainWindow::HasLogLine(const char* text) const
{
    return m_history->toPlainText().contains(QString::fromUtf8(text));
}

// A controller key: Back first closes what a page has opened inside itself (the tuner's list of countries); only Home
// depends on where the phone is, which is read from its picture.
void MainWindow::PressConsole(ConsoleKey key)
{
    if (key == ConsoleKey::Back) {
        if (MenuPage* page = FrontPage(); page && page->Back()) return;
    }
    PhoneScreen phone = PhoneScreen::Unknown;
    if (key == ConsoleKey::Home && m_console.IsProjectionConnected()) {
        phone = CurrentPhoneScreen();
        m_logger.Write(LogLevel::Info, "CONSOLE", std::string("Phone screen read from the picture: ") +
            (phone == PhoneScreen::Dashboard ? "dashboard" : phone == PhoneScreen::Other ? "other (app or launcher)" : "unknown"));
    }
    ApplyConsoleEffect(m_console.Press(key, phone));
}

// Test runs can save what the phone drew (and the whole window) into the directory named by HEADUNIT_TEST_SHOTS, so
// that a person can look at the evidence.
void MainWindow::SaveTestShot(const char* name, bool isWholeWindow)
{
    const QByteArray directory = qgetenv("HEADUNIT_TEST_SHOTS");
    if (directory.isEmpty()) return;
    QDir().mkpath(QString::fromLocal8Bit(directory));
    const QString path = QDir(QString::fromLocal8Bit(directory)).filePath(QString::fromLatin1(name));
    if (isWholeWindow) grab().save(path);
    else if (m_video->HasFrame()) m_video->Image().save(path);
}

// Ends a scripted test: the session ends cleanly, and FinishConnect then exits with the result.
void MainWindow::FinishTest(int exitCode, const std::string& summary)
{
    m_logger.Write(exitCode == 0 ? LogLevel::Info : LogLevel::Error, "TEST", summary);
    m_testExitCode = exitCode;
    m_isTestFinished = true;
    m_isStopRequested = true;
}

// The program's log.
Logger& MainWindow::TestLog()
{
    return m_logger;
}
}
