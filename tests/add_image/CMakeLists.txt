tarn_add_test(add_image)
# Stored reference images: add an image, move it past the edge, export, compare.
target_compile_definitions(test_add_image PRIVATE TARN_ADD_IMAGE_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
