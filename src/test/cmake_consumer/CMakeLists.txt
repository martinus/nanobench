# Consumes nanobench through add_subdirectory, which takes the non-standalone branch of the
# top-level CMakeLists.txt - the one that only defines the nanobench::nanobench library.
cmake_minimum_required(VERSION 3.8)
project(nanobench_cmake_consumer LANGUAGES CXX)

add_subdirectory(../../.. nanobench_build)

add_executable(consumer consumer.cpp)
target_link_libraries(consumer PRIVATE nanobench::nanobench)
