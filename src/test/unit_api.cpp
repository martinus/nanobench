#include <app/doctest.h>

#include <nanobench.h>

#include <cmath>

TEST_CASE("unit_api") {
    double x = 123.4;
    ankerl::nanobench("sin()").iters(1000000).run([&] { x = std::sin(x); }).noop(x);
}
