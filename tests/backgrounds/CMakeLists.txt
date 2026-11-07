tarn_add_test(backgrounds)
# Stored reference images the Background tests compare rendered PNGs with.
target_compile_definitions(test_backgrounds PRIVATE TARN_BACKGROUNDS_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
