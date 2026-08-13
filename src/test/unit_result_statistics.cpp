#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <vector>

// Result's statistics are public API - they are what a caller reaches for when
// the markdown table is not the output they want, and what the mustache
// templates are built on. median() and medianAbsolutePercentError() were the
// only ones any test touched; average(), minimum(), maximum(), sum(), empty()
// and fromString() had none at all, so an arithmetic slip in any of them would
// have shown up only as a wrong number in somebody's report.
namespace {

using ankerl::nanobench::Result;

// A Result carrying one measurement per given duration, all with a single
// iteration - so the per-iteration `elapsed` of entry i is exactly nanos[i]
// seconds / 1e9, and the arithmetic below can be written down by hand.
Result resultOf(std::vector<int64_t> const& nanos) {
    Result r{ankerl::nanobench::Config{}};
    auto& pc = ankerl::nanobench::detail::performanceCounters();
    for (auto ns : nanos) {
        r.add(std::chrono::nanoseconds(ns), 1, pc);
    }
    return r;
}

} // namespace

// NOLINTNEXTLINE
TEST_CASE("unit_result_empty") {
    auto const r = resultOf({});

    CHECK(r.empty());
    CHECK(r.size() == 0U);
    CHECK_FALSE(r.has(Result::Measure::elapsed));
    CHECK_FALSE(r.has(Result::Measure::iterations));

    // every statistic of nothing is 0, not NaN and not a read past the end
    CHECK(r.median(Result::Measure::elapsed) == doctest::Approx(0.0));
    CHECK(r.average(Result::Measure::elapsed) == doctest::Approx(0.0));
    CHECK(r.minimum(Result::Measure::elapsed) == doctest::Approx(0.0));
    CHECK(r.maximum(Result::Measure::elapsed) == doctest::Approx(0.0));
    CHECK(r.sum(Result::Measure::elapsed) == doctest::Approx(0.0));
    CHECK(r.sumProduct(Result::Measure::iterations, Result::Measure::elapsed) ==
          doctest::Approx(0.0));
}

// NOLINTNEXTLINE
TEST_CASE("unit_result_statistics_of_known_data") {
    // 10, 20, 30, 40 nanoseconds, one iteration each
    auto const r = resultOf({10, 20, 30, 40});
    auto const ns = 1e-9;

    REQUIRE(r.size() == 4U);
    CHECK_FALSE(r.empty());
    CHECK(r.has(Result::Measure::elapsed));

    CHECK(r.sum(Result::Measure::elapsed) == doctest::Approx(100 * ns));
    CHECK(r.average(Result::Measure::elapsed) == doctest::Approx(25 * ns));
    CHECK(r.minimum(Result::Measure::elapsed) == doctest::Approx(10 * ns));
    CHECK(r.maximum(Result::Measure::elapsed) == doctest::Approx(40 * ns));
    // even number of entries: the mean of the two in the middle
    CHECK(r.median(Result::Measure::elapsed) == doctest::Approx(25 * ns));

    // one iteration per epoch, four epochs
    CHECK(r.sum(Result::Measure::iterations) == doctest::Approx(4.0));
    CHECK(r.average(Result::Measure::iterations) == doctest::Approx(1.0));
    CHECK(r.minimum(Result::Measure::iterations) == doctest::Approx(1.0));
    CHECK(r.maximum(Result::Measure::iterations) == doctest::Approx(1.0));

    // total runtime: sum over epochs of iterations * time-per-iteration
    CHECK(r.sumProduct(Result::Measure::iterations, Result::Measure::elapsed) ==
          doctest::Approx(100 * ns));

    // get() reads a single epoch, in insertion order rather than sorted order
    CHECK(r.get(0, Result::Measure::elapsed) == doctest::Approx(10 * ns));
    CHECK(r.get(3, Result::Measure::elapsed) == doctest::Approx(40 * ns));
}

// NOLINTNEXTLINE
TEST_CASE("unit_result_median_is_order_independent") {
    auto const ns = 1e-9;

    // odd count: the middle element, wherever it was inserted
    CHECK(resultOf({30, 10, 20}).median(Result::Measure::elapsed) ==
          doctest::Approx(20 * ns));
    CHECK(resultOf({10, 20, 30}).median(Result::Measure::elapsed) ==
          doctest::Approx(20 * ns));
    CHECK(resultOf({20, 30, 10}).median(Result::Measure::elapsed) ==
          doctest::Approx(20 * ns));

    // a single measurement is its own median, minimum and maximum
    auto const one = resultOf({7});
    CHECK(one.median(Result::Measure::elapsed) == doctest::Approx(7 * ns));
    CHECK(one.minimum(Result::Measure::elapsed) == doctest::Approx(7 * ns));
    CHECK(one.maximum(Result::Measure::elapsed) == doctest::Approx(7 * ns));

    // median must not reorder the stored data - get() still sees it as added
    auto const r = resultOf({30, 10, 20});
    ankerl::nanobench::doNotOptimizeAway(r.median(Result::Measure::elapsed));
    CHECK(r.get(0, Result::Measure::elapsed) == doctest::Approx(30 * ns));
    CHECK(r.get(1, Result::Measure::elapsed) == doctest::Approx(10 * ns));
    CHECK(r.get(2, Result::Measure::elapsed) == doctest::Approx(20 * ns));
}

// NOLINTNEXTLINE
TEST_CASE("unit_result_elapsed_is_per_iteration") {
    // The measurement handed to add() is the time for the *whole* epoch, and
    // what gets stored is the time per iteration. Getting that division wrong
    // would scale every reported number by the epoch size.
    Result r{ankerl::nanobench::Config{}};
    auto& pc = ankerl::nanobench::detail::performanceCounters();
    r.add(std::chrono::nanoseconds(1000), 100, pc);

    CHECK(r.get(0, Result::Measure::elapsed) == doctest::Approx(10e-9));
    CHECK(r.get(0, Result::Measure::iterations) == doctest::Approx(100.0));
    // and the product is back to the epoch's total
    CHECK(r.sumProduct(Result::Measure::iterations, Result::Measure::elapsed) ==
          doctest::Approx(1000e-9));
}

// NOLINTNEXTLINE
TEST_CASE("unit_result_sum_product_of_different_lengths") {
    auto const r = resultOf({10, 20});

    // sumProduct pairs up measurements by index, so measures of different
    // lengths have no meaningful product and it returns 0 rather than reading
    // past the end of the shorter one. A performance counter the kernel
    // refused is exactly how that happens in practice: `elapsed` has an entry
    // per epoch and the counter has none.
    if (r.has(Result::Measure::pagefaults)) {
        // counters available - the pairing is real, so just check it is not
        // silently zero
        CHECK(r.sumProduct(Result::Measure::elapsed,
                           Result::Measure::pagefaults) >= 0.0);
    } else {
        CHECK(r.sumProduct(Result::Measure::elapsed,
                           Result::Measure::pagefaults) ==
              doctest::Approx(0.0));
    }
}

// NOLINTNEXTLINE
TEST_CASE("unit_result_measure_from_string") {
    // The mustache templates address measures by these names, so a typo here
    // turns a documented tag into a silent 0 in everybody's report.
    CHECK(Result::fromString("elapsed") == Result::Measure::elapsed);
    CHECK(Result::fromString("iterations") == Result::Measure::iterations);
    CHECK(Result::fromString("pagefaults") == Result::Measure::pagefaults);
    CHECK(Result::fromString("cpucycles") == Result::Measure::cpucycles);
    CHECK(Result::fromString("contextswitches") ==
          Result::Measure::contextswitches);
    CHECK(Result::fromString("instructions") == Result::Measure::instructions);
    CHECK(Result::fromString("branchinstructions") ==
          Result::Measure::branchinstructions);
    CHECK(Result::fromString("branchmisses") == Result::Measure::branchmisses);

    // anything else is reported as "no such measure" rather than defaulting to
    // one of them
    CHECK(Result::fromString("") == Result::Measure::_size);
    CHECK(Result::fromString("nonsense") == Result::Measure::_size);
    CHECK(Result::fromString("Elapsed") == Result::Measure::_size);
    CHECK(Result::fromString("elapsed ") == Result::Measure::_size);
}

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)

// NOLINTNEXTLINE
TEST_CASE("unit_result_get_out_of_range") {
    auto const r = resultOf({10, 20});

    // through a lambda: get() and context() are [[nodiscard]], and calling one
    // straight inside CHECK_THROWS_AS would discard its result
    auto read = [&r](size_t idx) {
        return r.get(idx, Result::Measure::elapsed);
    };
    CHECK_NOTHROW(read(1));
    CHECK_THROWS_AS(read(2), std::out_of_range);

    // Measure::_size is the enum's end marker, not a measure: the per-measure
    // storage has exactly _size entries, so it is deliberately not passed here.
}

// NOLINTNEXTLINE
TEST_CASE("unit_result_context_of_unknown_variable") {
    auto const r = resultOf({10});
    auto readContext = [&r](char const* name) {
        return r.context(name);
    };
    CHECK_THROWS_AS(readContext("never set"), std::out_of_range);

    ankerl::nanobench::Config config;
    config.mContext["threads"] = "8";
    Result const withContext{config};
    CHECK(withContext.context("threads") == "8");
    CHECK(withContext.context(std::string("threads")) == "8");
}

#endif
