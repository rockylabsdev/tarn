tarn_add_test(color LIBS tarn_ui)
# Stored reference images the recolor and resize exports are compared with.
target_compile_definitions(test_color PRIVATE TARN_COLOR_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
