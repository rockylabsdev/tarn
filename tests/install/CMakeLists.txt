# Install layout: `cmake --install` into a scratch folder puts every file where a
# package expects it. Not a Qt test, so no tarn_add_test.
add_test(NAME install_layout
    COMMAND ${CMAKE_COMMAND}
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DCMAKE_COMMAND=${CMAKE_COMMAND}
        -DSCRATCH=${CMAKE_CURRENT_BINARY_DIR}/prefix
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_install.cmake)
set_tests_properties(install_layout PROPERTIES TIMEOUT 120)
