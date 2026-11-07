tarn_add_test(pixelate)
# Stored reference images the Pixelate and Spotlight exports are compared with.
target_compile_definitions(test_pixelate PRIVATE TARN_PIXELATE_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
