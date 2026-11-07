tarn_add_test(watermark LIBS tarn_ui)
# Stored reference images the Watermark drawing is compared with.
target_compile_definitions(test_watermark PRIVATE TARN_WATERMARK_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
