add_subdirectory(cmake)
if(NANOBENCH_BUILD_TEST)
  # both only add sources and include dirs to the nb test executable
  add_subdirectory(include)
  add_subdirectory(test)
endif()
