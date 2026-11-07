tarn_add_test(shapes)
# Stored reference images: draw, export, compare.
target_compile_definitions(test_shapes PRIVATE TARN_SHAPES_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
