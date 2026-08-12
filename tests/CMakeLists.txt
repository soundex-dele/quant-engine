include(GoogleTest)

add_executable(quant_engine_tests quant_trading_test.cpp)
target_link_libraries(quant_engine_tests PRIVATE
    quant_engine GTest::gtest GTest::gtest_main)
target_compile_definitions(quant_engine_tests PRIVATE
    GRAPHFLOW_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
gtest_discover_tests(quant_engine_tests)

add_executable(quant_engine_public_api_smoke
    quant_trading_public_api_smoke.cpp)
target_link_libraries(quant_engine_public_api_smoke PRIVATE quant_engine_api)
target_compile_definitions(quant_engine_public_api_smoke PRIVATE
    GRAPHFLOW_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME quant_engine_public_api_smoke COMMAND quant_engine_public_api_smoke)
