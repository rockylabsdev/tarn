tarn_add_test(callout LIBS tarn_ui)
# Stored reference images: leaders against box edges from every side, exported.
target_compile_definitions(test_callout PRIVATE TARN_CALLOUT_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
