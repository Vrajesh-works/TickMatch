include(GoogleTest)
find_package(Threads REQUIRED)

add_executable(tickmatch_tests
    test_order_book.cpp
    test_id_index.cpp
    test_fuzz.cpp
    test_spsc_queue.cpp
    test_latency_histogram.cpp
    test_lobster.cpp
    test_replay.cpp)
target_link_libraries(tickmatch_tests PRIVATE tickmatch::tickmatch tickmatch_warnings GTest::gtest_main
                                              Threads::Threads)
gtest_discover_tests(tickmatch_tests DISCOVERY_TIMEOUT 60)

# Integration: replay the committed synthetic LOBSTER sample end to end.
if(TICKMATCH_BUILD_APPS)
    foreach(mode inline pipeline)
        add_test(NAME integration.replay_sample.${mode}
                 COMMAND ${CMAKE_COMMAND}
                         -DREPLAY=$<TARGET_FILE:tickmatch_replay>
                         -DINPUT=${PROJECT_SOURCE_DIR}/data/sample/synthetic_messages.csv
                         -DMODE=${mode}
                         -DOUT=${CMAKE_CURRENT_BINARY_DIR}/summary_${mode}.json
                         -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/check_replay.cmake)
        set_tests_properties(integration.replay_sample.${mode} PROPERTIES LABELS integration)
    endforeach()
endif()
