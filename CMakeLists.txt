cmake_minimum_required(VERSION 3.5)
project(nanobench LANGUAGES CXX)

# determine whether this is a standalone project or included by other projects
set(NANOBENCH_STANDALONE_PROJECT OFF)
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(NANOBENCH_STANDALONE_PROJECT ON)
endif()

if (NANOBENCH_STANDALONE_PROJECT)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # generate compile_commands.json

    set(NB_cxx_standard "11" CACHE STRING "C++ standard, e.g. 11, 14, 17")

    set(CMAKE_CXX_STANDARD ${NB_cxx_standard})
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    # configuration see .clang-tidy
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(CMAKE_CXX_CLANG_TIDY clang-tidy)
    endif()

    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        SET(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    endif(CCACHE_PROGRAM)

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
            target_link_libraries(nb PRIVATE -fsanitize=integer)

            target_compile_options(nb PRIVATE -fsanitize=nullability)
            target_link_libraries(nb PRIVATE -fsanitize=nullability)

            # we have to globally set the property here, so it actually works https://cmake.org/pipermail/cmake/2010-March/036020.html
            set_source_files_properties(
                src/test/tutorial_fast_v1.cpp 
                src/test/tutorial_fast_v2.cpp
                PROPERTIES COMPILE_FLAGS "-fno-sanitize=integer")

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
else()
    add_library(nanobench STATIC ${PROJECT_SOURCE_DIR}/src/test/app/nanobench.cpp)
    target_include_directories(nanobench INTERFACE ${PROJECT_SOURCE_DIR}/src/include)
endif()
