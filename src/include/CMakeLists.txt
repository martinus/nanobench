# nb only exists when the test suite is being built; the header itself needs no target.
if (NANOBENCH_BUILD_TEST)
    target_sources_local(nb PUBLIC nanobench.h)
    target_include_directories(nb PUBLIC ${CMAKE_CURRENT_LIST_DIR})
endif()
