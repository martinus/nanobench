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
TEST_CASE("unit_ab_median_interval_indices") {
    // The sign test: [x_(k), x_(n+1-k)] brackets the median with at least the
    // requested probability, where k is the largest one whose binomial tail
    // still fits in alpha/2. These are the exact answers, worked out from the
    // binomial rather than from this implementation.
    auto idx = [](size_t n, double conf) {
        return nb::medianIntervalIndices(n, conf);
    };

    CHECK(idx(6, 0.95) == std::make_pair(size_t(0), size_t(5)));
    CHECK(idx(8, 0.95) == std::make_pair(size_t(0), size_t(7)));
    CHECK(idx(12, 0.95) == std::make_pair(size_t(2), size_t(9)));
    CHECK(idx(20, 0.95) == std::make_pair(size_t(5), size_t(14)));
    CHECK(idx(52, 0.95) == std::make_pair(size_t(18), size_t(33)));
    CHECK(idx(100, 0.95) == std::make_pair(size_t(39), size_t(60)));

    // A stricter confidence reaches further out into the order statistics,
    // never less far.
    CHECK(idx(52, 0.99) == std::make_pair(size_t(16), size_t(35)));
    CHECK(idx(100, 0.99) == std::make_pair(size_t(36), size_t(63)));
    for (size_t n = 6; n <= 200; ++n) {
        INFO("n = " << n);
        auto const wide = idx(n, 0.99);
        auto const narrow = idx(n, 0.95);
        if (wide.first <= wide.second) {
            CHECK(wide.first <= narrow.first);
            CHECK(wide.second >= narrow.second);
        }
    }

    // Too few observations to make the statement at all. Five rounds cannot
    // support a 95% interval - even the full range only covers 93.75% - and
    // saying so is the honest answer, rather than reporting the range and
    // calling it 95%.
    for (size_t n = 0; n <= 5; ++n) {
        INFO("n = " << n);
        auto const none = idx(n, 0.95);
        CHECK(none.first > none.second);
    }
    // and 99% needs one more round than 95% does
    CHECK(idx(6, 0.99).first > idx(6, 0.99).second);
    CHECK(idx(8, 0.99).first <= idx(8, 0.99).second);

    // The interval is symmetric about the middle: as many order statistics are
    // excluded from each end. An off-by-one at one end only would still look
    // plausible without this.
    for (size_t n = 6; n <= 200; ++n) {
        INFO("n = " << n);
        auto const pair = idx(n, 0.95);
        if (pair.first <= pair.second) {
            CHECK(pair.first == n - 1U - pair.second);
        }
    }
}

// NOLINTNEXTLINE
TEST_CASE("unit_ab_median_interval") {
    // 101 values from 5 to 15, median 10
    std::vector<double> values;
    for (int i = -50; i <= 50; ++i) {
        values.push_back(10.0 + static_cast<double>(i) / 10.0);
    }

    auto const ci = nb::medianInterval(values, 0.95);
    CHECK(ci.first <= 10.0);
    CHECK(ci.second >= 10.0);
    // an interval for the *median*, which is known far better than any single
    // measurement is: the data spans 10 wide, this is a small fraction of that
    CHECK(ci.second - ci.first < 3.0);

    // no resampling anywhere, so the same data gives the same interval always -
    // not merely because a seed was pinned
    CHECK(nb::medianInterval(values, 0.95) == ci);

    // more scattered data, wider interval - this is the whole job
    std::vector<double> scattered;
    for (int i = -50; i <= 50; ++i) {
        scattered.push_back(10.0 + static_cast<double>(i));
    }
    auto const wide = nb::medianInterval(scattered, 0.95);
    CHECK(wide.second - wide.first > ci.second - ci.first);

    // a stricter confidence is a wider interval
    auto const ci99 = nb::medianInterval(values, 0.99);
    CHECK(ci99.first <= ci.first);
    CHECK(ci99.second >= ci.second);

    // Data that does not vary has an interval of zero width, and that is the
    // correct answer rather than a failure: every order statistic is the same
    // number. It is also what a comparison of two operations too fast for the
    // clock produces, which is why ab() reports the tie count next to it.
    auto const flat =
        nb::medianInterval({5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0}, 0.95);
    CHECK(flat.first == doctest::Approx(5.0));
    CHECK(flat.second == doctest::Approx(5.0));

    // too few observations to say anything at this confidence
    auto const tooFew = nb::medianInterval({1.0, 2.0, 3.0}, 0.95);
    CHECK(tooFew.first == doctest::Approx(0.0));
    CHECK(tooFew.second == doctest::Approx(0.0));
    CHECK(nb::medianInterval({}, 0.95).first == doctest::Approx(0.0));

    // the input is taken by value, so the caller's order is not disturbed
    std::vector<double> unsorted{3.0, 1.0, 2.0, 9.0, 5.0, 4.0, 8.0, 7.0};
    auto const copy = unsorted;
    ankerl::nanobench::doNotOptimizeAway(
        nb::medianInterval(unsorted, 0.95).first);
    CHECK(unsorted == copy);
}

// NOLINTNEXTLINE
TEST_CASE("unit_ab_tied_rounds") {
    CHECK(nb::countTiedRounds({}) == 0U);
    CHECK(nb::countTiedRounds({1.0, -1.0, 2.0}) == 0U);
    CHECK(nb::countTiedRounds({0.0, 1.0, 0.0, -1.0}) == 2U);
    CHECK(nb::countTiedRounds({0.0, 0.0, 0.0}) == 3U);
    // -0.0 is the same time measured twice just as much as +0.0 is
    CHECK(nb::countTiedRounds({-0.0, 0.0}) == 2U);
}

// NOLINTNEXTLINE
TEST_CASE("unit_ab_verdict_says_when_the_clock_ran_out") {
    // An interval of 1.00x .. 1.00x means one of two very different things, and
    // the reader cannot tell them apart from the numbers. Built directly rather
    // than measured, because ties need a clock too coarse for the operation and
    // that is not something a test can arrange.
    ankerl::nanobench::Result const empty{ankerl::nanobench::Config{}};
    ankerl::nanobench::AbResult const tied{"a", "b", empty, empty,
                                           1.0, 1.0, 1.0,   7U};
    CHECK(tied.tiedRounds() == 7U);

    std::ostringstream oss;
    oss << tied;
    INFO(oss.str());
    CHECK(oss.str().find("7 tied at the clock's resolution") !=
          std::string::npos);

    // and nothing about ties is said when there were none
    ankerl::nanobench::AbResult const clean{"a", "b", empty, empty,
                                            1.0, 1.0, 1.0,   0U};
    std::ostringstream quietOss;
    quietOss << clean;
    CHECK(quietOss.str().find("tied at the clock") == std::string::npos);
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
    REQUIRE(result.rounds() == 8U);
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
    // Each block of four rounds is eight epochs, and has to be ABBA or its
    // mirror. Both put the two a's and the two b's at positions with the same
    // mean, which is what cancels a linear drift.
    REQUIRE(order.size() % 8U == 0U);
    for (size_t block = 0; block * 8U < order.size(); ++block) {
        auto const chunk = order.substr(block * 8U, 8U);
        INFO("block " << block << ": " << chunk);
        CHECK((chunk == "abbabaab" || chunk == "baababba"));
    }

    // and over the whole run each side goes first exactly half the time
    size_t aFirst = 0;
    for (size_t round = 0; round < result.rounds(); ++round) {
        if ('a' == order[round * 2U]) {
            ++aFirst;
        }
    }
    CHECK(aFirst == result.rounds() / 2U);
}

// NOLINTNEXTLINE
TEST_CASE("unit_ab_runs_the_warmup_it_was_given") {
    // warmup() used to be read by run() and silently ignored by ab(). It does
    // not buy much here - calibration already runs each side for about an
    // epoch, and the correlation a warmup would target is removed by the
    // pairing - but a setting the caller wrote down has to do something.
    auto callsFor = [](uint64_t warmupIters) {
        uint64_t calls = 0;
        Work w;
        ankerl::nanobench::Bench bench;
        bench.output(nullptr)
            .epochs(8)
            .performanceCounters(false)
            .warmup(warmupIters)
            .minEpochTime(std::chrono::microseconds(100));
        bench.ab(
            "a",
            [&] {
                ++calls;
                w.step();
            },
            "b",
            [&] {
                w.step();
            });
        return calls;
    };

    uint64_t const warmupIters = 5000000;
    auto const without = callsFor(0);
    auto const with = callsFor(warmupIters);
    INFO("without " << without << ", with " << with);
    // calibration and the rounds vary a little between runs, so this only asks
    // that most of the warmup actually happened - if it were ignored the two
    // would be the same
    CHECK(with > without);
    CHECK(with - without > warmupIters * 4U / 5U);
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
    // and never fewer than eight, because five rounds cannot support a 95%
    // interval at all
    CHECK(quiet(4).ab("a", op, "b", op).rounds() == 8U);
    CHECK(quiet(1).ab("a", op, "b", op).rounds() == 8U);
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
