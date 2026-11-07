tarn_add_test(daemon)
# These tests run the real `tarn` binary against the scratch environment.
add_dependencies(test_daemon tarn)
target_compile_definitions(test_daemon PRIVATE TARN_BIN="$<TARGET_FILE:tarn>")
