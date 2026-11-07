cmake_minimum_required(VERSION 3.28)
project(tarn VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Tarn has no C++20 modules. Scanning every file for them (CMake's default for
# C++20) costs a step per file, and ccache refuses those calls.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CTest) # BUILD_TESTING, on by default; a package build passes -DBUILD_TESTING=OFF

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Qml Quick)
if(BUILD_TESTING)
  find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
endif()
find_package(LayerShellQt REQUIRED)
find_package(tomlplusplus REQUIRED) # config.toml (Settings), state.toml (Remembered state), later presets.toml
qt_standard_project_setup(REQUIRES 6.8)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(TarnModules)

# Everything below discovers its own pieces: adding a module, a test area or a
# QML file never means editing a shared CMake list. See docs/agents/verify.md.
add_subdirectory(src/core)   # tarn_core: one subdirectory per module
add_subdirectory(src/ui)     # tarn_ui: QML module and the layer-shell surfaces
add_subdirectory(src/app)    # the `tarn` executable
if(BUILD_TESTING)
  add_subdirectory(tests)    # one test executable per area
endif()

# `cmake --install`: the binaries (tarn here, tarn-clip in its own folder), the
# launcher entry, and the Hyprland file and Waybar example under the doc folder.
install(FILES tarn.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES hypr/tarn.lua DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY examples/waybar DESTINATION ${CMAKE_INSTALL_DOCDIR})
