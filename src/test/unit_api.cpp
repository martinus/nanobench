#include <app/doctest.h>

#include <nanobench.h>

#include <cmath>

TEST_CASE("incorrect1") {
    // compiler optimizes sin() away, because it is unused
    ankerl::nanobench([&] { (void)std::sin(2.32); });
}
TEST_CASE("incorrect2") {
    // compiler can still calculate sin(2.32) at compile time and replace it with the number
    ankerl::nanobench([&] { ankerl::noop(std::sin(2.32)); });
}

TEST_CASE("incorrect3") {
    // we produce a side effect by always modifying x, but the result is never used so the compiler
    // might still optimize it away
    double x = 123.4;
    ankerl::nanobench([&] { x = std::sin(x); });
}

TEST_CASE("simplest_api") {
    // correct: std::sin() produces a side effect, and after benchmark the result is checked.
    double x = 123.4;
    ankerl::nanobench([&] { x = std::sin(x); }).noop(x);
}

TEST_CASE("unit_api") {
    double x = 123.4;
    ankerl::nanobench("sin()").run([&] { x = std::sin(x); }).noop(x);
}
