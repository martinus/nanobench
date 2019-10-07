#include <app/doctest.h>

#include <nanobench.h>

#include <cmath>

TEST_CASE("string") {
    std::string shortString = "hello";
    ankerl::nanobench::name("short string").run([&] { std::string copy(shortString); });

    std::string longString = "0123456789abcdefghijklmnopqrstuvwxyz";
    ankerl::nanobench::name("long string").run([&] { std::string copy(longString); });
}

TEST_CASE("incorrect1" * doctest::skip()) {
    // compiler optimizes sin() away, because it is unused
    ankerl::nanobench::name("std::sin(2.32)").run([&] { (void)std::sin(2.32); });
}
TEST_CASE("incorrect2") {
    // compiler can still calculate sin(2.32) at compile time and replace it with the number.
    // So we get a result, but it's still not what we want
    ankerl::nanobench::name("std::sin(2.32)").run([&] { ankerl::nanobench::doNotOptimizeAway(std::sin(2.32)); });
}

TEST_CASE("incorrect3" * doctest::skip()) {
    // we produce a side effect by always modifying x, but the result is never used so the compiler
    // might still optimize it away
    double x = 123.4;
    ankerl::nanobench::name("x = std::sin(x)").run([&] { x = std::sin(x); });
}

TEST_CASE("simplest_api") {
    // correct: std::sin() produces a side effect, and after benchmark the result is checked.
    double x = 123.4;
    ankerl::nanobench::name("x = std::sin(x) noop afterwards").run([&] { x = std::sin(x); });
    ankerl::nanobench::doNotOptimizeAway(x);

    ankerl::nanobench::name("x = std::sin(x) always noop").run([&] { ankerl::nanobench::doNotOptimizeAway(x = std::sin(x)); });
}

TEST_CASE("comparison") {
    ankerl::nanobench::tableHeader();

    double x = 1.0;
    auto baseline = ankerl::nanobench::name("x += x").batch(2).run([&] { x += x; }).doNotOptimizeAway(x);

    x = 1.0;
    ankerl::nanobench::name("std::sin(x)").relative(baseline).batch(2).run([&] { x += std::sin(x); }).doNotOptimizeAway(x);

    x = 1.0;
    ankerl::nanobench::name("std::log(x)").relative(baseline).run([&] { x += std::log(x); }).doNotOptimizeAway(x);

    x = 1.0;
    ankerl::nanobench::name("1/x").relative(baseline).run([&] { x += 1 / x; }).doNotOptimizeAway(x);

    ankerl::nanobench::name("noop").relative(baseline).run([&] {});

    x = 1.0;
    ankerl::nanobench::name("std::sqrt(x)").relative(baseline).run([&] { x += std::sqrt(x); }).doNotOptimizeAway(x);
}

TEST_CASE("unit_api") {
    std::string str(200000, 'x');

    size_t h = 0;
    ankerl::nanobench::name("std::hash").batch(str.size()).unit("B").run([&] {
        h += std::hash<std::string>{}(str);
        ++str[11];
    });
    ankerl::nanobench::doNotOptimizeAway(h);
}
