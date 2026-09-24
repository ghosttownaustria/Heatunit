#pragma once
#include "androidauto/ConsoleController.h"
#include "androidauto/DisplayConfig.h"
#include "androidauto/ProjectionInput.h"
#include "audio/AudioEngine.h"
#include "audio/AudioFocus.h"
#include "audio/AudioTypes.h"
#include "media/AudioPlayer.h"
#include "ui/HomeMenuLayout.h"
#include "usb/AutoConnectSystem.h"
#include "usb/IUsbBackend.h"
#include "logging/Logger.h"
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QMainWindow>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
class QCloseEvent;
class QComboBox;
class QKeyEvent;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;

namespace headunit {
class CarPanel;
class HomeMenu;
class MenuPage;
class MultimediaPage;
class RadioPage;
class SettingsPage;
class VideoWidget;

// Always ready, like a car: from the start the window watches USB and (where built) wireless Android Auto and connects
// whichever phone comes, with no button to press (RunPhoneWatch). The button ends a running session, and otherwise
// connects the phone on the USB cable once more (finding the phone, repairing the driver, starting Android Auto,
// restarting the USB link when needed). The scripted test modes connect once over USB instead. Next to the picture sits the
// simulated centre console: rotary knob, hard keys and the audio display. The picture itself takes
// mouse input as touch. In its place the radio's own pages are shown whenever the console is on the
// radio's side (always while no phone is projected): the home menu, the music player, the tuner and the
// settings; the knob then works the page instead of the phone. The radio's own player plays through the
// same audio output as the phone, and only one of them sounds at a time: the one started last.
// The display size (video resolution) is chosen next to the button and only while no session runs:
// the phone is told the size once, when the connection starts.
class MainWindow final : public QMainWindow {
public:
    // The test modes run one scripted check against the real phone and exit with its result.
    enum class TestMode { None, Smoke, Projection, Input, Audio, Console, Keys };
    MainWindow(IUsbBackend& backend, Logger& logger, TestMode mode = TestMode::None);
    ~MainWindow() override;
    // Selects the display size for the next connection (ignored while a session is running or for a
    // size that is not offered). Not remembered: only a choice made in the window is.
    void SetDisplay(const DisplayConfig& display);
protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
private:
    // Idle: nothing runs. Watching: the automatic mode waits for a phone. Connecting: a connection or session runs.
    // Stopping: it is being ended.
    enum class State { Idle, Watching, Connecting, Stopping };
    void SetState(State state);
    void OnButton();
    void StartConnect();
    void BeginConnect();
    void BeginWatch();
    AutoConnectResult WatchPhones();
    ProjectionCallbacks MakeCallbacks();
    void OnAttemptStart();
    void OnAttemptEnd(const AutoConnectResult& result);
    void RequestStop();
    void FinishConnect(const AutoConnectResult& result);
    void ShowStep(const QString& text);
    void Tick();
    bool HandleKey(QKeyEvent* event, bool isDown);
    void RunInputTest();
    void RunAudioTest();
    void RunConsoleTest();
    void RunKeysTest();
    void PressConsole(ConsoleKey key);
    MenuPage* FrontPage() const;
    void ShowScreen();
    void SendKey(unsigned keycode, bool isDown);
    bool PressLocally(unsigned keycode);
    void PressPageKey(MenuPage& page, unsigned keycode);
    void Rotate(int detents);
    void OpenMenuEntry(HomeMenuEntry entry);
    void PausePhoneMedia();
    void SetTiles(const HomeTileSetup& setup);
    void KeepOneSound();
    void TapPhone(int x, int y);
    PhoneScreen CurrentPhoneScreen() const;
    void ApplyConsoleEffect(const ConsoleEffect& effect);
    void FinishTest(int exitCode, const std::string& summary);
    void SaveTestShot(const char* name, bool isWholeWindow = false);
    IUsbBackend& m_backend;
    Logger& m_logger;
    TestMode m_mode;
    std::shared_ptr<ProjectionInput> m_input;
    ConsoleController m_console;
    std::shared_ptr<AudioState> m_audioState;
    std::unique_ptr<IAudioEngine> m_audio;
    std::unique_ptr<AudioPlayer> m_player;   // the radio's own music and radio; needs m_audio, so declared after it
    DisplayConfig m_display{kDefaultDisplay};
    QStackedWidget* m_screens{};   // the phone's picture or one of the radio's pages
    VideoWidget* m_video{};
    HomeMenu* m_homeMenu{};
    MultimediaPage* m_music{};
    RadioPage* m_radio{};
    SettingsPage* m_settings{};
    HomeTileSetup m_tiles;                            // which tiles the home menu shows (remembered)
    std::shared_ptr<MediaActivity> m_phoneMedia{std::make_shared<MediaActivity>()};   // when the phone's music plays
    std::int64_t m_localStartMs{};                    // when the radio's own player last started or resumed
    std::set<unsigned> m_localKeys;  // keys held down whose press the radio's side took
    CarPanel* m_panel{};
    QLabel* m_status{};
    QLabel* m_step{};
    QComboBox* m_displayChoice{};
    QPushButton* m_button{};
    QPlainTextEdit* m_history{};
    std::atomic_bool m_isStopRequested{};         // ends the running connection or session
    std::atomic_bool m_isWatchStopRequested{};    // ends the automatic mode (the window closes)
    std::atomic_bool m_isUsbRequested{};          // the automatic mode connects the phone on the USB cable once more
    std::mutex m_displayMutex;                    // m_display, as the worker reads it for each connection
    std::mutex m_frameMutex;
    std::optional<VideoFrame> m_latestFrame;
    unsigned m_displayedFrames{};
    bool m_isCloseRequested{};
    State m_state{State::Idle};
    QFutureWatcher<AutoConnectResult> m_watcher;
    QFutureWatcher<UsbScanResult> m_scanWatcher;
    // Scripted test state.
    QElapsedTimer m_testClock;
    int m_testStage{};
    unsigned m_testFrames[4]{};
    int m_testExitCode{3};
    bool m_isTestFinished{};
    std::string m_testProblems;
    std::vector<std::string> m_testSteps;   // --test-keys script
};
}
