#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

TEST_CASE("comparison_fast_v2") {
    uint64_t x = 1;
    ankerl::nanobench::Bench()
        .run("x += x",
             [&]() {
                 x += x;
             })
        .doNotOptimizeAway(x);
}
