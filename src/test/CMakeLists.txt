add_subdirectory(app)
add_subdirectory(thirdparty)

target_include_directories(nb PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_sources_local(nb PRIVATE
    example_atomic.cpp
    example_containers.cpp
    example_random_number_generators.cpp
    example_random_uniform01.cpp
    unit_api.cpp
)
