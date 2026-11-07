tarn_add_test(text_tool LIBS tarn_ui)
# Stored reference images: each Text look and multi-line Text, exported.
target_compile_definitions(test_text_tool PRIVATE TARN_TEXT_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
