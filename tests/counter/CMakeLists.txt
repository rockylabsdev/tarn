tarn_add_test(counter)
# Stored reference images: place Counters, delete or undo, export, compare.
target_compile_definitions(test_counter PRIVATE TARN_COUNTER_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
