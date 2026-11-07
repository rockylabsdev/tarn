tarn_add_test(pin)
# tarn.lua is read to check that its Pin rule matches the title Tarn gives a Pin.
target_compile_definitions(test_pin PRIVATE TARN_HYPR_LUA="${CMAKE_SOURCE_DIR}/hypr/tarn.lua")
