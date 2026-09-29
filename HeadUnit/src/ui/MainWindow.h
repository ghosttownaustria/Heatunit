#pragma once
#include "androidauto/AutoConnect.h"
#include "androidauto/ConsoleController.h"
#include "androidauto/DisplayConfig.h"
#include "androidauto/ProjectionInput.h"
#include "audio/AudioEngine.h"
#include "audio/AudioState.h"
#include "audio/MediaActivity.h"
#include "logging/Logger.h"
#include "media/AudioPlayer.h"
#include "ui/BluetoothPhones.h"
#include "ui/HomeMenuEntry.h"
#include "ui/HomeTileSetup.h"
#include "ui/ScriptedPhoneTest.h"
#include "ui/ScriptedTestHost.h"
#include "ui/StatusBar.h"
#include "usb/AutoConnectSystem.h"
#include "usb/IUsbBackend.h"
#include <QFutureWatcher>
#include <QMainWindow>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

class QCloseEvent;
class QComboBox;
class QHBoxLayout;
class QKeyEvent;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace headunit {
class BluetoothPage;
class CarPanel;
class HomeMenu;
class MenuPage;
class MultimediaPage;
class PairingPage;
class RadioPage;
class SettingsPage;
class VideoWidget;
class VolumeOverlay;

// Always ready, like a car: from the start the window watches USB and (where built) wireless Android Auto and connects
// whichever phone comes, with no button to press (RunPhoneWatch). The Android Auto tile and the projection key connect
// the phone on the USB cable once more (finding the phone, repairing the driver, starting Android Auto, restarting the
// USB link when needed). The button quits: it ends the session, switches Bluetooth and the Wi-Fi off (where wireless is
// built) and closes the program. The scripted test modes connect once over USB instead. Next to the picture sits
// the simulated centre console: rotary knob, hard keys and the audio display. The picture itself takes mouse input as
// touch. In its place the radio's own pages are shown whenever the console is on the radio's side (always while no phone
// is projected): the home menu, the music player, the tuner, the Bluetooth phones and the settings; the knob then works
// the page instead of the phone. The radio's own player plays through the same audio output as the phone, and only one
// of them sounds at a time: the one started last. The display size (video resolution) is chosen next to the button and only while no session
// runs: the phone is told the size once, when the connection starts.
class MainWindow final : public QMainWindow, private ScriptedTestHost {
public:
    // The test modes run one scripted check against the real phone and exit with its result.
    enum class TestMode { None, Smoke, Projection, Input, Audio, Console, Keys };

    MainWindow(IUsbBackend& backend, Logger& logger, TestMode mode = TestMode::None);
    ~MainWindow() override;

    void SetDisplay(const DisplayConfig& display);

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    // Idle: nothing runs. Watching: the automatic mode waits for a phone. Connecting: a connection or session runs.
    // Stopping: it is being ended.
    enum class State { Idle, Watching, Connecting, Stopping };

    IUsbBackend& m_backend;
    Logger& m_logger;
    TestMode m_mode;
    std::shared_ptr<ProjectionInput> m_input;
    ConsoleController m_console;
    std::shared_ptr<AudioState> m_audioState;
    AudioEngine m_audio;
    AudioPlayer m_player;                             // the radio's own music and radio; needs m_audio, so declared after it
    DisplayConfig m_display{kDefaultDisplay};
    QStackedWidget* m_screens{};                      // the phone's picture or one of the radio's pages
    VideoWidget* m_video{};
    HomeMenu* m_homeMenu{};
    MultimediaPage* m_music{};
    RadioPage* m_radio{};
    SettingsPage* m_settings{};
    BluetoothPage* m_bluetoothPage{};
    PairingPage* m_pairing{};                         // a phone's Bluetooth pairing question; in front while it asks
    VolumeOverlay* m_volumeBar{};                     // over the screens for a moment whenever the volume changes
    int m_shownVolume{};                              // the volume and mute state the bar showed last
    bool m_wasMuted{};
    StatusState m_shownStatus;                        // what the pages' status bar shows
    QString m_phoneName;                              // the connected phone's name, for the status bar
    HomeTileSetup m_tiles;                            // which tiles the home menu shows (remembered)
    std::shared_ptr<MediaActivity> m_phoneMedia{std::make_shared<MediaActivity>()};   // when the phone's music plays
    std::int64_t m_localStartMs{};                    // when the radio's own player last started or resumed
    std::set<unsigned> m_localKeys;                   // keys held down whose press the radio's side took
    CarPanel* m_panel{};
    QLabel* m_status{};
    QLabel* m_step{};
    QComboBox* m_displayChoice{};
    QPushButton* m_button{};
    QPlainTextEdit* m_history{};
    std::atomic_bool m_isStopRequested{};             // ends the running connection or session
    std::atomic_bool m_isWatchStopRequested{};        // ends the automatic mode (the window closes)
    std::atomic_bool m_isUsbRequested{};              // the automatic mode connects the phone on the USB cable once more
    std::atomic_bool m_isRadioOffRequested{};         // the program quits with Bluetooth and the Wi-Fi switched off
    std::mutex m_displayMutex;                        // m_display, as the worker reads it for each connection
    std::mutex m_phoneSwitchMutex;                    // m_phoneSwitch, handed to the watch's worker
    std::string m_phoneSwitch;                        // the phone chosen on the Bluetooth page, until the worker takes it
    std::mutex m_frameMutex;
    std::optional<VideoFrame> m_latestFrame;
    unsigned m_displayedFrames{};
    bool m_isCloseRequested{};
    State m_state{State::Idle};
    QFutureWatcher<AutoConnectResult> m_watcher;
    QFutureWatcher<UsbScanResult> m_scanWatcher;
    std::unique_ptr<ScriptedPhoneTest> m_scriptedTest;
    int m_testExitCode{3};
    bool m_isTestFinished{};

    QWidget* BuildScreens(QWidget* parent);
    void BuildControls(QWidget* parent, QVBoxLayout* layout);
    void BuildPanel(QWidget* parent, QHBoxLayout* layout);
    void ConnectSignals();
    void StartMode();
    void SetState(State state);
    void Quit();
    void StartConnect();
    void BeginConnect();
    void BeginWatch();
    AutoConnectResult WatchPhones();
    ProjectionCallbacks MakeCallbacks();
    void OnAttemptStart();
    void OnAttemptEnd(const AutoConnectResult& result);
    void FinishConnect(const AutoConnectResult& result);
    void ClearPicture(const QString& message);
    void ShowStep(const QString& text);
    void Tick();
    void ShowLatestFrame();
    void UpdateAudioDisplay();
    void UpdateVolumeBar();
    void UpdateStatusBar();
    void PressStatus(StatusButton button);
    std::vector<MenuPage*> MenuPages() const;
    bool HandleKey(QKeyEvent* event, bool isDown);
    bool HandleExtraKey(int key);
    MenuPage* FrontPage() const;
    void ShowScreen();
    void SendKey(unsigned keycode, bool isDown);
    bool PressLocally(unsigned keycode);
    void Rotate(int detents);
    void OpenMenuEntry(HomeMenuEntry entry);
    void PausePhoneMedia();
    void SetTiles(const HomeTileSetup& setup);
    void SwitchToPhone(const BluetoothPhone& phone);
    std::string TakePhoneSwitch();
    void KeepOneSound();
    void TapPhone(int x, int y);
    void ApplyConsoleEffect(const ConsoleEffect& effect);
    unsigned DisplayedFrames() const override;
    ProjectionInput& PhoneInput() override;
    AudioState& Audio() override;
    DisplayConfig AnnouncedDisplay() const override;
    PhoneScreen CurrentPhoneScreen() const override;
    ConsoleController::Screen ConsoleScreen() const override;
    FrontView FrontViewShown() const override;
    bool HasLogLine(const char* text) const override;
    void PressConsole(ConsoleKey key) override;
    void SaveTestShot(const char* name, bool isWholeWindow = false) override;
    void FinishTest(int exitCode, const std::string& summary) override;
    Logger& TestLog() override;
};
}
