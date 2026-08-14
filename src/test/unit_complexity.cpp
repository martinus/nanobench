#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

// The complexity estimation is a headline feature - run the same benchmark over
// a range of sizes, and nanobench says which of O(1), O(n), O(log n),
// O(n log n), O(n^2), O(n^3) fits best. Three test files used it and not one of
// them asserted anything: they printed the table to stdout and moved on. So the
// curve fitting, the ranking and collectRangeMeasure were verified by nobody.
//
// The fitting is pure arithmetic, so it is checked here against data that is
// *exactly* of a given complexity, where the answer can be written down: the
// matching model fits with zero error and recovers the constant it was built
// with. Only the end-to-end shape is checked against a real benchmark, because
// which model wins there depends on the machine.
namespace {

using ankerl::nanobench::BigO;
using ankerl::nanobench::Result;

// A complexity model: maps n to the shape the runtime is expected to follow.
using Shape = double (*)(double);

// Measurements of exactly `constant * shape(n)` for a spread of sizes.
BigO::RangeMeasure exactly(double constant, Shape shape) {
    BigO::RangeMeasure data;
    for (double n : {8.0, 16.0, 32.0, 64.0, 128.0, 256.0, 512.0}) {
        data.emplace_back(n, constant * shape(n));
    }
    return data;
}

double one(double) {
    return 1.0;
}
double linear(double n) {
    return n;
}
double log2Of(double n) {
    return std::log2(n);
}
double nLogN(double n) {
    return n * std::log2(n);
}
double squared(double n) {
    return n * n;
}
double cubed(double n) {
    return n * n * n;
}

// The shape each built-in model name is supposed to be fitted with, written
// out here independently of nanobench's own list.
Shape shapeNamed(std::string const& name) {
    if (name == "O(1)") {
        return one;
    }
    if (name == "O(n)") {
        return linear;
    }
    if (name == "O(log n)") {
        return log2Of;
    }
    if (name == "O(n log n)") {
        return nLogN;
    }
    if (name == "O(n^2)") {
        return squared;
    }
    if (name == "O(n^3)") {
        return cubed;
    }
    return nullptr;
}

std::vector<std::string> namesOf(std::vector<BigO> const& bigOs) {
    std::vector<std::string> names;
    for (auto const& bigO : bigOs) {
        names.push_back(bigO.name());
    }
    return names;
}

} // namespace

// NOLINTNEXTLINE
TEST_CASE("unit_complexity_exact_fit_has_no_error") {
    // For data that is exactly c * shape(n), the model of that shape has to fit
    // perfectly: zero residual, and the constant recovered as it went in.
    struct Case {
        char const* name;
        Shape shape;
    };
    std::vector<Case> const cases{{"O(1)", one},        {"O(n)", linear},
                                  {"O(log n)", log2Of}, {"O(n log n)", nLogN},
                                  {"O(n^2)", squared},  {"O(n^3)", cubed}};

    for (auto const& c : cases) {
        INFO(c.name);
        auto const data = exactly(3.5, c.shape);
        BigO const fit(c.name, data, c.shape);
        CHECK(fit.name() == c.name);
        CHECK(fit.constant() == doctest::Approx(3.5));
        CHECK(fit.normalizedRootMeanSquare() == doctest::Approx(0.0));
    }
}

// NOLINTNEXTLINE
TEST_CASE("unit_complexity_wrong_model_has_error") {
    // The flip side, and the reason the numbers above mean anything: a model of
    // the wrong shape must not also fit perfectly.
    auto const quadratic = exactly(2.0, squared);

    BigO const right("O(n^2)", quadratic, squared);
    BigO const wrong("O(n)", quadratic, linear);
    BigO const alsoWrong("O(1)", quadratic, one);

    CHECK(right.normalizedRootMeanSquare() == doctest::Approx(0.0));
    CHECK(right.normalizedRootMeanSquare() < wrong.normalizedRootMeanSquare());

    // The exact residuals, not just "more than zero". The error is what the
    // table prints as err%, so a factor slipped into it is a wrong number in
    // the output even though every ranking above still comes out right.
    // Least squares on the seven sizes used by exactly(): fitting c*n to n^2
    // gives c = sum(n^3)/sum(n^2), and the normalized RMS follows from it.
    CHECK(wrong.constant() == doctest::Approx(2.0 * 438.8837209302));
    CHECK(wrong.normalizedRootMeanSquare() == doctest::Approx(0.5853778254));
    CHECK(alsoWrong.constant() == doctest::Approx(2.0 * 49929.1428571429));
    CHECK(alsoWrong.normalizedRootMeanSquare() ==
          doctest::Approx(1.7889976877));
}

// NOLINTNEXTLINE
TEST_CASE("unit_complexity_error_is_normalized") {
    // "normalized" root mean square means relative to the mean measurement, so
    // it is a property of the *shape* of the data and not of its scale. Timing
    // the same algorithm in nanoseconds or in seconds has to give the same
    // err%, or the number cannot be compared between benchmarks at all.
    BigO const small("O(n)", exactly(1.0, squared), linear);
    BigO const large("O(n)", exactly(1e6, squared), linear);

    CHECK(small.normalizedRootMeanSquare() ==
          doctest::Approx(large.normalizedRootMeanSquare()));
    // the constant does scale with the data - it carries the unit
    CHECK(large.constant() == doctest::Approx(small.constant() * 1e6));
}

// NOLINTNEXTLINE
TEST_CASE("unit_complexity_best_fit_wins_the_sort") {
    // The six candidates are ranked by ascending error, which is what makes the
    // first row of the printed table the answer. Built here rather than through
    // Bench, so the input is exact and the outcome is not a matter of how the
    // machine behaved.
    auto rank = [](BigO::RangeMeasure const& data) {
        std::vector<BigO> bigOs{
            BigO("O(1)", data, one),        BigO("O(n)", data, linear),
            BigO("O(log n)", data, log2Of), BigO("O(n log n)", data, nLogN),
            BigO("O(n^2)", data, squared),  BigO("O(n^3)", data, cubed)};
        std::sort(bigOs.begin(), bigOs.end());
        return bigOs;
    };

    CHECK(rank(exactly(1.0, linear)).front().name() == "O(n)");
    CHECK(rank(exactly(1.0, squared)).front().name() == "O(n^2)");
    CHECK(rank(exactly(1.0, cubed)).front().name() == "O(n^3)");
    CHECK(rank(exactly(1.0, nLogN)).front().name() == "O(n log n)");
    CHECK(rank(exactly(1.0, one)).front().name() == "O(1)");

    // and the ranking really is by ascending error
    auto const ranked = rank(exactly(1.0, squared));
    REQUIRE(ranked.size() == 6U);
    for (size_t i = 1; i < ranked.size(); ++i) {
        INFO(ranked[i - 1].name() << " then " << ranked[i].name());
        CHECK(ranked[i - 1].normalizedRootMeanSquare() <=
              ranked[i].normalizedRootMeanSquare());
    }
}

// NOLINTNEXTLINE
TEST_CASE("unit_complexity_ordering_is_a_strict_weak_ordering") {
    // Every candidate has the same error when there is nothing to fit, and
    // std::sort needs a strict weak ordering even then - which is why the
    // comparison falls back to the name instead of leaving equal elements
    // incomparable. Getting this wrong is undefined behavior, not a wrong
    // answer, so it is worth checking directly.
    BigO::RangeMeasure const empty;
    std::vector<BigO> bigOs{BigO("O(n^2)", empty, squared),
                            BigO("O(1)", empty, one),
                            BigO("O(n)", empty, linear)};
    CHECK_NOTHROW(std::sort(bigOs.begin(), bigOs.end()));
    REQUIRE(bigOs.size() == 3U);

    // Nothing to fit means no constant and no error, rather than the 0/0 this
    // used to compute - which is a NaN, and a NaN is both undefined behaviour
    // for the division and unordered for the sort above.
    for (auto const& bigO : bigOs) {
        INFO(bigO.name());
        CHECK_FALSE(std::isnan(bigO.constant()));
        CHECK_FALSE(std::isnan(bigO.normalizedRootMeanSquare()));
        CHECK(bigO.constant() == doctest::Approx(0.0));
        CHECK(bigO.normalizedRootMeanSquare() == doctest::Approx(0.0));
    }
    // with equal errors the ordering is by name, so the sort is deterministic
    CHECK(bigOs.front().name() == "O(1)");

    // measurements that are all zero are the same kind of degenerate: the error
    // is relative to the mean, and there is no relative error to report
    BigO::RangeMeasure const allZero{{1.0, 0.0}, {2.0, 0.0}, {4.0, 0.0}};
    BigO const flat("O(n)", allZero, linear);
    CHECK_FALSE(std::isnan(flat.constant()));
    CHECK_FALSE(std::isnan(flat.normalizedRootMeanSquare()));
    CHECK(flat.normalizedRootMeanSquare() == doctest::Approx(0.0));

    // and so is a range where every n is zero
    BigO::RangeMeasure const noRange{{0.0, 1.0}, {0.0, 2.0}};
    BigO const degenerate("O(n)", noRange, linear);
    CHECK_FALSE(std::isnan(degenerate.constant()));
    CHECK_FALSE(std::isnan(degenerate.normalizedRootMeanSquare()));

    // two models with identical error are ordered by name, deterministically
    auto const data = exactly(1.0, linear);
    BigO const a("O(a)", data, linear);
    BigO const b("O(b)", data, linear);
    CHECK(a < b);
    CHECK_FALSE(b < a);
    CHECK_FALSE(a < a);
}

// NOLINTNEXTLINE
TEST_CASE("unit_complexity_map_range_measure") {
    BigO::RangeMeasure const data{{2.0, 100.0}, {4.0, 200.0}};

    // mapRangeMeasure rewrites the n of each pair and leaves the measurement
    // alone - the measurement is what the model is fitted against
    auto const mapped = BigO::mapRangeMeasure(data, squared);
    REQUIRE(mapped.size() == 2U);
    CHECK(mapped[0].first == doctest::Approx(4.0));
    CHECK(mapped[0].second == doctest::Approx(100.0));
    CHECK(mapped[1].first == doctest::Approx(16.0));
    CHECK(mapped[1].second == doctest::Approx(200.0));
}

// NOLINTNEXTLINE
TEST_CASE("unit_complexity_collect_range_measure") {
    auto& pc = ankerl::nanobench::detail::performanceCounters();
    auto resultWith = [&pc](double complexityN, int64_t nanos) {
        ankerl::nanobench::Config config;
        config.mComplexityN = complexityN;
        Result r{config};
        r.add(std::chrono::nanoseconds(nanos), 1, pc);
        return r;
    };

    std::vector<Result> const results{
        resultWith(10.0, 100), resultWith(20.0, 200), resultWith(-1.0, 999)};

    // only the results that were given a complexityN take part; the default of
    // -1 means "this benchmark is not part of a complexity run"
    auto const collected = BigO::collectRangeMeasure(results);
    REQUIRE(collected.size() == 2U);
    CHECK(collected[0].first == doctest::Approx(10.0));
    CHECK(collected[0].second == doctest::Approx(100e-9));
    CHECK(collected[1].first == doctest::Approx(20.0));
    CHECK(collected[1].second == doctest::Approx(200e-9));

    // nothing to collect is not an error
    std::vector<Result> const none{resultWith(-1.0, 100)};
    CHECK(BigO::collectRangeMeasure(none).empty());
}

// NOLINTNEXTLINE
TEST_CASE("unit_complexity_rendering") {
    auto const data = exactly(2.5, linear);
    BigO const fit("O(n)", data, linear);

    std::ostringstream single;
    single << fit;
    INFO(single.str());
    CHECK(single.str().find("O(n)") != std::string::npos);
    CHECK(single.str().find("rms=") != std::string::npos);

    std::ostringstream table;
    table << std::vector<BigO>{fit};
    INFO(table.str());
    CHECK(table.str().find("complexity") != std::string::npos);
    CHECK(table.str().find("O(n)") != std::string::npos);
    // the error column is a percentage
    CHECK(table.str().find("%") != std::string::npos);

    // printing must not leave the stream reformatted for whatever comes next
    std::ostringstream reused;
    reused << std::setprecision(3) << std::fixed;
    reused << std::vector<BigO>{fit};
    reused.str("");
    reused << 1.25;
    CHECK(reused.str() == "1.250");
}

// NOLINTNEXTLINE
TEST_CASE("unit_complexity_through_bench") {
    // End to end, but only the shape is asserted: which model wins depends on
    // what the machine did, and this is not the place to find that out.
    ankerl::nanobench::Bench bench;
    bench.output(nullptr).warmup(0).epochs(2).performanceCounters(false);

    ankerl::nanobench::Rng rng(123);
    for (size_t n = 16; n <= 256; n *= 2) {
        bench.complexityN(n).run("work", [&] {
            for (size_t i = 0; i < n; ++i) {
                ankerl::nanobench::doNotOptimizeAway(rng());
            }
        });
    }
    CHECK(bench.complexityN() == doctest::Approx(256.0));

    auto const bigOs = bench.complexityBigO();
    // the six built-in models, best fit first
    REQUIRE(bigOs.size() == 6U);
    auto const names = namesOf(bigOs);
    for (char const* expected :
         {"O(1)", "O(n)", "O(log n)", "O(n log n)", "O(n^2)", "O(n^3)"}) {
        INFO(expected);
        CHECK(names.end() !=
              std::find(names.begin(), names.end(), std::string(expected)));
    }
    for (size_t i = 1; i < bigOs.size(); ++i) {
        CHECK(bigOs[i - 1].normalizedRootMeanSquare() <=
              bigOs[i].normalizedRootMeanSquare());
    }

    // Each built-in name has to be fitted with the shape it advertises. The
    // measurements are whatever the machine produced, but both sides here are
    // fitted to the *same* numbers - read back through the same collector
    // complexityBigO() uses - so the comparison is exact and not a timing
    // assumption. Without this, a built-in model quietly fitted with the wrong
    // curve still comes out under the right name.
    auto const data = BigO::collectRangeMeasure(bench.results());
    REQUIRE(data.size() >= 4U);
    for (auto const& produced : bigOs) {
        INFO(produced.name());
        Shape const shape = shapeNamed(produced.name());
        REQUIRE(shape != nullptr);
        BigO const independent(produced.name(), data, shape);
        CHECK(produced.constant() == doctest::Approx(independent.constant()));
        CHECK(produced.normalizedRootMeanSquare() ==
              doctest::Approx(independent.normalizedRootMeanSquare()));
    }

    // the custom-model overload keeps the name it was given
    auto const custom = bench.complexityBigO("O(log log n)", [](double n) {
        return std::log2(std::log2(n));
    });
    CHECK(custom.name() == "O(log log n)");
    CHECK(custom.normalizedRootMeanSquare() >= 0.0);
}
