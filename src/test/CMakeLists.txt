add_subdirectory(app)
add_subdirectory(thirdparty)

target_include_directories(nb PRIVATE ${CMAKE_CURRENT_LIST_DIR})
