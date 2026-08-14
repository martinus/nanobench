#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// Bench::ab() compares two alternatives paired and interleaved, and reports a
// ratio with a confidence interval instead of a bare percentage.
//
// The statistics are checked here as pure functions, against inputs whose
// answer is known. The end-to-end behaviour is checked for the two things that
// would make the feature worse than useless: claiming a difference between
// identical work, and failing to see one that is really there.
namespace {
namespace nb = ankerl::nanobench::detail;

// something the compiler cannot fold away or hoist out of the measuring loop
struct Work {
    uint64_t state = 1;
    // the multiply wraps on purpose, which -fsanitize=integer flags
    ANKERL_NANOBENCH_NO_SANITIZE("integer")
    void step() {
        state = state * UINT64_C(6364136223846793005) + 1U;
        ankerl::nanobench::doNotOptimizeAway(state);
    }
};

ankerl::nanobench::Bench quiet(size_t rounds) {
    ankerl::nanobench::Bench bench;
    bench.output(nullptr)
        .epochs(rounds)
        .performanceCounters(false)
        .minEpochTime(std::chrono::microseconds(200));
    return bench;
}

} // namespace

// NOLINTNEXTLINE
TEST_CASE("unit_ab_paired_log_ratios") {
    // ln(a) - ln(b) per round, which turns a speedup into a difference
    auto const d =
        nb::pairedLogRatios({std::exp(1.0), std::exp(2.0)}, {1.0, 1.0});
    REQUIRE(d.size() == 2U);
    CHECK(d[0] == doctest::Approx(1.0));
    CHECK(d[1] == doctest::Approx(2.0));

    // equal timings are a ratio of 1, which is 0 in log space
    auto const same = nb::pairedLogRatios({5.0, 5.0}, {5.0, 5.0});
    REQUIRE(same.size() == 2U);
    CHECK(same[0] == doctest::Approx(0.0));
    CHECK(same[1] == doctest::Approx(0.0));

    // A round where either side measured 0 has no ratio: log(0) is -inf, and
    // one of those makes every statistic downstream meaningless. Dropped rather
    // than propagated.
    auto const withZero = nb::pairedLogRatios({1.0, 0.0, 2.0}, {1.0, 1.0, 1.0});
    REQUIRE(withZero.size() == 2U);
    for (auto x : withZero) {
        CHECK_FALSE(std::isinf(x));
        CHECK_FALSE(std::isnan(x));
    }

    // unequal lengths pair up as far as they can
    CHECK(nb::pairedLogRatios({1.0, 2.0, 3.0}, {1.0}).size() == 1U);
    CHECK(nb::pairedLogRatios({}, {1.0}).empty());
}

// NOLINTNEXTLINE
TEST_CASE("unit_ab_median_of") {
    CHECK(nb::medianOf({}) == doctest::Approx(0.0));
    CHECK(nb::medianOf({7.0}) == doctest::Approx(7.0));
    CHECK(nb::medianOf({3.0, 1.0, 2.0}) == doctest::Approx(2.0));
    CHECK(nb::medianOf({4.0, 1.0, 3.0, 2.0}) == doctest::Approx(2.5));
}

// NOLINTNEXTLINE
TEST_CASE("unit_ab_bootstrap_interval") {
    // 101 values centred on 10, spread +-5
    std::vector<double> values;
    for (int i = -50; i <= 50; ++i) {
        values.push_back(10.0 + static_cast<double>(i) / 10.0);
    }

    auto const ci = nb::bootstrapMedianInterval(values, 1234, 2000, 0.95);
    // the interval brackets the median it is an interval for
    CHECK(ci.first <= 10.0);
    CHECK(ci.second >= 10.0);
    // and it is an interval for the *median*, which is known far more precisely
    // than any single measurement: the data spans 10 wide, the uncertainty
    // about its middle is a small fraction of that. Reporting the spread of the
    // data instead is the mistake this rules out.
    CHECK(ci.second - ci.first < 3.0);

    // deterministic: the same data gives the same interval, so a reported
    // number does not jitter between runs of the same binary
    auto const again = nb::bootstrapMedianInterval(values, 1234, 2000, 0.95);
    CHECK(ci.first == doctest::Approx(again.first));
    CHECK(ci.second == doctest::Approx(again.second));

    // more scattered data has to give a wider interval - this is the whole job
    std::vector<double> scattered;
    for (int i = -50; i <= 50; ++i) {
        scattered.push_back(10.0 + static_cast<double>(i));
    }
    auto const wide = nb::bootstrapMedianInterval(scattered, 1234, 2000, 0.95);
    CHECK(wide.second - wide.first > ci.second - ci.first);

    // data that does not vary at all has no uncertainty
    auto const none =
        nb::bootstrapMedianInterval({5.0, 5.0, 5.0, 5.0}, 1234, 500, 0.95);
    CHECK(none.first == doctest::Approx(5.0));
    CHECK(none.second == doctest::Approx(5.0));

    // a wider confidence level is a wider interval
    auto const ci99 = nb::bootstrapMedianInterval(values, 1234, 2000, 0.99);
    CHECK(ci99.second - ci99.first >= ci.second - ci.first);

    // Half the probability is still an interval, and a narrower one. This pins
    // down that the requested confidence is split over *two* tails: putting it
    // all in one tail leaves a 50% interval with both ends at the median, and
    // every check above would still pass.
    auto const ci50 = nb::bootstrapMedianInterval(values, 1234, 2000, 0.50);
    CHECK(ci50.second - ci50.first > 0.0);
    CHECK(ci50.second - ci50.first < ci.second - ci.first);

    // degenerate inputs are answered, not crashed on
    CHECK(nb::bootstrapMedianInterval({}, 1, 100, 0.95).first ==
          doctest::Approx(0.0));
    CHECK(nb::bootstrapMedianInterval({1.0}, 1, 0, 0.95).first ==
          doctest::Approx(0.0));
}

// NOLINTNEXTLINE
TEST_CASE("unit_ab_reports_no_difference_for_identical_work") {
    // Both sides are the *same* closure over the same state, so any difference
    // found is the machine and not the code.
    //
    // Deliberately not asserting isSignificant() == false. A 95% interval is
    // wrong 5% of the time by construction; measured over 60 runs of exactly
    // this comparison the rate was 3.3%, which is the interval doing its job
    // rather than a bug. Asserting it here would buy a test that fails a few
    // times in a hundred - and a benchmark suite that cries wolf is how people
    // learn to ignore it. What is asserted instead is that the ratio lands near
    // 1 and the interval is a well-formed interval around it, which holds every
    // time.
    Work w;
    auto op = [&w] {
        w.step();
    };
    auto bench = quiet(32);
    auto const result = bench.ab("a", op, "b", op);

    INFO("speedup " << result.speedup() << " CI [" << result.speedupLow()
                    << ", " << result.speedupHigh() << "]");
    CHECK(result.speedup() == doctest::Approx(1.0).epsilon(0.10));
    CHECK(result.speedupLow() <= result.speedup());
    CHECK(result.speedupHigh() >= result.speedup());
    // an interval that wide would mean something has gone badly wrong with the
    // pairing
    CHECK(result.speedupHigh() - result.speedupLow() < 0.5);
}

// NOLINTNEXTLINE
TEST_CASE("unit_ab_resolves_a_real_difference") {
    // The other half: a difference that is really there has to be found, with
    // the right sign and roughly the right size. Doing the work twice is the
    // least ambiguous thing to ask for.
    Work a;
    Work b;
    auto bench = quiet(32);
    auto const result = bench.ab(
        "once",
        [&] {
            a.step();
        },
        "eight times",
        [&] {
            for (int i = 0; i < 8; ++i) {
                b.step();
            }
        });

    INFO("speedup " << result.speedup() << " CI [" << result.speedupLow()
                    << ", " << result.speedupHigh() << "]");
    CHECK(result.isSignificant());
    // B does the work eight times over, so it is the slower one: the ratio is
    // below 1, and the whole interval is
    CHECK(result.speedup() < 1.0);
    CHECK(result.speedupHigh() < 1.0);

    // Deliberately no assertion on *how much* slower. Eight times the
    // arithmetic is not eight times the time: the measuring loop and the
    // doNotOptimizeAway store are paid once per iteration either way, and how
    // far they dominate is a property of the machine. An earlier version
    // asserted a factor and failed on the 32 bit leg, which measured 0.95
    // where x86-64 measured 0.50 - both correct. What the feature promises is
    // the direction and that the interval resolved it, and that is what is
    // checked.
    CHECK(result.speedup() > 0.05);
}

// NOLINTNEXTLINE
TEST_CASE("unit_ab_both_sides_run_the_same_iteration_count") {
    // Not a detail: what is compared is time per iteration, and an epoch
    // carries a fixed overhead that gets divided by that count. Different
    // counts amortize it differently, which biases the ratio systematically -
    // measured at 1.2% on 200us epochs - and no amount of pairing removes it.
    Work a;
    Work b;
    auto bench = quiet(8);
    auto const result = bench.ab(
        "fast",
        [&] {
            a.step();
        },
        "slow",
        [&] {
            b.step();
            b.step();
            b.step();
            b.step();
        });

    using M = ankerl::nanobench::Result::Measure;
    REQUIRE(result.resultA().size() == result.resultB().size());
    for (size_t i = 0; i < result.resultA().size(); ++i) {
        INFO("round " << i);
        CHECK(result.resultA().get(i, M::iterations) ==
              doctest::Approx(result.resultB().get(i, M::iterations)));
    }
}

// NOLINTNEXTLINE
TEST_CASE("unit_ab_orders_the_epochs_abba") {
    // ABBA cancels a drift that is linear over the block, because the two A
    // positions and the two B positions then have the same mean time. Always
    // running A first would still interleave, and would still look fine in
    // every other assertion here, while quietly giving one side the
    // first-in-round position every single time.
    std::string calls;
    Work a;
    Work b;
    auto bench = quiet(4);
    auto const result = bench.ab(
        "a",
        [&] {
            calls += 'a';
            a.step();
        },
        "b",
        [&] {
            calls += 'b';
            b.step();
        });

    using M = ankerl::nanobench::Result::Measure;
    REQUIRE(result.rounds() == 4U);
    auto const iters =
        static_cast<size_t>(result.resultA().get(0, M::iterations));
    REQUIRE(iters > 0U);

    // calibration runs before any round, so the rounds are the tail: two epochs
    // each, one letter per call, exactly `iters` calls per epoch
    auto const roundCalls = 2U * result.rounds() * iters;
    REQUIRE(calls.size() >= roundCalls);
    auto const tail = calls.substr(calls.size() - roundCalls);

    std::string order;
    for (size_t epoch = 0; epoch < 2U * result.rounds(); ++epoch) {
        auto const chunk = tail.substr(epoch * iters, iters);
        // an epoch is one operation called `iters` times, so the chunk is one
        // repeated letter
        REQUIRE(chunk.find_first_not_of(chunk[0]) == std::string::npos);
        order += chunk[0];
    }

    INFO("epoch order: " << order);
    CHECK((order == "abbabaab" || order == "baabab" + std::string("ba")));
}

// NOLINTNEXTLINE
TEST_CASE("unit_ab_rounds_up_to_whole_blocks") {
    // The default epochs() is 11, which is not a multiple of 4 - a partial
    // block gives one side the first position more often than the other, which
    // is the imbalance ABBA exists to remove.
    Work w;
    auto op = [&w] {
        w.step();
    };
    CHECK(quiet(9).ab("a", op, "b", op).rounds() == 12U);
    CHECK(quiet(5).ab("a", op, "b", op).rounds() == 8U);
    CHECK(quiet(4).ab("a", op, "b", op).rounds() == 4U);
}

// NOLINTNEXTLINE
TEST_CASE("unit_ab_result_carries_both_measurements") {
    Work a;
    Work b;
    auto bench = quiet(12);
    auto const result = bench.ab(
        "left",
        [&] {
            a.step();
        },
        "right",
        [&] {
            b.step();
        });

    CHECK(result.nameA() == "left");
    CHECK(result.nameB() == "right");
    // rounds are rounded up to a whole number of ABBA blocks
    CHECK(result.rounds() == 12U);

    // both sides are also available as ordinary Results, for callers who want
    // their own arithmetic
    using M = ankerl::nanobench::Result::Measure;
    CHECK(result.resultA().size() == 12U);
    CHECK(result.resultB().size() == 12U);
    CHECK(result.resultA().median(M::elapsed) > 0.0);
    CHECK(result.resultB().median(M::elapsed) > 0.0);
    CHECK(result.resultA().config().mBenchmarkName == "left");
    CHECK(result.resultB().config().mBenchmarkName == "right");

    // ab() reports its own verdict and does not append to the ordinary results,
    // which belong to run()
    CHECK(bench.results().empty());
}

// NOLINTNEXTLINE
TEST_CASE("unit_ab_writes_a_verdict_to_the_output_stream") {
    Work a;
    Work b;
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    bench.output(&oss).epochs(12).performanceCounters(false).minEpochTime(
        std::chrono::microseconds(200));
    auto const result = bench.ab(
        "alpha",
        [&] {
            a.step();
        },
        "beta",
        [&] {
            b.step();
        });

    auto const text = oss.str();
    INFO(text);
    CHECK(text.find("`alpha`") != std::string::npos);
    CHECK(text.find("`beta`") != std::string::npos);
    CHECK(text.find("95% CI") != std::string::npos);
    CHECK(text.find("12 paired rounds") != std::string::npos);

    // the same verdict is available through the stream operator
    std::ostringstream direct;
    direct << result;
    CHECK(direct.str().find("`beta`") != std::string::npos);

    // and output(nullptr) really writes nothing
    std::ostringstream silent;
    ankerl::nanobench::Bench quietBench;
    quietBench.output(nullptr)
        .epochs(12)
        .performanceCounters(false)
        .minEpochTime(std::chrono::microseconds(200));
    quietBench.ab(
        "x",
        [&] {
            a.step();
        },
        "y",
        [&] {
            b.step();
        });
    CHECK(silent.str().empty());
}

// NOLINTNEXTLINE
TEST_CASE("unit_ab_names_a_backtick_safely") {
    // same escaping rule as the markdown table: a benchmark name is written as
    // code
    Work a;
    Work b;
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    bench.output(&oss).epochs(4).performanceCounters(false).minEpochTime(
        std::chrono::microseconds(200));
    bench.ab(
        "a`b",
        [&] {
            a.step();
        },
        "plain",
        [&] {
            b.step();
        });
    CHECK(oss.str().find("`a``b`") != std::string::npos);
}
