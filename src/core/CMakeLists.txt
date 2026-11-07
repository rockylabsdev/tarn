# The daemon's C++ core, below QML. It is an OBJECT library so that modules which
# register themselves with TARN_MODULE are never dropped by the linker.
add_library(tarn_core OBJECT)
target_include_directories(tarn_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tarn_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network tomlplusplus::tomlplusplus)

# One subdirectory per module (the Hyprland bridge, the Screen grabber, ...).
tarn_add_subdirectories(${CMAKE_CURRENT_SOURCE_DIR})
