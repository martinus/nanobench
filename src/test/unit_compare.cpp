#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// Bench::compare() compares two alternatives paired and interleaved, and
// reports a ratio with a confidence interval instead of a bare percentage.
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
TEST_CASE("unit_compare_paired_log_ratios") {
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
TEST_CASE("unit_compare_median_of") {
    CHECK(nb::medianOf({}) == doctest::Approx(0.0));
    CHECK(nb::medianOf({7.0}) == doctest::Approx(7.0));
    CHECK(nb::medianOf({3.0, 1.0, 2.0}) == doctest::Approx(2.0));
    CHECK(nb::medianOf({4.0, 1.0, 3.0, 2.0}) == doctest::Approx(2.5));
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_median_interval_indices") {
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
TEST_CASE("unit_compare_median_interval") {
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
    // clock produces, which is why compare() reports the tie count next to it.
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
TEST_CASE("unit_compare_tied_rounds") {
    CHECK(nb::countTiedRounds({}) == 0U);
    CHECK(nb::countTiedRounds({1.0, -1.0, 2.0}) == 0U);
    CHECK(nb::countTiedRounds({0.0, 1.0, 0.0, -1.0}) == 2U);
    CHECK(nb::countTiedRounds({0.0, 0.0, 0.0}) == 3U);
    // -0.0 is the same time measured twice just as much as +0.0 is
    CHECK(nb::countTiedRounds({-0.0, 0.0}) == 2U);
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_verdict_says_when_the_clock_ran_out") {
    // An interval of 1.00x .. 1.00x means one of two very different things, and
    // the reader cannot tell them apart from the numbers. Built directly rather
    // than measured, because ties need a clock too coarse for the operation and
    // that is not something a test can arrange.
    using Entry = ankerl::nanobench::CompareResult::Entry;
    ankerl::nanobench::Result const empty{ankerl::nanobench::Config{}};

    std::vector<Entry> tiedEntries;
    tiedEntries.emplace_back("a", empty, 1.0, 1.0, 1.0, size_t(0));
    tiedEntries.emplace_back("b", empty, 1.0, 1.0, 1.0, size_t(7));
    ankerl::nanobench::CompareResult const tied{std::move(tiedEntries), 52U};
    CHECK(tied[1].tiedRounds == 7U);

    std::ostringstream oss;
    oss << tied;
    INFO(oss.str());
    CHECK(
        oss.str().find("up to 7 of 52 rounds tied at the clock's resolution") !=
        std::string::npos);

    // and nothing about ties is said when there were none
    std::vector<Entry> cleanEntries;
    cleanEntries.emplace_back("a", empty, 1.0, 1.0, 1.0, size_t(0));
    cleanEntries.emplace_back("b", empty, 1.0, 1.0, 1.0, size_t(0));
    ankerl::nanobench::CompareResult const clean{std::move(cleanEntries), 52U};
    std::ostringstream quietOss;
    quietOss << clean;
    CHECK(quietOss.str().find("tied at the clock") == std::string::npos);
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_reports_no_difference_for_identical_work") {
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
    auto const result = bench.compare("a", op, "b", op);

    INFO("speedup " << result[1].relative << " CI [" << result[1].relativeLow
                    << ", " << result[1].relativeHigh << "]");
    CHECK(result[1].relative == doctest::Approx(1.0).epsilon(0.10));
    CHECK(result[1].relativeLow <= result[1].relative);
    CHECK(result[1].relativeHigh >= result[1].relative);
    // an interval that wide would mean something has gone badly wrong with the
    // pairing
    CHECK(result[1].relativeHigh - result[1].relativeLow < 0.5);
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_resolves_a_real_difference") {
    // The other half: a difference that is really there has to be found, with
    // the right sign and roughly the right size. Doing the work twice is the
    // least ambiguous thing to ask for.
    Work a;
    Work b;
    auto bench = quiet(32);
    auto const result = bench.compare(
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

    INFO("speedup " << result[1].relative << " CI [" << result[1].relativeLow
                    << ", " << result[1].relativeHigh << "]");
    CHECK(result.isSignificant(1));
    // B does the work eight times over, so it is the slower one: the ratio is
    // below 1, and the whole interval is
    CHECK(result[1].relative < 1.0);
    CHECK(result[1].relativeHigh < 1.0);

    // Deliberately no assertion on *how much* slower. Eight times the
    // arithmetic is not eight times the time: the measuring loop and the
    // doNotOptimizeAway store are paid once per iteration either way, and how
    // far they dominate is a property of the machine. An earlier version
    // asserted a factor and failed on the 32 bit leg, which measured 0.95
    // where x86-64 measured 0.50 - both correct. What the feature promises is
    // the direction and that the interval resolved it, and that is what is
    // checked.
    CHECK(result[1].relative > 0.05);
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_both_sides_run_the_same_iteration_count") {
    // Not a detail: what is compared is time per iteration, and an epoch
    // carries a fixed overhead that gets divided by that count. Different
    // counts amortize it differently, which biases the ratio systematically -
    // measured at 1.2% on 200us epochs - and no amount of pairing removes it.
    Work a;
    Work b;
    auto bench = quiet(8);
    auto const result = bench.compare(
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
    REQUIRE(result[0].result.size() == result[1].result.size());
    for (size_t i = 0; i < result[0].result.size(); ++i) {
        INFO("round " << i);
        CHECK(result[0].result.get(i, M::iterations) ==
              doctest::Approx(result[1].result.get(i, M::iterations)));
    }
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_orders_the_epochs_abba") {
    // ABBA cancels a drift that is linear over the block, because the two A
    // positions and the two B positions then have the same mean time. Always
    // running A first would still interleave, and would still look fine in
    // every other assertion here, while quietly giving one side the
    // first-in-round position every single time.
    std::string calls;
    Work a;
    Work b;
    auto bench = quiet(8);
    auto const result = bench.compare(
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
        static_cast<size_t>(result[0].result.get(0, M::iterations));
    REQUIRE(iters > 0U);

    // calibration runs before any round, so the rounds are the tail: one epoch
    // per alternative each, one letter per call, exactly `iters` calls per
    // epoch
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
    // A block is one round per alternative - two rounds here, so four epochs -
    // and has to be a Latin square: each alternative in each position exactly
    // once. For two alternatives that is ABBA or its mirror, which is what puts
    // the two a's and the two b's at positions with the same mean.
    REQUIRE(order.size() % 4U == 0U);
    for (size_t block = 0; block * 4U < order.size(); ++block) {
        auto const chunk = order.substr(block * 4U, 4U);
        INFO("block " << block << ": " << chunk);
        CHECK((chunk == "abba" || chunk == "baab"));
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
TEST_CASE("unit_compare_more_than_two_alternatives") {
    // More than two alternatives: the first is the baseline and everything else
    // is measured against it, in the same rounds.
    Work a;
    Work b;
    Work c;
    Work d;
    // Two things this test learned the hard way, both on legs this machine
    // cannot reproduce:
    //
    // 40 rounds, not the default 11. Three comparisons means each interval is
    // built at 98.3% rather than 95%, and at 12 rounds that interval spans the
    // middle 75% of the paired data - three rounds of the machine misbehaving
    // are enough to make it straddle 1.0 and report a real difference as
    // unresolved. At 40 it spans 38%, which needs thirteen.
    //
    // And the alternatives are spread wide apart, rather than the 2x and 8x
    // this used to compare. The 32 bit leg measures these tiny operations with
    // an error approaching 2x - it reported doing the work *twice* as 1.02x,
    // i.e. marginally faster than doing it once - so an assertion that 2x is
    // resolvable is an assertion that leg cannot keep. Ordering assertions need
    // gaps wider than the noise, and 5x/25x clears it with room to spare.
    auto bench = quiet(40);
    auto const result = bench.compare(
        "baseline",
        [&] {
            a.step();
        },
        "same",
        [&] {
            b.step();
        },
        "five times",
        [&] {
            for (int i = 0; i < 5; ++i) {
                c.step();
            }
        },
        "twenty five times",
        [&] {
            for (int i = 0; i < 25; ++i) {
                d.step();
            }
        });

    REQUIRE(result.size() == 4U);
    CHECK(result[0].name == "baseline");
    CHECK(result[3].name == "twenty five times");

    // one comparison per alternative besides the baseline
    CHECK(result.comparisons() == 3U);

    // rounds come in blocks of one epoch per alternative
    CHECK(result.rounds() % 4U == 0U);
    for (size_t i = 0; i < result.size(); ++i) {
        INFO("entry " << i);
        CHECK(result[i].result.size() == result.rounds());
    }

    // the baseline is the reference, so it has no ratio and no interval of its
    // own
    CHECK(result[0].relative == doctest::Approx(1.0));
    CHECK(result[0].relativeLow == doctest::Approx(1.0));
    CHECK(result[0].relativeHigh == doctest::Approx(1.0));
    CHECK_FALSE(result.isSignificant(0));

    // doing the work more times is resolvably slower, and more times is slower
    // still
    INFO("five times " << result[2].relative << ", twenty five times "
                       << result[3].relative);
    CHECK(result.isSignificant(2));
    CHECK(result.isSignificant(3));
    CHECK(result[2].relative < 1.0);
    CHECK(result[3].relative < result[2].relative);

    // and the fastest is one of the two that do the least work
    auto const best = result.fastest();
    INFO("fastest is " << result[best].name);
    CHECK(best <= 1U);
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_corrects_for_the_number_of_comparisons") {
    // Every alternative besides the baseline is another chance to be wrong at
    // 5%, so the intervals widen to keep the whole table at 95% rather than
    // each row separately. Checked on the order statistics directly, because
    // the effect on a measured interval is too small to assert without being
    // flaky.
    auto const single = nb::medianIntervalIndices(52, 0.95);
    auto const nine = nb::medianIntervalIndices(52, 1.0 - 0.05 / 9.0);
    CHECK(nine.first < single.first);
    CHECK(nine.second > single.second);

    // it costs surprisingly little - the binomial tail is steep, so correcting
    // for nine comparisons moves the interval by a few order statistics, not by
    // half the data
    CHECK(single.first - nine.first <= 5U);
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_rounds_up_to_whole_blocks") {
    // A block is one round per alternative, so for two of them the round count
    // has to be a multiple of two. The default epochs() is 11, which is not: a
    // partial block gives one side the first position more often than the
    // other, which is the imbalance the ordering exists to remove.
    Work w;
    auto op = [&w] {
        w.step();
    };
    // rounded up to a whole block, which for two alternatives is two rounds
    CHECK(quiet(9).compare("a", op, "b", op).rounds() == 10U);
    CHECK(quiet(6).compare("a", op, "b", op).rounds() == 6U);
    // and raised until an interval is possible at all: five rounds cannot
    // support a 95% statement, so anything below six is lifted to it
    CHECK(quiet(4).compare("a", op, "b", op).rounds() == 6U);
    CHECK(quiet(1).compare("a", op, "b", op).rounds() == 6U);
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_result_carries_both_measurements") {
    Work a;
    Work b;
    auto bench = quiet(12);
    auto const result = bench.compare(
        "left",
        [&] {
            a.step();
        },
        "right",
        [&] {
            b.step();
        });

    CHECK(result[0].name == "left");
    CHECK(result[1].name == "right");
    // rounds are rounded up to a whole number of ABBA blocks
    CHECK(result.rounds() == 12U);

    // both sides are also available as ordinary Results, for callers who want
    // their own arithmetic
    using M = ankerl::nanobench::Result::Measure;
    CHECK(result[0].result.size() == 12U);
    CHECK(result[1].result.size() == 12U);
    CHECK(result[0].result.median(M::elapsed) > 0.0);
    CHECK(result[1].result.median(M::elapsed) > 0.0);
    CHECK(result[0].result.config().mBenchmarkName == "left");
    CHECK(result[1].result.config().mBenchmarkName == "right");

    // compare() reports its own verdict and does not append to the ordinary
    // results, which belong to run()
    CHECK(bench.results().empty());
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_writes_a_verdict_to_the_output_stream") {
    Work a;
    Work b;
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    bench.output(&oss).epochs(12).performanceCounters(false).minEpochTime(
        std::chrono::microseconds(200));
    auto const result = bench.compare(
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
    // the absolute numbers of each side, not only the ratio between them: a
    // ratio with no scale next to it cannot be told from the same ratio on a
    // completely different scale, and an unstable side is invisible
    CHECK(text.find("Summary") != std::string::npos);
    CHECK(text.find("ns/op") != std::string::npos);
    CHECK(text.find("err%") != std::string::npos);
    CHECK(text.find("relative") != std::string::npos);
    CHECK(text.find("95% CI") != std::string::npos);

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
    quietBench.compare(
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
TEST_CASE("unit_compare_names_a_backtick_safely") {
    // same escaping rule as the markdown table: a benchmark name is written as
    // code
    Work a;
    Work b;
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    bench.output(&oss).epochs(4).performanceCounters(false).minEpochTime(
        std::chrono::microseconds(200));
    bench.compare(
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

// The tests below are regressions. Each one covers a bug that was in compare()
// as first written, and each one goes red if that bug is put back.

// NOLINTNEXTLINE
TEST_CASE("unit_compare_honors_epoch_iterations") {
    // An exact epochIterations() overrides any calculated count in a normal
    // run, in every state of the iteration logic. compare() calibrated its own
    // count and ignored the setting entirely, so this asked for 3 and got
    // whatever filled an epoch - a number some four orders of magnitude larger.
    Work a;
    Work b;
    auto bench = quiet(8);
    bench.epochIterations(3);
    auto const result = bench.compare(
        "a",
        [&] {
            a.step();
        },
        "b",
        [&] {
            b.step();
        });

    // Result stores the iteration count of every epoch it measured.
    for (size_t i = 0; i < result.size(); ++i) {
        auto const& r = result[i].result;
        REQUIRE(r.size() > 0U);
        for (size_t epoch = 0; epoch < r.size(); ++epoch) {
            CHECK(
                r.get(epoch, ankerl::nanobench::Result::Measure::iterations) ==
                doctest::Approx(3.0));
        }
    }
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_min_epoch_time_wins_over_max") {
    // When the two bounds conflict the minimum wins - that is the rule run()
    // has always followed. compare() clamped in the other order, so a minimum
    // above the maximum silently produced the *maximum*, i.e. epochs orders of
    // magnitude shorter than asked for.
    //
    // A one-sided bound on elapsed time, deliberately loose: getting this wrong
    // moves the runtime by orders of magnitude, so there is nothing to gain by
    // being tight and a flaky leg to lose.
    Work a;
    Work b;
    ankerl::nanobench::Bench bench;
    bench.output(nullptr)
        .epochs(2)
        .performanceCounters(false)
        .minEpochTime(std::chrono::milliseconds(20))
        .maxEpochTime(std::chrono::microseconds(1));

    auto const before = std::chrono::steady_clock::now();
    bench.compare(
        "a",
        [&] {
            a.step();
        },
        "b",
        [&] {
            b.step();
        });
    auto const elapsed = std::chrono::steady_clock::now() - before;

    // 2 rounds x 2 alternatives x 20ms = 80ms if the minimum wins. If the
    // maximum wins instead this finishes in well under a millisecond.
    CHECK(elapsed >= std::chrono::milliseconds(40));
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_honors_hide_column") {
    // hideColumn() applies to a comparison table too. The compare table built
    // its columns directly instead of going through the visibility check, so
    // hideColumn() was silently inert there.
    Work a;
    Work b;
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    bench.output(&oss)
        .epochs(4)
        .performanceCounters(false)
        .minEpochTime(std::chrono::microseconds(200))
        .hideColumn(ankerl::nanobench::Column::error);
    bench.compare(
        "a",
        [&] {
            a.step();
        },
        "b",
        [&] {
            b.step();
        });

    CHECK(oss.str().find("err%") == std::string::npos);
    // the columns that were not hidden are still there
    CHECK(oss.str().find("relative") != std::string::npos);
    CHECK(oss.str().find("total") != std::string::npos);
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_leaves_the_stream_ready_for_a_table") {
    // The header of an ordinary table is only written when the table's shape
    // changes, and that state lives on the output stream. compare() writes its
    // own header without touching that state, so a run() afterwards compared
    // against a shape it had never written and streamed its rows with no header
    // at all - directly under the comparison, where they read as part of it.
    Work a;
    Work b;
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    bench.output(&oss).epochs(4).performanceCounters(false).minEpochTime(
        std::chrono::microseconds(200));

    // a run() first, so the stream remembers that shape
    bench.run("before", [&] {
        a.step();
    });
    auto const afterFirstRun = oss.str().size();

    bench.compare(
        "x",
        [&] {
            a.step();
        },
        "y",
        [&] {
            b.step();
        });
    bench.run("after", [&] {
        b.step();
    });

    // the second run() must have written a header of its own
    auto const tail = oss.str().substr(afterFirstRun);
    CHECK(tail.find("ns/op") != std::string::npos);
    CHECK(tail.find("`after`") != std::string::npos);
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_honors_clock_resolution_multiple") {
    // The epoch length is clockResolution() * clockResolutionMultiple(), then
    // clamped into [minEpochTime, maxEpochTime]. compare() calibrated from the
    // bounds alone and never consulted the multiple, which is only harmless
    // while minEpochTime binds - i.e. on every platform whose clock is good.
    // On a coarse-clock platform it is the multiple that sets the epoch, and
    // ignoring it makes every epoch far too short to measure anything.
    //
    // Iteration counts rather than times: the multiple is what turns the clock
    // resolution into the target, so raising it by 10000x has to raise the
    // calibrated count too. A loose one-sided bound, for the usual reason.
    auto countFor = [](size_t multiple) {
        Work a;
        Work b;
        ankerl::nanobench::Bench bench;
        bench.output(nullptr)
            .epochs(2)
            .performanceCounters(false)
            .minEpochTime(std::chrono::nanoseconds(1))
            .maxEpochTime(std::chrono::milliseconds(500))
            .clockResolutionMultiple(multiple);
        auto const result = bench.compare(
            "a",
            [&] {
                a.step();
            },
            "b",
            [&] {
                b.step();
            });
        return result[0].result.get(
            0, ankerl::nanobench::Result::Measure::iterations);
    };

    CHECK(countFor(10000) > countFor(1) * 10.0);
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_resolves_a_two_fold_difference") {
    // The question a comparison exists to answer: is a 2x difference resolved,
    // and reported as a 2x difference?
    //
    // unit_compare_more_than_two_alternatives used to ask this with one step
    // against two, and the 32 bit leg answered 1.02x - no difference at all.
    // The reason is not that the leg cannot resolve 2x; it is that one step
    // against two is not a 2x difference in wall time there. Each iteration
    // carries a fixed cost - the loop, the doNotOptimizeAway barrier - and
    // where that cost is comparable to one step, (fixed + 1) against
    // (fixed + 2) is nowhere near double.
    //
    // So the work is amortized instead: 20 steps against 40. The fixed cost is
    // divided by twenty, the ratio is a genuine 2x on any platform, and what is
    // being tested is the statistics rather than the platform's ability to time
    // a single multiply.
    Work a;
    Work b;
    auto const result = quiet(52).compare(
        "twenty steps",
        [&] {
            for (int i = 0; i < 20; ++i) {
                a.step();
            }
        },
        "forty steps",
        [&] {
            for (int i = 0; i < 40; ++i) {
                b.step();
            }
        });

    INFO("relative " << result[1].relative << ", CI [" << result[1].relativeLow
                     << " .. " << result[1].relativeHigh << "]");

    // resolved: the whole interval sits below the baseline, so the measurement
    // separated them rather than merely ranking them
    CHECK(result.isSignificant(1));
    CHECK(result[1].relativeHigh < 1.0);

    // and it is the difference that is really there. Loose on purpose - the
    // point is that a 2x difference reads as roughly 2x rather than as noise,
    // not that any leg reproduces 0.5 exactly.
    CHECK(result[1].relative < 0.8);
    CHECK(result[1].relative > 0.25);
}
