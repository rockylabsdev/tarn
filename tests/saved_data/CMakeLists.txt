tarn_add_test(saved_data LIBS tarn_ui)
# Files written by an earlier Tarn, kept byte for byte: the tests open them as a user's old data.
target_compile_definitions(test_saved_data PRIVATE TARN_SAVED_DATA_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
