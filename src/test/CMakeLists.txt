add_subdirectory(app)
add_subdirectory(thirdparty)

# SYSTEM, so that -Weverything does not report the vendored thirdparty/doctest/doctest.h. Suppressing
# those warnings one -Wno- at a time would also blind nanobench.h itself to them, forever.
target_include_directories(nb SYSTEM PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_sources_local(nb PRIVATE
    example_always_the_same.cpp
    example_atomic.cpp
    example_branch_misses.cpp
    example_complexity.cpp
    example_containers.cpp
    example_csv.cpp
    example_hide_output.cpp
    example_preconfigured_name.cpp
    example_pyperf.cpp
    example_random_number_generators.cpp
    example_random_uniform01.cpp
    example_random2.cpp
    example_shuffle.cpp
    tutorial_complexity_set.cpp
    tutorial_complexity_sort.cpp
    tutorial_context.cpp
    tutorial_fast_v1.cpp
    tutorial_fast_v2.cpp
    tutorial_fluctuating_v1.cpp
    tutorial_fluctuating_v2.cpp
    tutorial_mustache.cpp
    tutorial_render_simple.cpp    
    tutorial_slow_v1.cpp
    tutorial_slow_v2.cpp
    unit_api.cpp
    unit_bench_config.cpp
    unit_cold.cpp
    unit_columns.cpp
    unit_epoch_time.cpp
    unit_exact_iters_and_epochs.cpp
    unit_markdown_output.cpp
    unit_mdape.cpp
    unit_multi_output.cpp
    unit_number_format.cpp
    unit_relative_batch.cpp
    unit_render_commands.cpp
    unit_render_errors.cpp
    unit_result_statistics.cpp
    unit_rng.cpp
    unit_setup.cpp
    unit_string_view.cpp
    unit_templates.cpp
    unit_timeunit.cpp
)
