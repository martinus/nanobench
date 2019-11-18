cmake_minimum_required(VERSION 3.5)
project(nanobench LANGUAGES CXX)

set(NB_cxx_standard "11" CACHE STRING "C++ standard, e.g. 11, 14, 17")

set(CMAKE_CXX_STANDARD ${NB_cxx_standard})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# configuration see .clang-tidy
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_CLANG_TIDY clang-tidy)
endif()

add_executable(nb "")

if (NB_sanitizer)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # see https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html#silencing-unsigned-integer-overflow
        # Compile with -g and -fno-omit-frame-pointer to get proper debug information in your binary
        target_compile_options(nb PRIVATE -g)
        target_compile_options(nb PRIVATE -O2)
        target_compile_options(nb PRIVATE -fno-omit-frame-pointer)

        target_compile_options(nb PRIVATE -fsanitize=address)
        target_link_libraries(nb PRIVATE -fsanitize=address)

        target_compile_options(nb PRIVATE -fsanitize=undefined,float-divide-by-zero)
        target_link_libraries(nb PRIVATE -fsanitize=undefined,float-divide-by-zero)

        target_compile_options(nb PRIVATE -fsanitize=integer)
        target_compile_options(nb PRIVATE -fno-sanitize=unsigned-integer-overflow)
        target_link_libraries(nb PRIVATE -fsanitize=integer)
        target_link_libraries(nb PRIVATE -fno-sanitize=unsigned-integer-overflow)

        target_compile_options(nb PRIVATE -fsanitize=nullability)
        target_link_libraries(nb PRIVATE -fsanitize=nullability)
    endif()
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        # need to use gold linker, otherwise travis gets '/usr/bin/ld: --push-state: unknown option' error
        target_link_libraries(nb PRIVATE -fuse-ld=gold)

        target_compile_options(nb PRIVATE -g)
        target_compile_options(nb PRIVATE -O2)
        target_compile_options(nb PRIVATE -fno-omit-frame-pointer)

        target_compile_options(nb PRIVATE -fsanitize=undefined,float-divide-by-zero,float-cast-overflow)
        target_link_libraries(nb PRIVATE -fsanitize=undefined,float-divide-by-zero,float-cast-overflow)

        target_compile_options(nb PRIVATE -fsanitize=pointer-compare,pointer-subtract)
        target_link_libraries(nb PRIVATE -fsanitize=pointer-compare,pointer-subtract)

        target_compile_options(nb PRIVATE -fsanitize=address)
        target_link_libraries(nb PRIVATE -fsanitize=address)
    endif()
endif()

add_subdirectory(src)
add_compile_flags_target(nb)

target_sources_local(nb PUBLIC .clang-tidy)
