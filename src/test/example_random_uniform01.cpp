#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <random>

TEST_CASE("example_random_uniform01") {
    ankerl::nanobench::Config cfg;
    cfg.title("random double in [0, 1(");
    cfg.relative(true);

    std::random_device dev;
    std::default_random_engine defaultRng(dev());
    double d = 0;
    cfg.run("std::default_random_engine & std::uniform_real_distribution",
            [&] { d += std::uniform_real_distribution<>{}(defaultRng); })
        .doNotOptimizeAway(d);

    ankerl::nanobench::Rng nanobenchRng;
    d = 0;
    cfg.run("ankerl::nanobench::Rng & std::uniform_real_distribution", [&] { d += std::uniform_real_distribution<>{}(nanobenchRng); })
        .doNotOptimizeAway(d);

    d = 0;
    cfg.run("nanobenchRng.uniform01()", [&] { d += nanobenchRng.uniform01(); }).doNotOptimizeAway(d);
}
