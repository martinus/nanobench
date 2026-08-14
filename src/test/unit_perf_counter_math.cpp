#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <cstdint>

// The ins/op, cyc/op, IPC, bra/op and miss% columns are what distinguish
// nanobench from a plain timer, and every number in them has been through the
// arithmetic below: extrapolate a counter the kernel had to time-share,
// subtract the cost of measuring, subtract what the measuring loop itself did,
// and take the loop's own branch back out of the branch count.
//
// None of it was verified, and none of it could be: it used to live inside the
// Linux-only counter class, reachable only through a real perf_event_open(),
// which fails in the containers CI runs in. So the whole path was untested on
// every platform - and a counter that is quietly 5% low looks exactly like a
// correct one. The arithmetic is now free functions in detail, compiled
// everywhere, and this is what checks them.
namespace {
namespace nb = ankerl::nanobench::detail;
} // namespace

// NOLINTNEXTLINE
TEST_CASE("unit_perf_div_rounded") {
    // rounds to nearest rather than truncating, which is what keeps a
    // per-iteration overhead from being systematically understated
    CHECK(nb::divRounded(UINT64_C(10), UINT64_C(4)) == UINT64_C(3));
    CHECK(nb::divRounded(UINT64_C(9), UINT64_C(4)) == UINT64_C(2));
    CHECK(nb::divRounded(UINT64_C(8), UINT64_C(4)) == UINT64_C(2));
    CHECK(nb::divRounded(UINT64_C(5), UINT64_C(2)) == UINT64_C(3));
    CHECK(nb::divRounded(UINT64_C(0), UINT64_C(7)) == UINT64_C(0));
    CHECK(nb::divRounded(UINT64_C(1), UINT64_C(1)) == UINT64_C(1));
    // exactly half rounds up
    CHECK(nb::divRounded(UINT64_C(2), UINT64_C(4)) == UINT64_C(1));
}

// NOLINTNEXTLINE
TEST_CASE("unit_perf_value_index") {
    // read_format is [nr, time_enabled, time_running, (value, id) * nr], so the
    // values start at 3 and come in pairs. Getting this wrong reads an event id
    // as a counter value, which is a plausible-looking enormous number.
    CHECK(nb::perfValueIndex(0) == 3U);
    CHECK(nb::perfValueIndex(1) == 5U);
    CHECK(nb::perfValueIndex(2) == 7U);
    CHECK(nb::perfValueIndex(5) == 13U);
}

// NOLINTNEXTLINE
TEST_CASE("unit_perf_scale_multiplexed") {
    // When more events are monitored than the hardware can count at once, the
    // kernel time-shares them and reports how long each was actually running.
    // Scaling by enabled/running extrapolates to the whole measurement, which
    // is what `perf stat` does.
    CHECK(nb::scaleMultiplexed(100, 200, 100) == UINT64_C(200));
    CHECK(nb::scaleMultiplexed(100, 300, 100) == UINT64_C(300));
    CHECK(nb::scaleMultiplexed(100, 150, 100) == UINT64_C(150));

    // active the whole time: nothing to extrapolate, and in particular not a
    // scaling *down*
    CHECK(nb::scaleMultiplexed(100, 100, 100) == UINT64_C(100));
    CHECK(nb::scaleMultiplexed(100, 50, 100) == UINT64_C(100));

    // never ran at all - dividing by zero is the alternative
    CHECK(nb::scaleMultiplexed(100, 200, 0) == UINT64_C(100));

    // rounds rather than truncates
    CHECK(nb::scaleMultiplexed(1, 3, 2) == UINT64_C(2));
    CHECK(nb::scaleMultiplexed(10, 3, 2) == UINT64_C(15));

    // a large but realistic count does not lose precision or overflow
    CHECK(nb::scaleMultiplexed(UINT64_C(1000000000), 4, 1) ==
          UINT64_C(4000000000));
}

// NOLINTNEXTLINE
TEST_CASE("unit_perf_correct_overhead") {
    // measuring overhead is a fixed cost per epoch; loop overhead is per
    // iteration
    CHECK(nb::correctOverhead(1000, 100, 2, 10) == UINT64_C(880));
    CHECK(nb::correctOverhead(1000, 0, 0, 10) == UINT64_C(1000));
    CHECK(nb::correctOverhead(1000, 1000, 0, 0) == UINT64_C(0));

    // a correction the caller did not ask for is a correction of zero, which is
    // how the two flags are expressed
    CHECK(nb::correctOverhead(500, 0, 3, 100) == UINT64_C(200));
    CHECK(nb::correctOverhead(500, 200, 0, 100) == UINT64_C(300));

    // Saturates at 0 instead of wrapping. This is the case that matters: on
    // unsigned arithmetic the alternative is not a small negative number, it is
    // 18 quintillion instructions per operation.
    CHECK(nb::correctOverhead(100, 200, 0, 0) == UINT64_C(0));
    CHECK(nb::correctOverhead(100, 50, 10, 100) == UINT64_C(0));
    CHECK(nb::correctOverhead(0, 0, 0, 1000) == UINT64_C(0));
}

// NOLINTNEXTLINE
TEST_CASE("unit_perf_loop_overhead_per_iteration") {
    // Calibration runs the same operation once and twice per iteration. If one
    // iteration costs the loop L plus the work W, the two runs measure
    // n*(W+L) and n*(2W+L), so twice the first minus the second leaves n*L -
    // the work cancels and only the loop is left.
    uint64_t const n = 100;
    uint64_t const work = 3;
    uint64_t const loop = 2;
    uint64_t const measuring = 7;
    auto const single = n * (work + loop) + measuring;
    auto const doubled = n * (2 * work + loop) + measuring;

    CHECK(nb::loopOverheadPerIteration(single, doubled, measuring, n) == loop);

    // no loop cost at all
    CHECK(nb::loopOverheadPerIteration(n * work, n * 2 * work, 0, n) ==
          UINT64_C(0));

    // saturates rather than wrapping when the second run cost more than twice
    // the first, which is what noise looks like here
    CHECK(nb::loopOverheadPerIteration(100, 1000, 0, 10) == UINT64_C(0));
    // ... and when the measuring overhead swamps both
    CHECK(nb::loopOverheadPerIteration(10, 20, 1000, 10) == UINT64_C(0));
}

// NOLINTNEXTLINE
TEST_CASE("unit_perf_correct_branch_instructions") {
    // the measuring loop branches once per iteration, plus once to leave
    CHECK(nb::correctBranchInstructions(1000, 100) == UINT64_C(899));
    CHECK(nb::correctBranchInstructions(102, 100) == UINT64_C(1));

    // an operation with no branches of its own reports none, rather than
    // wrapping to something enormous
    CHECK(nb::correctBranchInstructions(101, 100) == UINT64_C(0));
    CHECK(nb::correctBranchInstructions(100, 100) == UINT64_C(0));
    CHECK(nb::correctBranchInstructions(0, 100) == UINT64_C(0));
    CHECK(nb::correctBranchInstructions(0, 0) == UINT64_C(0));
}

// NOLINTNEXTLINE
TEST_CASE("unit_perf_correct_branch_misses") {
    // one miss is attributed to the loop mispredicting its own exit
    CHECK(nb::correctBranchMisses(100, 1000.0) == doctest::Approx(99.0));
    CHECK(nb::correctBranchMisses(3, 1000.0) == doctest::Approx(2.0));

    // there cannot be more misses than there were branches
    CHECK(nb::correctBranchMisses(2000, 1000.0) == doctest::Approx(999.0));

    // and never fewer than one, so miss% has a floor rather than going negative
    CHECK(nb::correctBranchMisses(0, 1000.0) == doctest::Approx(1.0));
    CHECK(nb::correctBranchMisses(1, 1000.0) == doctest::Approx(1.0));
    CHECK(nb::correctBranchMisses(2, 1000.0) == doctest::Approx(1.0));

    // Note the floor wins over the clamp: an operation whose branches all
    // belonged to the loop, so that none are left after the correction, is
    // still charged one miss. The markdown table hides it - miss% is only
    // computed when there are branches to divide by - but the raw measure is
    // what the json and csv templates export, so it shows up there as a
    // nonzero median(branchmisses) next to a median(branchinstructions) of 0.
    // Asserted here as the behaviour that exists, not as the behaviour that
    // would be obviously right.
    CHECK(nb::correctBranchMisses(5, 0.0) == doctest::Approx(1.0));
    CHECK(nb::correctBranchMisses(0, 0.0) == doctest::Approx(1.0));
}

// NOLINTNEXTLINE
TEST_CASE("unit_perf_counters_are_consistent_when_available") {
    // Where the kernel does hand out counters, the corrected values have to be
    // self-consistent. This is skipped rather than failed where perf_event_open
    // is refused - most containers and VMs - which is exactly why the
    // arithmetic above is tested separately.
    auto const& pc = ankerl::nanobench::detail::performanceCounters();
    if (!pc.has().branchInstructions || !pc.has().instructions) {
        MESSAGE("no hardware performance counters here, skipping");
        return;
    }

    ankerl::nanobench::Bench bench;
    ankerl::nanobench::Rng rng(123);
    uint64_t x = 0;
    bench.output(nullptr).warmup(0).epochs(11).epochIterations(10000).run(
        "counted", [&] {
            x += rng();
        });
    ankerl::nanobench::doNotOptimizeAway(x);

    using M = ankerl::nanobench::Result::Measure;
    auto const& r = bench.results().back();
    REQUIRE(r.has(M::instructions));

    // a few instructions per call, not zero and not thousands - the loop's own
    // cost has been taken out, but the operation's has not
    auto const insPerOp = r.median(M::instructions);
    INFO("instructions per op: " << insPerOp);
    CHECK(insPerOp > 0.0);
    CHECK(insPerOp < 1000.0);

    if (r.has(M::branchmisses) && r.has(M::branchinstructions)) {
        // Misses cannot exceed the branches they came from - except for the one
        // miss that is always attributed to the loop's own exit, which survives
        // even when the operation has no branches left after the loop's are
        // subtracted. That floor is per epoch, so per operation it is one
        // iteration's worth. See the note on correctBranchMisses above.
        auto const perIteration = 1.0 / 10000.0;
        CHECK(r.median(M::branchmisses) <=
              r.median(M::branchinstructions) + perIteration + 1e-9);
    }
}
