# tarn-clip: the detached clipboard helper (see clip_offers.h, main.cpp).
# Plain C++ and libwayland-client; no Qt.
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
set(EXT_DC_XML /usr/share/wayland-protocols/staging/ext-data-control/ext-data-control-v1.xml)
if(NOT EXISTS ${EXT_DC_XML})
  message(FATAL_ERROR "wayland-protocols is missing ${EXT_DC_XML}")
endif()

set(gen ${CMAKE_CURRENT_BINARY_DIR})
add_custom_command(
  OUTPUT ${gen}/ext-data-control-v1-client.h ${gen}/ext-data-control-v1.c
  COMMAND ${WAYLAND_SCANNER} client-header ${EXT_DC_XML} ${gen}/ext-data-control-v1-client.h
  COMMAND ${WAYLAND_SCANNER} private-code ${EXT_DC_XML} ${gen}/ext-data-control-v1.c
  DEPENDS ${EXT_DC_XML})

# The part with no Wayland in it: which types are offered, and their bytes.
add_library(tarn_clip_offers STATIC clip_offers.cpp clip_offers.h)
target_include_directories(tarn_clip_offers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)

enable_language(C)
add_executable(tarn-clip main.cpp ${gen}/ext-data-control-v1.c)
target_include_directories(tarn-clip PRIVATE ${gen})
target_link_libraries(tarn-clip PRIVATE tarn_clip_offers PkgConfig::WAYLAND_CLIENT)
# Next to `tarn`, where the daemon looks for it when it is not on PATH.
set_target_properties(tarn-clip PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/app)
install(TARGETS tarn-clip RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
