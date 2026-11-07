tarn_add_test(frame)
# Stored reference images the Frame tests compare rendered PNGs with.
target_compile_definitions(test_frame PRIVATE TARN_FRAME_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
