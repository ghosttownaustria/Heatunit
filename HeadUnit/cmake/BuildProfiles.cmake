# The four build profiles as CMake configurations, and the output layout that the Visual Studio projects share:
#   debug              Debug               bin/<system><architecture>/debug/
#   release            Release             bin/<system><architecture>/release/
#   debug_level_log    DebugLevelLog       bin/<system><architecture>/debug_level_log/
#   release_level_log  ReleaseLevelLog     bin/<system><architecture>/release_level_log/
# The two log profiles compile exactly like their base profile; they only define HEADUNIT_VERBOSE_LOGGING, which the
# logger turns into its most detailed level (see src/logging/LogLevel.h).
set(HEADUNIT_CONFIGURATIONS Debug Release DebugLevelLog ReleaseLevelLog)
get_property(headunitIsMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(headunitIsMultiConfig)
  set(CMAKE_CONFIGURATION_TYPES ${HEADUNIT_CONFIGURATIONS} CACHE STRING "Build profiles" FORCE)
else()
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build profile" FORCE)
  endif()
  if(NOT CMAKE_BUILD_TYPE IN_LIST HEADUNIT_CONFIGURATIONS)
    message(FATAL_ERROR "CMAKE_BUILD_TYPE must be one of: ${HEADUNIT_CONFIGURATIONS}")
  endif()
endif()

foreach(flagVariable CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_STATIC_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
  set(${flagVariable}_DEBUGLEVELLOG "${${flagVariable}_DEBUG}")
  set(${flagVariable}_RELEASELEVELLOG "${${flagVariable}_RELEASE}")
endforeach()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug,DebugLevelLog>:Debug>DLL")
set_property(GLOBAL PROPERTY DEBUG_CONFIGURATIONS Debug DebugLevelLog)
if(WIN32)
  # Qt and the vcpkg libraries come as Debug and Release builds; the log profiles use the matching one. The empty
  # entry lets imported targets without configurations (libusb) fall back to their only location.
  set(CMAKE_MAP_IMPORTED_CONFIG_DEBUGLEVELLOG "Debug;")
  set(CMAKE_MAP_IMPORTED_CONFIG_RELEASELEVELLOG "Release;")
endif()

string(TOLOWER "${CMAKE_SYSTEM_NAME}" headunitSystem)
if(MSVC)
  string(TOLOWER "${MSVC_CXX_ARCHITECTURE_ID}" headunitProcessor)
else()
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" headunitProcessor)
endif()
if(headunitProcessor MATCHES "^(x86_64|amd64|x64)$")
  set(headunitArchitecture x64)
elseif(headunitProcessor MATCHES "^(aarch64|arm64)$")
  set(headunitArchitecture arm64)
elseif(headunitProcessor MATCHES "^(i[3-6]86|x86)$")
  set(headunitArchitecture x86)
elseif(headunitProcessor MATCHES "^arm")
  set(headunitArchitecture arm)
else()
  message(FATAL_ERROR "Unknown target architecture '${headunitProcessor}'")
endif()
set(headunitProfileDirectory
  "$<IF:$<CONFIG:DebugLevelLog>,debug_level_log,$<IF:$<CONFIG:ReleaseLevelLog>,release_level_log,$<LOWER_CASE:$<CONFIG>>>>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin/${headunitSystem}${headunitArchitecture}/${headunitProfileDirectory}")
