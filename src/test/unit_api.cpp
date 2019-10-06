#include <app/doctest.h>

#include <nanobench.h>

#include <cmath>

TEST_CASE("incorrect1" * doctest::skip()) {
    // compiler optimizes sin() away, because it is unused
    ankerl::nanobench("std::sin(2.32)").run([&] { (void)std::sin(2.32); });
}
TEST_CASE("incorrect2") {
    // compiler can still calculate sin(2.32) at compile time and replace it with the number.
    // So we get a result, but it's still not what we want
    ankerl::nanobench("std::sin(2.32)").run([&] {
        ankerl::nanobench::do_not_optimize_away(std::sin(2.32));
    });
}

TEST_CASE("incorrect3" * doctest::skip()) {
    // we produce a side effect by always modifying x, but the result is never used so the compiler
    // might still optimize it away
    double x = 123.4;
    ankerl::nanobench("x = std::sin(x)").run([&] { x = std::sin(x); });
}

TEST_CASE("simplest_api") {
    // correct: std::sin() produces a side effect, and after benchmark the result is checked.
    double x = 123.4;
    ankerl::nanobench("x = std::sin(x) noop afterwards").run([&] { x = std::sin(x); });
    ankerl::nanobench::do_not_optimize_away(x);

    ankerl::nanobench("x = std::sin(x) always noop").run([&] {
        ankerl::nanobench::do_not_optimize_away(x = std::sin(x));
    });
}

TEST_CASE("unit_api") {
    std::string str(200000, 'x');

    size_t h = 0;
    ankerl::nanobench("std::hash").batch(str.size()).unit("B").run([&] {
        h += std::hash<std::string>{}(str);
        ++str[11];
    });
    ankerl::nanobench::do_not_optimize_away(h);
}
