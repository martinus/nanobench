# 3.10 is the lowest version current CMake still accepts without a deprecation warning, and that
# warning shows up in the build of every project that pulls nanobench in (issue #137). Kept at the
# floor on purpose rather than moved to something modern: nothing here needs newer, and a header-only
# library has no business dictating the CMake version of the projects that consume it.
cmake_minimum_required(VERSION 3.10)
# VERSION is checked against the macros in nanobench.h by src/scripts/lint/lint-version.py, so it
# cannot drift from them silently.
project(
    nanobench
    VERSION 4.6.0
    DESCRIPTION "Simple, fast, accurate single-header microbenchmarking functionality for C++11/14/17/20"
    HOMEPAGE_URL "https://github.com/martinus/nanobench"
    LANGUAGES CXX)

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

    # Packagers want the header and the CMake package files without paying for a 36 translation unit
    # test binary they will never run.
    option(NANOBENCH_BUILD_TEST "Build nanobench's test suite" ON)

    # clang-tidy is not wired into the build any more: it ran for every clang compile, with whatever
    # version happened to be installed, and "Checks: *" in .clang-tidy gains checks with every
    # release - so a new clang stopped people from building the tests at all. The "lint" job of
    # .github/workflows/main.yml runs it with a pinned version instead. To get it back locally:
    #   cmake -S . -B build -DCMAKE_CXX_CLANG_TIDY=clang-tidy-18

    find_program(CCACHE_PROGRAM NAMES ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    endif()

    if (NANOBENCH_BUILD_TEST)
        add_executable(nb "")
    endif()

    if (NB_sanitizer AND NANOBENCH_BUILD_TEST)
        # Compile with -g and -fno-omit-frame-pointer to get proper debug information in your binary.
        # The sanitizer sets below differ per compiler; these three do not.
        target_compile_options(nb PRIVATE -g -O2 -fno-omit-frame-pointer)

        # MATCHES rather than STREQUAL: AppleClang has to take this branch too.
        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            # see https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html#silencing-unsigned-integer-overflow
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

            target_compile_options(nb PRIVATE -fsanitize=undefined,float-divide-by-zero,float-cast-overflow)
            target_link_libraries(nb PRIVATE -fsanitize=undefined,float-divide-by-zero,float-cast-overflow)

            target_compile_options(nb PRIVATE -fsanitize=pointer-compare,pointer-subtract)
            target_link_libraries(nb PRIVATE -fsanitize=pointer-compare,pointer-subtract)

            target_compile_options(nb PRIVATE -fsanitize=address)
            target_link_libraries(nb PRIVATE -fsanitize=address)
        endif()
    endif()

    add_subdirectory(src)
    if (NANOBENCH_BUILD_TEST)
        add_compile_flags_target(nb)
        target_sources_local(nb PUBLIC .clang-tidy)
    endif()


    include(GNUInstallDirs)
    set(
      NANOBENCH_INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
      CACHE PATH "Installation directory for the CMake package configuration files"
    )

    # Install library target
    add_library(nanobench STATIC ${PROJECT_SOURCE_DIR}/src/test/app/nanobench.cpp)
    target_compile_features(nanobench PUBLIC cxx_std_11)
    target_include_directories(nanobench
      PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/include>
        $<INSTALL_INTERFACE:include>
    )
    install(
      TARGETS nanobench
      EXPORT install_targets
    )

    # Install targets file
    install(EXPORT install_targets
      FILE
        ${PROJECT_NAME}Targets.cmake
      NAMESPACE
        ${PROJECT_NAME}::
      DESTINATION
        ${NANOBENCH_INSTALL_CONFIGDIR}
    )

    # Install ${PROJECT_NAME}Config.cmake and ${PROJECT_NAME}ConfigVersion.cmake
    include(CMakePackageConfigHelpers)
    write_basic_package_version_file(
      ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
      VERSION ${PROJECT_VERSION}
      COMPATIBILITY SameMajorVersion
    )
    configure_package_config_file(
      ${CMAKE_CURRENT_SOURCE_DIR}/cmake/${PROJECT_NAME}Config.cmake.in
      ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
      INSTALL_DESTINATION ${NANOBENCH_INSTALL_CONFIGDIR}
    )
    install(FILES
      ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
      ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
      DESTINATION ${NANOBENCH_INSTALL_CONFIGDIR}
    )

    # Install headers
    install(FILES src/include/nanobench.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
else()
    add_library(nanobench STATIC ${PROJECT_SOURCE_DIR}/src/test/app/nanobench.cpp)
    add_library(nanobench::nanobench ALIAS nanobench)
    set_property(TARGET nanobench PROPERTY CXX_STANDARD 17)
    target_include_directories(nanobench PUBLIC ${PROJECT_SOURCE_DIR}/src/include)
endif()
