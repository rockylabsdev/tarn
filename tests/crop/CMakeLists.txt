tarn_add_test(crop LIBS tarn_ui)
# Stored reference images the Crop, rotate, flip, output scale and widen-again exports are compared with.
target_compile_definitions(test_crop PRIVATE TARN_CROP_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
