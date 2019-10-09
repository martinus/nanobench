#include <app/doctest.h>

#include <nanobench.h>

#include <cmath>

TEST_CASE("string") {
    std::string shortString = "hello";
    ankerl::nanobench::run("short string", [&] { std::string copy(shortString); });

    std::string longString = "0123456789abcdefghijklmnopqrstuvwxyz";
    ankerl::nanobench::run("long string", [&] { std::string copy(longString); });
}

TEST_CASE("incorrect1" * doctest::skip()) {
    // compiler optimizes sin() away, because it is unused
    ankerl::nanobench::run("std::sin(2.32)", [&] { (void)std::sin(2.32); });
}
TEST_CASE("incorrect2") {
    // compiler can still calculate sin(2.32) at compile time and replace it with the number.
    // So we get a result, but it's still not what we want
    ankerl::nanobench::run("std::sin(2.32)", [&] { ankerl::nanobench::doNotOptimizeAway(std::sin(2.32)); });
}

TEST_CASE("incorrect3" * doctest::skip()) {
    // we produce a side effect by always modifying x, but the result is never used so the compiler
    // might still optimize it away
    double x = 123.4;
    ankerl::nanobench::run("x = std::sin(x)", [&] { x = std::sin(x); });
}

TEST_CASE("simplest_api") {
    // correct: std::sin() produces a side effect, and after benchmark the result is checked.
    double x = 123.4;
    ankerl::nanobench::run("x = std::sin(x) noop afterwards", [&] { x = std::sin(x); }).doNotOptimizeAway(x);

    ankerl::nanobench::run("x = std::sin(x) always noop", [&] { ankerl::nanobench::doNotOptimizeAway(x = std::sin(x)); });
}

TEST_CASE("comparison") {
    ankerl::nanobench::forceTableHeader();

    double x = 1.0;

    auto baseline = ankerl::nanobench::Config().run("x += x", [&] { x += x; }).doNotOptimizeAway(x);

    auto cfg = ankerl::nanobench::Config().relative(baseline);

    x = 1.123;
    cfg.run("std::sin(x)", [&] { x += std::sin(x); }).doNotOptimizeAway(x);

    // The compiler might be smart enough to optimize this away, since log(1) = 1.
    x = 1.123;
    cfg.run("std::log(x)", [&] { x += std::log(x); }).doNotOptimizeAway(x);

    x = 1.123;
    cfg.run("1/x", [&] { x += 1 / x; }).doNotOptimizeAway(x);

    cfg.run("noop", [&] {});

    x = 1.123;
    cfg.run("std::sqrt(x)", [&] { x += std::sqrt(x); }).doNotOptimizeAway(x);
}

TEST_CASE("unit_api") {
    std::string str(200000, 'x');

    size_t h = 0;
    ankerl::nanobench::Config()
        .batch(str.size())
        .unit("B")
        .run("std::hash",
             [&] {
                 h += std::hash<std::string>{}(str);
                 ++str[11];
             })
        .doNotOptimizeAway(h);
}
