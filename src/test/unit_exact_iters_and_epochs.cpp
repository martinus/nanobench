#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <cmath>
#include <cstdint>

// Benchmarks insertion and removal in multiple different containers.
// This uses a very fast random generator.
// NOLINTNEXTLINE
TEST_CASE("test_exact_iters_and_epochs") {

    ankerl::nanobench::Bench bench;
    size_t x = 0;
    bench.epochs(7).epochIterations(123).run("++x", [&] {
        ++x;
    });
    REQUIRE(x == bench.epochs() * bench.epochIterations());

    int y = 0;
    bench.epochs(1).epochIterations(77).run("++y", [&] {
        ++y;
    });
    REQUIRE(y == bench.epochs() * bench.epochIterations());

    // one benchmark
    REQUIRE(bench.results().size() == 2);

    {
        // check first result
        auto const& r = bench.results()[0];
        REQUIRE(r.size() == 7U); // each epoch has a result
        for (size_t i = 0; i < r.size(); ++i) {
            REQUIRE(static_cast<int>(std::lround(r.get(
                        i, ankerl::nanobench::Result::Measure::iterations))) ==
                    123);
        }
        REQUIRE(static_cast<int>(std::lround(
                    r.sum(ankerl::nanobench::Result::Measure::iterations))) ==
                7 * 123);
    }

    {
        // check second result
        auto const& r = bench.results()[1];
        REQUIRE(r.size() == 1U); // each epoch has a result
        for (size_t i = 0; i < r.size(); ++i) {
            REQUIRE(static_cast<int>(std::lround(r.get(
                        i, ankerl::nanobench::Result::Measure::iterations))) ==
                    77);
        }
        REQUIRE(static_cast<int>(std::lround(
                    r.sum(ankerl::nanobench::Result::Measure::iterations))) ==
                1 * 77);
    }
}

// epochIterations() promises that *exactly* that many iterations are run per
// epoch. That promise used to be broken as soon as warmup() was also set: the
// warmup epoch switched into upscaling instead of measuring, so the first
// epochs ran with a calculated number of iterations instead of the requested
// one - which also mixes wildly different epoch sizes into the median.
// NOLINTNEXTLINE
TEST_CASE("test_warmup_with_exact_iters") {
    uint64_t const warmup = 100;
    uint64_t const epochIterations = 1000;
    size_t const epochs = 5;

    ankerl::nanobench::Rng rng(123);
    uint64_t numCalls = 0;

    ankerl::nanobench::Bench bench;
    bench.epochs(epochs)
        .epochIterations(epochIterations)
        .warmup(warmup)
        .run("warmup + epochIterations", [&] {
            ++numCalls;
            ankerl::nanobench::doNotOptimizeAway(rng());
        });

    // warmup iterations are run, but not measured
    REQUIRE(numCalls == warmup + epochs * epochIterations);

    auto const& r = bench.results().front();
    REQUIRE(r.size() == epochs);
    for (size_t i = 0; i < r.size(); ++i) {
        REQUIRE(static_cast<int>(std::lround(r.get(
                    i, ankerl::nanobench::Result::Measure::iterations))) ==
                static_cast<int>(epochIterations));
    }
}
