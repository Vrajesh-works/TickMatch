add_executable(tickmatch_bench bench_order_book.cpp)
target_link_libraries(tickmatch_bench PRIVATE tickmatch::tickmatch tickmatch_warnings
                                              benchmark::benchmark)
