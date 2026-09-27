# The protocol stack (AASDK with protobuf, OpenSSL and Boost.Asio) and the FFmpeg H.264 decoder.
# Windows finds them in the vcpkg tree that scripts/Prepare-Dependencies.ps1 builds; Linux uses the system
# packages (docs/linux.md lists them).
if(WIN32)
  set(HEADUNIT_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/../.tools/vcpkg/installed/x64-windows" CACHE PATH "Prepared native dependencies")
  list(PREPEND CMAKE_PREFIX_PATH "${HEADUNIT_DEPS}")
endif()
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Protobuf CONFIG QUIET)
if(NOT Protobuf_FOUND)
  find_package(Protobuf REQUIRED)
endif()
# vcpkg's Boost has a target per library; a distribution's Boost is one set of headers. Both ship a BoostConfig
# (Boost 1.70 and newer), so the FindBoost module, which newer CMake versions no longer have, is not needed.
find_package(Boost CONFIG QUIET COMPONENTS asio endian)
if(TARGET Boost::asio AND TARGET Boost::endian)
  set(HEADUNIT_BOOST_TARGETS Boost::asio Boost::endian)
else()
  find_package(Boost 1.74 CONFIG REQUIRED)
  set(HEADUNIT_BOOST_TARGETS Boost::headers)
endif()
set(SDK "${CMAKE_CURRENT_SOURCE_DIR}/third_party/aasdk")
set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED}")
file(GLOB_RECURSE PROTOS CONFIGURE_DEPENDS "${SDK}/protobuf/*.proto")
foreach(PROTO IN LISTS PROTOS)
  file(RELATIVE_PATH RELATIVE "${SDK}/protobuf" "${PROTO}")
  string(REGEX REPLACE "\\.proto$" ".pb.cc" CPP "${RELATIVE}")
  string(REGEX REPLACE "\\.proto$" ".pb.h" HEADER "${RELATIVE}")
  add_custom_command(OUTPUT "${GENERATED}/${CPP}" "${GENERATED}/${HEADER}"
    COMMAND protobuf::protoc "--cpp_out=${GENERATED}" "--proto_path=${SDK}/protobuf" "${PROTO}"
    DEPENDS ${PROTOS} protobuf::protoc VERBATIM)
  list(APPEND GENERATED_CPP "${GENERATED}/${CPP}")
endforeach()
file(GLOB SDK_CPP CONFIGURE_DEPENDS
  "${SDK}/src/Common/*.cpp" "${SDK}/src/Error/*.cpp" "${SDK}/src/IO/*.cpp"
  "${SDK}/src/Messenger/*.cpp" "${SDK}/src/Channel/Control/*.cpp"
  "${SDK}/src/Channel/MediaSink/Video/*.cpp" "${SDK}/src/Channel/MediaSink/Audio/*.cpp"
  "${SDK}/src/Channel/MediaSource/*.cpp" "${SDK}/src/Channel/InputSource/*.cpp"
  "${SDK}/src/Channel/SensorSource/*.cpp")
list(FILTER SDK_CPP EXCLUDE REGEX "\\.ut\\.cpp$")
add_library(headunit_aasdk STATIC ${SDK_CPP} ${GENERATED_CPP}
  "${SDK}/src/Channel/Channel.cpp" "${SDK}/src/Transport/SSLWrapper.cpp")
target_include_directories(headunit_aasdk PUBLIC "${SDK}/include" "${GENERATED}")
target_link_libraries(headunit_aasdk PUBLIC protobuf::libprotobuf ${HEADUNIT_BOOST_TARGETS} OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_compile_definitions(headunit_aasdk PUBLIC BOOST_ALL_NO_LIB)
if(WIN32)
  target_compile_definitions(headunit_aasdk PUBLIC _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
if(MSVC)
  target_compile_options(headunit_aasdk PUBLIC /utf-8 /Zc:__cplusplus /bigobj)
  target_link_libraries(headunit_aasdk PUBLIC ws2_32 crypt32)
endif()

# FFmpeg: H.264 for the phone's video (avcodec, swscale), and files and radio streams for the radio's own player
# (avformat with http/https, swresample).
add_library(headunit_ffmpeg INTERFACE)
if(WIN32)
  find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h PATHS "${HEADUNIT_DEPS}/include" REQUIRED)
  target_include_directories(headunit_ffmpeg INTERFACE "${AVCODEC_INCLUDE_DIR}")
  foreach(COMPONENT avcodec avformat avutil swresample swscale)
    find_library(${COMPONENT}_RELEASE NAMES ${COMPONENT} PATHS "${HEADUNIT_DEPS}/lib" NO_DEFAULT_PATH REQUIRED)
    find_library(${COMPONENT}_DEBUG NAMES ${COMPONENT} PATHS "${HEADUNIT_DEPS}/debug/lib" NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(headunit_ffmpeg INTERFACE optimized "${${COMPONENT}_RELEASE}" debug "${${COMPONENT}_DEBUG}")
  endforeach()
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswresample libswscale)
  target_link_libraries(headunit_ffmpeg INTERFACE PkgConfig::FFMPEG)
endif()

add_library(headunit_video src/video/VideoDecoder.cpp)
target_include_directories(headunit_video PUBLIC src)
target_link_libraries(headunit_video PRIVATE headunit_ffmpeg)

# The radio's own player (music files, internet radio); plays through the audio engines of headunit_usb.
add_library(headunit_media src/media/AudioPlayer.cpp)
target_link_libraries(headunit_media PUBLIC headunit_core PRIVATE headunit_ffmpeg)
