add_subdirectory(app)
add_subdirectory(thirdparty)

target_include_directories(nb PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_sources_local(nb PRIVATE
    example_always_the_same.cpp
    example_atomic.cpp
    example_branch_misses.cpp
    example_containers.cpp
    example_hide_output.cpp
    example_random_number_generators.cpp
    example_random_uniform01.cpp
    framework_comparison.cpp
    unit_api.cpp
)
