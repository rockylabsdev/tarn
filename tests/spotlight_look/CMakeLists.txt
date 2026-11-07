tarn_add_test(spotlight_look)
# Stored reference images of the Spotlight's rounded look.
target_compile_definitions(test_spotlight_look PRIVATE TARN_SPOTLIGHT_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
