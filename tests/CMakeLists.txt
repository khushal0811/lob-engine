add_executable(lob_tests
    unit/main_test.cpp
    unit/test_price_level.cpp
    unit/test_order_book.cpp
    unit/test_matching_limit.cpp
    unit/test_matching_market.cpp
    unit/test_cancel.cpp
    unit/test_modify.cpp
    unit/test_replace.cpp
    unit/test_stop_orders.cpp
    unit/test_iceberg.cpp
    unit/test_phase4.cpp
    unit/test_integration.cpp
)

target_link_libraries(lob_tests
    PRIVATE
    lob_core
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(lob_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_TIMEOUT 30
)
