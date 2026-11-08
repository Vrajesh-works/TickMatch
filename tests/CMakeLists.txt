include(GoogleTest)

add_executable(tickmatch_tests
    test_order_book.cpp
    test_id_index.cpp)
target_link_libraries(tickmatch_tests PRIVATE tickmatch::tickmatch tickmatch_warnings GTest::gtest_main)
gtest_discover_tests(tickmatch_tests DISCOVERY_TIMEOUT 60)
