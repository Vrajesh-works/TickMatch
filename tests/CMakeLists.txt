include(GoogleTest)
find_package(Threads REQUIRED)

add_executable(tickmatch_tests
    test_order_book.cpp
    test_id_index.cpp
    test_fuzz.cpp
    test_spsc_queue.cpp
    test_latency_histogram.cpp
    test_lobster.cpp)
target_link_libraries(tickmatch_tests PRIVATE tickmatch::tickmatch tickmatch_warnings GTest::gtest_main
                                              Threads::Threads)
gtest_discover_tests(tickmatch_tests DISCOVERY_TIMEOUT 60)
