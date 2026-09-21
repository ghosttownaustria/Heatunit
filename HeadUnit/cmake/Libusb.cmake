# libusb as the target `headunit_libusb` (link it, and the header <libusb.h> is found).
# Windows uses the vendored 1.0.30 DLL (third_party/libusb); Linux uses the system library through pkg-config
# (libusb-1.0-0-dev, needs libusb 1.0.16 or newer).
add_library(headunit_libusb INTERFACE)
if(WIN32)
  add_library(libusb SHARED IMPORTED)
  set_target_properties(libusb PROPERTIES
    IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libusb/x64/libusb-1.0.lib"
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libusb/x64/libusb-1.0.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libusb/include")
  target_link_libraries(headunit_libusb INTERFACE libusb)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
  target_link_libraries(headunit_libusb INTERFACE PkgConfig::LIBUSB)
endif()
