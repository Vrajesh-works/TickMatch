find_package(Threads REQUIRED)

add_library(tickmatch
    order_book.cpp
    latency_histogram.cpp
    lobster.cpp
    replay.cpp)
add_library(tickmatch::tickmatch ALIAS tickmatch)
target_include_directories(tickmatch PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(tickmatch PUBLIC cxx_std_20)
target_link_libraries(tickmatch PUBLIC Threads::Threads PRIVATE tickmatch_warnings)
