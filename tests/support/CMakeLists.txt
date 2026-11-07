# The test harness every area links (see tarn_add_test in cmake/TarnModules.cmake).
add_library(tarn_harness STATIC test_env.cpp test_env.h)
target_include_directories(tarn_harness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(tarn_harness PUBLIC
  TARN_STUB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stubs"
  TARN_SAFE_TOOLS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/safe-tools.txt")
target_link_libraries(tarn_harness PUBLIC tarn_core Qt6::Test)
