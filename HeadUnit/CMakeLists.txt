cmake_minimum_required(VERSION 3.24)
project(HeadUnit VERSION 0.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
option(HEADUNIT_BUILD_APP "Build the Qt application (needs Qt 6, the protocol dependencies, libusb and FFmpeg)" ON)
option(HEADUNIT_INSTALL_UDEV_RULES "Linux: install the udev rule for phone access with the program (for packagers; users run scripts/install-udev-rules.sh)" OFF)
option(HEADUNIT_WIRELESS "Linux: wireless Android Auto (Bluetooth and a Wi-Fi hotspot); needs Qt 6 DBus to build, BlueZ and NetworkManager to run" ON)
set(HEADUNIT_AUDIO_BACKEND "auto" CACHE STRING "Audio output: auto (WASAPI on Windows, miniaudio elsewhere), wasapi or miniaudio")
set_property(CACHE HEADUNIT_AUDIO_BACKEND PROPERTY STRINGS auto wasapi miniaudio)
include(cmake/BuildProfiles.cmake)
include(CTest)

# The portable core: no Qt, no operating system API.
add_library(headunit_core src/CommandLine.cpp src/logging/LogLevel.cpp src/logging/Logger.cpp src/platform/Environment.cpp
  src/usb/UsbTypes.cpp src/usb/UsbDescriptors.cpp src/usb/UsbLogging.cpp src/usb/UsbProbeResult.cpp src/usb/RepairResult.cpp
  src/androidauto/AndroidDeviceDetector.cpp src/androidauto/AoaNegotiator.cpp src/androidauto/AutoConnect.cpp src/androidauto/PhoneWatch.cpp
  src/androidauto/ConsoleController.cpp src/androidauto/DisplayConfig.cpp src/androidauto/PhoneScreenDetector.cpp src/androidauto/ProjectionInput.cpp
  src/androidauto/TouchMapping.cpp src/androidauto/TransportReceiveBuffer.cpp
  src/audio/AudioClock.cpp src/audio/PcmFormat.cpp src/audio/Pcm16.cpp src/audio/PcmRingBuffer.cpp src/audio/AudioState.cpp src/audio/MediaActivity.cpp
  src/audio/WatchedOutput.cpp src/media/MusicLibrary.cpp src/media/StreamText.cpp src/ui/KnobZones.cpp src/ui/HomeTileSetup.cpp src/ui/HomeMenuLayout.cpp
  src/ui/BluetoothPhones.cpp src/ui/TouchDrag.cpp)
target_include_directories(headunit_core PUBLIC src)
target_compile_definitions(headunit_core PRIVATE $<$<CONFIG:DebugLevelLog,ReleaseLevelLog>:HEADUNIT_VERBOSE_LOGGING>)
if(MSVC)
  target_compile_options(headunit_core PUBLIC /W4 /utf-8 /permissive-)
else()
  target_compile_options(headunit_core PUBLIC -Wall -Wextra -Wpedantic)
endif()

if(HEADUNIT_BUILD_APP)
  if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Use an x64 compiler and x64 Qt kit")
  endif()
  if(HEADUNIT_AUDIO_BACKEND STREQUAL "wasapi" AND NOT WIN32)
    message(FATAL_ERROR "HEADUNIT_AUDIO_BACKEND=wasapi needs Windows")
  endif()
  if(HEADUNIT_AUDIO_BACKEND STREQUAL "miniaudio" OR (HEADUNIT_AUDIO_BACKEND STREQUAL "auto" AND NOT WIN32))
    set(HEADUNIT_USE_MINIAUDIO ON)
  else()
    set(HEADUNIT_USE_MINIAUDIO OFF)
  endif()

  find_package(Threads REQUIRED)
  find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Concurrent Network)
  # Wireless Android Auto talks to BlueZ over D-Bus (QtDBus is part of qt6-base-dev) and drives NetworkManager.
  set(HEADUNIT_WIRELESS_ENABLED OFF)
  if(HEADUNIT_WIRELESS AND NOT WIN32)
    find_package(Qt6 6.4 QUIET COMPONENTS DBus)
    if(Qt6DBus_FOUND)
      set(HEADUNIT_WIRELESS_ENABLED ON)
    else()
      message(WARNING "Qt6 DBus not found: building without wireless Android Auto (HEADUNIT_WIRELESS=OFF hides this warning)")
    endif()
  endif()
  include(cmake/Protocol.cmake)
  include(cmake/Libusb.cmake)

  # Everything that talks to the phone and the sound output. The sources shared by all platforms come first;
  # what differs per platform is the discovery backend (WindowsUsbBackend; libusb everywhere, LibusbUsbBackend),
  # the driver repair and the audio engine.
  set(HEADUNIT_USB_SOURCES src/usb/AndroidUsbProbe.cpp src/usb/AutoConnectSystem.cpp src/usb/LibusbControl.cpp src/usb/LibusbHandles.cpp
    src/usb/LibusbUsbBackend.cpp src/usb/ProjectionTransport.cpp src/usb/UsbBackendFactory.cpp
    src/androidauto/AndroidAutoSession.cpp src/androidauto/AudioSinkChannel.cpp src/androidauto/ChannelSupport.cpp src/androidauto/DisplayService.cpp
    src/androidauto/InputReports.cpp src/androidauto/MicrophoneChannel.cpp src/androidauto/ProjectionSession.cpp src/androidauto/ServiceDiscovery.cpp
    src/audio/AudioEngine.cpp src/audio/QueuedPcmStream.cpp)
  if(WIN32)
    list(APPEND HEADUNIT_USB_SOURCES src/usb/WindowsUsbBackend.cpp src/usb/WindowsDriverRepair.cpp src/platform/WindowsSupport.cpp)
  else()
    list(APPEND HEADUNIT_USB_SOURCES src/usb/LinuxDriverRepair.cpp)
  endif()
  if(HEADUNIT_USE_MINIAUDIO)
    list(APPEND HEADUNIT_USB_SOURCES src/audio/MiniaudioPcmStream.cpp src/audio/MiniaudioImpl.cpp)
  else()
    list(APPEND HEADUNIT_USB_SOURCES src/audio/WasapiPcmStream.cpp)
  endif()
  add_library(headunit_usb ${HEADUNIT_USB_SOURCES})
  target_link_libraries(headunit_usb PRIVATE headunit_libusb headunit_aasdk)
  target_link_libraries(headunit_usb PUBLIC headunit_video)
  target_link_libraries(headunit_usb PUBLIC headunit_core Threads::Threads)
  if(WIN32)
    target_link_libraries(headunit_usb PRIVATE setupapi)
    target_compile_definitions(headunit_usb PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  endif()
  if(HEADUNIT_USE_MINIAUDIO)
    target_compile_definitions(headunit_usb PRIVATE HEADUNIT_AUDIO_MINIAUDIO)
    # miniaudio loads the sound system libraries (PulseAudio, ALSA, ...) at run time.
    target_include_directories(headunit_usb SYSTEM PRIVATE third_party/miniaudio)
    target_link_libraries(headunit_usb PRIVATE ${CMAKE_DL_LIBS})
    if(NOT WIN32)
      target_link_libraries(headunit_usb PRIVATE m)
    endif()
    # Third-party code: its warnings are not ours to fix.
    set_source_files_properties(src/audio/MiniaudioImpl.cpp PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")
  endif()

  if(HEADUNIT_WIRELESS_ENABLED)
    # Linux only. The parts of wireless Android Auto that need neither Qt nor a phone (message framing, the conversation
    # with the phone, the TCP transport) are a library of their own, which ProtocolTests links as well.
    add_library(headunit_wireless_link src/wireless/WirelessProtocol.cpp src/wireless/WirelessFrameParser.cpp src/wireless/WirelessHandshake.cpp
      src/wireless/WirelessLink.cpp src/wireless/FileDescriptor.cpp src/wireless/TcpListener.cpp src/wireless/SocketTransport.cpp)
    target_link_libraries(headunit_wireless_link PUBLIC headunit_core headunit_aasdk Threads::Threads)
    # The Bluetooth service and the hotspot. The D-Bus objects need moc, so this is the one target that runs it (the rest
    # of the code base has no Q_OBJECT).
    add_library(headunit_wireless src/wireless/SystemCommand.cpp src/wireless/Hotspot.cpp src/wireless/BluezCalls.cpp src/wireless/BluetoothContext.cpp
      src/wireless/PairingAgent.cpp src/wireless/AndroidAutoProfile.cpp src/wireless/DeviceWatcher.cpp src/wireless/BluetoothService.cpp
      src/wireless/WirelessSettings.cpp src/wireless/WirelessStation.cpp src/wireless/WirelessDiagnostics.cpp)
    set_target_properties(headunit_wireless PROPERTIES AUTOMOC ON)
    target_link_libraries(headunit_wireless PUBLIC headunit_wireless_link headunit_usb Qt6::Core Qt6::DBus)
    target_compile_definitions(headunit_wireless PUBLIC HEADUNIT_WIRELESS)
  endif()

  add_executable(HeadUnit src/main.cpp src/platform/ShutdownSignal.cpp src/radio/RadioBrowser.cpp
    src/ui/MainWindow.cpp src/ui/ScriptedPhoneTest.cpp src/ui/VideoWidget.cpp src/ui/RotaryKnob.cpp src/ui/AudioDisplay.cpp src/ui/CarPanel.cpp
    src/ui/MenuStyle.cpp src/ui/MenuPage.cpp src/ui/HomeMenu.cpp src/ui/SettingsPage.cpp src/ui/PairingPage.cpp src/ui/PlayerPage.cpp
    src/ui/MultimediaPage.cpp src/ui/RadioPage.cpp src/ui/BluetoothPage.cpp src/ui/VolumeOverlay.cpp)
  target_link_libraries(HeadUnit PRIVATE headunit_usb headunit_media Qt6::Widgets Qt6::Concurrent Qt6::Network)
  if(HEADUNIT_WIRELESS_ENABLED)
    target_link_libraries(HeadUnit PRIVATE headunit_wireless)
  endif()

  if(WIN32)
    add_custom_command(TARGET HeadUnit POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:libusb>" "$<TARGET_FILE_DIR:HeadUnit>")
    add_custom_command(TARGET HeadUnit POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${HEADUNIT_DEPS_BIN}" "$<TARGET_FILE_DIR:HeadUnit>")
    install(FILES "$<TARGET_FILE:libusb>" DESTINATION bin)
    install(DIRECTORY "${HEADUNIT_DEPS_BIN}/" DESTINATION bin FILES_MATCHING PATTERN "*.dll")
    install(TARGETS HeadUnit RUNTIME DESTINATION bin)
    qt_generate_deploy_app_script(TARGET HeadUnit OUTPUT_SCRIPT deployScript NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${deployScript})
  else()
    # The libraries come from the system's packages; nothing is deployed next to the program.
    include(GNUInstallDirs)
    install(TARGETS HeadUnit RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(HEADUNIT_INSTALL_UDEV_RULES)
      install(FILES packaging/linux/70-headunit-android.rules DESTINATION lib/udev/rules.d)
    endif()
  endif()
endif()

if(BUILD_TESTING)
  add_executable(CoreTests tests/CoreTests.cpp tests/TestSupport.cpp tests/UsbDescriptorTests.cpp tests/AoaTests.cpp tests/AutoConnectTests.cpp
    tests/PhoneWatchTests.cpp tests/DisplayTests.cpp tests/ConsoleTests.cpp tests/AudioTests.cpp tests/MenuTests.cpp tests/MediaTests.cpp
    tests/LoggingTests.cpp tests/CommandLineTests.cpp tests/TransportBufferTests.cpp)
  target_link_libraries(CoreTests PRIVATE headunit_core)
  add_test(NAME CoreTests COMMAND CoreTests)
  if(HEADUNIT_BUILD_APP)
    add_executable(ProtocolTests tests/ProtocolTests.cpp tests/TestSupport.cpp tests/FakeTransports.cpp tests/TlsTests.cpp tests/SessionTests.cpp tests/InputAudioTests.cpp)
    target_link_libraries(ProtocolTests PRIVATE headunit_usb headunit_aasdk headunit_libusb)
    add_test(NAME ProtocolTests COMMAND ProtocolTests)
    if(HEADUNIT_WIRELESS_ENABLED)
      # Message framing, the conversation (against a simulated phone) and the socket transport of wireless Android Auto.
      target_sources(ProtocolTests PRIVATE tests/WirelessTests.cpp)
      target_link_libraries(ProtocolTests PRIVATE headunit_wireless_link)
      target_compile_definitions(ProtocolTests PRIVATE HEADUNIT_WIRELESS_TESTS)
    endif()
    if(WIN32)
      add_custom_command(TARGET ProtocolTests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${HEADUNIT_DEPS_BIN}" "$<TARGET_FILE_DIR:ProtocolTests>")
    endif()
  endif()
endif()
