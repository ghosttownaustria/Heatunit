set(HEADUNIT_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/../.tools/vcpkg/installed/x64-windows" CACHE PATH "Prepared native dependencies")
list(PREPEND CMAKE_PREFIX_PATH "${HEADUNIT_DEPS}")
find_package(Protobuf CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS asio endian)
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
target_link_libraries(headunit_aasdk PUBLIC protobuf::libprotobuf Boost::asio Boost::endian OpenSSL::SSL OpenSSL::Crypto)
target_compile_definitions(headunit_aasdk PUBLIC _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX BOOST_ALL_NO_LIB)
if(MSVC)
  target_compile_options(headunit_aasdk PUBLIC /utf-8 /Zc:__cplusplus /bigobj)
  target_link_libraries(headunit_aasdk PUBLIC ws2_32 crypt32)
endif()
find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h PATHS "${HEADUNIT_DEPS}/include" REQUIRED)
add_library(headunit_video src/video/VideoDecoder.cpp)
target_include_directories(headunit_video PUBLIC src PRIVATE "${AVCODEC_INCLUDE_DIR}")
foreach(COMPONENT avcodec avutil swscale)
  find_library(${COMPONENT}_RELEASE NAMES ${COMPONENT} PATHS "${HEADUNIT_DEPS}/lib" NO_DEFAULT_PATH REQUIRED)
  find_library(${COMPONENT}_DEBUG NAMES ${COMPONENT} PATHS "${HEADUNIT_DEPS}/debug/lib" NO_DEFAULT_PATH REQUIRED)
  target_link_libraries(headunit_video PRIVATE optimized "${${COMPONENT}_RELEASE}" debug "${${COMPONENT}_DEBUG}")
endforeach()
