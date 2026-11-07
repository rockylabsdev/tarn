tarn_add_test(doctor)
# These tests run the real `tarn` binary against the scratch environment.
add_dependencies(test_doctor tarn)
target_compile_definitions(test_doctor PRIVATE
    TARN_BIN="$<TARGET_FILE:tarn>"
    TARN_TRICKLE_DAEMON="${CMAKE_CURRENT_SOURCE_DIR}/../support/trickle_daemon.py")
