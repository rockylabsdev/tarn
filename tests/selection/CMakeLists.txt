tarn_add_test(selection)
# Stored reference images: edit, undo or redo, export, compare.
target_compile_definitions(test_selection PRIVATE TARN_SELECTION_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
