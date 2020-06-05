#if 0
#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <chrono>
#include <random>
#include <thread>

TEST_CASE("comparison_slow") {
    ankerl::nanobench::Bench().title("framework comparison").run("sleep 10ms", [&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });
}

TEST_CASE("comparison_fluctuating_v1") {
    std::random_device dev;
    std::mt19937_64 rng(dev());
    ankerl::nanobench::Bench().run("random fluctuations", [&] {
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
    ankerl::nanobench::Bench().minEpochIterations(5000).run("random fluctuations", [&] {
        // each run, perform a random number of rng calls
        auto iterations = rng() & UINT64_C(0xff);
        for (uint64_t i = 0; i < iterations; ++i) {
            (void)rng();
        }
    });
}

#endif