add_executable(lob_bench bench_main.cpp benchmark_runner.cpp)
target_include_directories(lob_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lob_bench PRIVATE lob_core)
