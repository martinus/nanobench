# Consumes an *installed* nanobench through find_package, which is the half of the top-level
# CMakeLists.txt (install, export, nanobenchConfig.cmake) that nothing else ever builds.
cmake_minimum_required(VERSION 3.10)
project(nanobench_installed_consumer LANGUAGES CXX)

find_package(nanobench REQUIRED)

add_executable(consumer-installed ../consumer.cpp)
target_link_libraries(consumer-installed PRIVATE nanobench::nanobench)
