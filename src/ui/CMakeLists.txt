# QML screens and the layer-shell surfaces that show them. Every .qml file in
# qml/ and every file in assets/ is compiled into the binary as a Qt resource
# (nothing is read from disk at run time).
file(GLOB qml_files CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
file(GLOB_RECURSE asset_files CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} assets/*)
file(GLOB ui_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

# Style.qml is a singleton (Tarn's radii, sizes and spacing).
# Popovers.qml is one too: the open Popovers, for the Editor's Esc.
set_source_files_properties(qml/Style.qml qml/Popovers.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(tarn_ui STATIC)
qt_add_qml_module(tarn_ui
  URI Tarn.Ui
  VERSION 1.0
  SOURCES ${ui_sources}
  QML_FILES ${qml_files}
  RESOURCES ${asset_files}
  OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Tarn/Ui
  NO_PLUGIN)
target_include_directories(tarn_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tarn_ui PUBLIC tarn_core Qt6::Quick Qt6::Qml LayerShellQt::Interface)
