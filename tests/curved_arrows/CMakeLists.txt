tarn_add_test(curved_arrows)
# Stored reference images: curved arrows in every style, compared pixel for pixel.
target_compile_definitions(test_curved_arrows PRIVATE TARN_CURVED_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
