tarn_add_test(editor)
# Stored reference images the drawing tests compare exported PNGs with.
target_compile_definitions(test_editor PRIVATE TARN_EDITOR_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
