# One subdirectory per area, each calling tarn_add_test(<area>). tests/support
# is the harness (scratch XDG folders, stub executables first on PATH).
tarn_add_subdirectories(${CMAKE_CURRENT_SOURCE_DIR})
