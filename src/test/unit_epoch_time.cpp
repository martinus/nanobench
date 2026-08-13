#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <chrono>
#include <cstdint>

// How long an epoch runs is the core of nanobench's measurement loop: the
// target is the clock resolution times clockResolutionMultiple, clamped into
// [minEpochTime, maxEpochTime], and if those two conflict the minimum wins.
// All three of those setters had no test, so the clamping could have been
// dropped, inverted, or applied in the wrong order without anything noticing -
// the only symptom is a benchmark that takes a different amount of time and
// measures against a different amount of noise.
//
// These check the rules through the one thing they are observable in: how much
// time the run actually spends measuring. The bounds are deliberately loose -
// a broken clamp changes the runtime by one or two orders of magnitude, so
// there is no need to be tight about it and no reason to be flaky on a busy
// machine.
namespace {

// Total measured time of the last benchmark, in seconds: per epoch, the time
// per iteration times the number of iterations, summed.
double measuredSeconds(ankerl::nanobench::Bench const& bench) {
    return bench.results().back().sumProduct(
        ankerl::nanobench::Result::Measure::iterations,
        ankerl::nanobench::Result::Measure::elapsed);
}

ankerl::nanobench::Bench quiet() {
    ankerl::nanobench::Bench bench;
    bench.output(nullptr).warmup(0).performanceCounters(false);
    return bench;
}

// Something cheap the compiler cannot remove, so the iteration count is what
// controls how long an epoch takes.
void tick(ankerl::nanobench::Rng& rng) {
    ankerl::nanobench::doNotOptimizeAway(rng());
}

} // namespace

// NOLINTNEXTLINE
TEST_CASE("unit_epoch_time_minimum_is_honored") {
    ankerl::nanobench::Rng rng(123);

    auto fast = quiet();
    fast.epochs(3).minEpochTime(std::chrono::milliseconds(1)).run("fast", [&] {
        tick(rng);
    });

    auto slow = quiet();
    slow.epochs(3).minEpochTime(std::chrono::milliseconds(20)).run("slow", [&] {
        tick(rng);
    });

    // An epoch is accepted once it reaches two thirds of the target, so three
    // epochs of a 20ms target take at least 40ms. Half of that is a floor no
    // machine can undercut while still honoring the setting.
    CHECK(measuredSeconds(slow) > 0.020);

    // and it really is the setting that did it, not the machine being slow
    CHECK(measuredSeconds(slow) > measuredSeconds(fast) * 3.0);
}

// NOLINTNEXTLINE
TEST_CASE("unit_epoch_time_maximum_caps_the_target") {
    ankerl::nanobench::Rng rng(123);

    // clockResolutionMultiple asks for an epoch of resolution * 100,000,000,
    // which is seconds per epoch on any machine. maxEpochTime is what keeps
    // this test from taking minutes.
    auto bench = quiet();
    bench.epochs(2)
        .clockResolutionMultiple(100000000)
        .maxEpochTime(std::chrono::milliseconds(5))
        .run("capped", [&] {
            tick(rng);
        });

    // two epochs of at most 5ms each; a generous bound, since the point is to
    // separate "capped" from "several seconds"
    CHECK(measuredSeconds(bench) < 1.0);
}

// NOLINTNEXTLINE
TEST_CASE("unit_epoch_time_minimum_wins_over_maximum") {
    ankerl::nanobench::Rng rng(123);

    // The two are clamped in order: first down to the maximum, then up to the
    // minimum. So when they contradict each other, the minimum is what the
    // epoch ends up with - which is the documented behavior and the safe one,
    // since too short an epoch measures mostly clock noise.
    auto bench = quiet();
    bench.epochs(3)
        .minEpochTime(std::chrono::milliseconds(20))
        .maxEpochTime(std::chrono::milliseconds(1))
        .run("min wins", [&] {
            tick(rng);
        });

    // if the maximum had won, three epochs would add up to about 3ms
    CHECK(measuredSeconds(bench) > 0.020);
}

// NOLINTNEXTLINE
TEST_CASE("unit_epoch_time_exact_iterations_ignore_the_clamps") {
    ankerl::nanobench::Rng rng(123);
    uint64_t calls = 0;

    // An exact epochIterations overrides any calculated count in every state,
    // so a long minEpochTime must not stretch the epoch by running more
    // iterations than were asked for.
    auto bench = quiet();
    bench.epochs(4)
        .epochIterations(3)
        .minEpochTime(std::chrono::milliseconds(50))
        .run("exact", [&] {
            ++calls;
            tick(rng);
        });

    CHECK(calls == UINT64_C(12));

    auto const& r = bench.results().back();
    REQUIRE(r.size() == 4U);
    for (size_t i = 0; i < r.size(); ++i) {
        INFO("epoch " << i);
        CHECK(r.get(i, ankerl::nanobench::Result::Measure::iterations) ==
              doctest::Approx(3.0));
    }

    // and the same with a warmup in front of it, which used to switch the run
    // into upscaling and lose the exact count
    calls = 0;
    auto warmed = quiet();
    warmed.epochs(2)
        .epochIterations(5)
        .warmup(11)
        .minEpochTime(std::chrono::milliseconds(50))
        .run("exact with warmup", [&] {
            ++calls;
            tick(rng);
        });
    CHECK(calls == UINT64_C(11 + 2 * 5));
}
