cmake_minimum_required(VERSION 3.5)
project(nanobench LANGUAGES CXX)

set(NB_cxx_standard "14" CACHE STRING "C++ standard, e.g. 11, 14, 17")

set(CMAKE_CXX_STANDARD ${NB_cxx_standard})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# configuration see .clang-tidy
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_CLANG_TIDY clang-tidy)
endif()

add_executable(nb "")
add_subdirectory(src)
add_compile_flags_target(nb)

target_sources_local(nb PUBLIC .clang-tidy)
