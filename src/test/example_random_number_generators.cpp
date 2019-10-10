#include <app/doctest.h>
#include <nanobench.h>

#include <random>

// Benchmarks how fast we can get 64bit random values from Rng
template <typename Rng>
ankerl::nanobench::Result bench(ankerl::nanobench::Config const& cfg, std::string name) {
    Rng rng;
    uint64_t x = 0;
    return cfg.run(name, [&] { x += std::uniform_int_distribution<uint64_t>{}(rng); }).doNotOptimizeAway(x);
}

TEST_CASE("example_random_number_generators") {
    // perform a few warmup calls, and since the runtime is not always stable for each
    // generator, increase the number of epochs to get more accurate numbers.
    auto cfg = ankerl::nanobench::Config().title("Random Number Generators").unit("uint64_t").warmup(10000).epochs(100);

    // Get the baseline against which the other random engines are compared
    auto baseline = bench<std::default_random_engine>(cfg, "std::default_random_engine");
    cfg.relative(baseline);

    // benchmark all remaining random engines
    bench<std::mt19937>(cfg, "std::mt19937");
    bench<std::mt19937_64>(cfg, "std::mt19937_64");
    bench<std::ranlux24_base>(cfg, "std::ranlux24_base");
    bench<std::ranlux48_base>(cfg, "std::ranlux48_base");
    bench<std::ranlux24>(cfg, "std::ranlux24_base");
    bench<std::ranlux48>(cfg, "std::ranlux48");
    bench<std::knuth_b>(cfg, "std::knuth_b");
    bench<ankerl::nanobench::Rng>(cfg, "ankerl::nanobench::Rng");
}

TEST_CASE("example_uniform01") {
    auto cfg = ankerl::nanobench::Config().title("random double in [0, 1(");

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
