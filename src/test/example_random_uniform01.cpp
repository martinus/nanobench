#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <random>

TEST_CASE("example_random_uniform01") {
    ankerl::nanobench::Config cfg;
    cfg.title("random double in [0, 1(");

    std::default_random_engine defaultRng;
    double d = 0;
    auto baseline = cfg.run("std::default_random_engine & std::uniform_real_distribution",
                            [&] { d += std::uniform_real_distribution<>{}(defaultRng); })
                        .doNotOptimizeAway(d);

    cfg.relative(baseline);

    ankerl::nanobench::Rng nanobenchRng;
    d = 0;
    cfg.run("ankerl::nanobench::Rng & std::uniform_real_distribution", [&] { d += std::uniform_real_distribution<>{}(nanobenchRng); })
        .doNotOptimizeAway(d);

    d = 0;
    cfg.run("nanobenchRng.uniform01()", [&] { d += nanobenchRng.uniform01(); }).doNotOptimizeAway(d);
}
