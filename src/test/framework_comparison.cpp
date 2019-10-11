#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <chrono>
#include <random>
#include <thread>

TEST_CASE("framework_comparison") {
    ankerl::nanobench::Config cfg;
    cfg.title("framework comparison");

    // a benchmark that is extremely fast, and prone to be optimized away.
    uint64_t x = 123;
    cfg.run("x += x >> 1", [&] { x += x >> 1; }).doNotOptimizeAway(x);

    // a relatively slow test, should take 100ms
    cfg.run("sleep 10ms", [&] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });

    // an unreliable test where runtime fluctuates randomly.
    std::random_device dev;
    std::mt19937_64 rng(dev());
    cfg.run("random fluctuations", [&] {
        // each run, perform a random number of rng calls
        auto iterations = rng() & 0xfff;
        for (uint64_t i = 0; i < iterations; ++i) {
            rng();
        }
    });
}
