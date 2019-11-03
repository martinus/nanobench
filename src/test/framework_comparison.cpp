#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <chrono>
#include <random>
#include <thread>

TEST_CASE("comparison_fast_v1") {
    uint64_t x = 1;
    ankerl::nanobench::Config().run("x += x", [&]() ANKERL_NANOBENCH_NO_SANITIZE("integer") { x += x; });
}

TEST_CASE("comparison_fast_v2") {
    uint64_t x = 1;
    ankerl::nanobench::Config().run("x += x", [&]() ANKERL_NANOBENCH_NO_SANITIZE("integer") { x += x; }).doNotOptimizeAway(x);
}

TEST_CASE("comparison_fast") {
    uint64_t x = 1;
    ankerl::nanobench::Config()
        .title("framework comparison")
        .run("x += x", [&]() ANKERL_NANOBENCH_NO_SANITIZE("integer") { x += x; })
        .doNotOptimizeAway(x);
}

TEST_CASE("comparison_slow") {
    ankerl::nanobench::Config().title("framework comparison").run("sleep 10ms", [&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });
}

TEST_CASE("comparison_fluctuating_v1") {
    std::random_device dev;
    std::mt19937_64 rng(dev());
    ankerl::nanobench::Config().run("random fluctuations", [&] {
        // each run, perform a random number of rng calls
        auto iterations = rng() & UINT64_C(0xff);
        for (uint64_t i = 0; i < iterations; ++i) {
            (void)rng();
        }
    });
}

TEST_CASE("comparison_fluctuating_v2") {
    std::random_device dev;
    std::mt19937_64 rng(dev());
    ankerl::nanobench::Config().minEpochIterations(5000).run("random fluctuations", [&] {
        // each run, perform a random number of rng calls
        auto iterations = rng() & UINT64_C(0xff);
        for (uint64_t i = 0; i < iterations; ++i) {
            (void)rng();
        }
    });
}
