#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <chrono>
#include <thread>
#include <vector>

// NOLINTNEXTLINE
TEST_CASE("unit_setup_time_is_excluded_from_measurement") {
    size_t setupCalls = 0;
    size_t benchCalls = 0;

    ankerl::nanobench::Bench bench;
    bench.output(nullptr)
        .warmup(0)
        .epochs(3)
        .epochIterations(1)
        .performanceCounters(false);
    bench
        .setup([&] {
            ++setupCalls;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        })
        .run([&] {
            ++benchCalls;
        });

    REQUIRE(setupCalls == 3);
    REQUIRE(benchCalls == 3);

    // Setup sleeps 50ms per epoch, but measured time should be near zero
    double const elapsedS = bench.results().back().median(
        ankerl::nanobench::Result::Measure::elapsed);
    REQUIRE(elapsedS < 0.01);
}

// NOLINTNEXTLINE
TEST_CASE("unit_setup_runs_before_each_epoch") {
    std::vector<char> callSequence;

    ankerl::nanobench::Bench bench;
    bench.output(nullptr)
        .warmup(0)
        .epochs(2)
        .epochIterations(3)
        .performanceCounters(false);
    bench
        .setup([&] {
            callSequence.push_back('S');
        })
        .run([&] {
            callSequence.push_back('R');
        });

    REQUIRE(callSequence ==
            std::vector<char>{'S', 'R', 'R', 'R', 'S', 'R', 'R', 'R'});
}

// The named overloads exist so that adding a setup to an existing benchmark
// does not force its call site to switch to name(). Also covers
// epochIterations(1), which is what the documentation points at for operations
// that cannot be repeated without re-running the setup. NOLINTNEXTLINE
TEST_CASE("unit_setup_named_run") {
    std::vector<char> callSequence;

    ankerl::nanobench::Bench bench;
    bench.output(nullptr)
        .warmup(0)
        .epochs(3)
        .epochIterations(1)
        .performanceCounters(false);
    bench
        .setup([&] {
            callSequence.push_back('S');
        })
        .run("named", [&] {
            callSequence.push_back('R');
        });

    REQUIRE(bench.results().size() == 1);
    REQUIRE(bench.results().back().config().mBenchmarkName == "named");
    REQUIRE(callSequence == std::vector<char>{'S', 'R', 'S', 'R', 'S', 'R'});

    std::string const asString = "named too";
    bench.setup([&] {}).run(asString, [] {});
    REQUIRE(bench.results().back().config().mBenchmarkName == asString);
}
