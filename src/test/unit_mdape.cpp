#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

// medianAbsolutePercentError(elapsed) of a result built from the given
// per-epoch durations. It must never be NaN, whatever was measured.
double mdape(std::vector<int64_t> const& nanos) {
    ankerl::nanobench::Result r{ankerl::nanobench::Config{}};
    auto& pc = ankerl::nanobench::detail::performanceCounters();
    for (auto ns : nanos) {
        r.add(std::chrono::nanoseconds(ns), 1, pc);
    }
    auto error = r.medianAbsolutePercentError(
        ankerl::nanobench::Result::Measure::elapsed);
    REQUIRE(!std::isnan(error));
    return error;
}

} // namespace

// A measurement of 0 used to make medianAbsolutePercentError calculate
// (x - med) / x = 0 / 0 = NaN. A NaN in the data breaks the strict weak
// ordering that std::sort requires, so calculating the median of it was
// undefined behavior.
// NOLINTNEXTLINE
TEST_CASE("mdape_with_zero_measurements") {
    // no measurement at all
    REQUIRE(mdape({}) == doctest::Approx(0.0));

    // all measurements are 0: they all agree, so there is no error
    REQUIRE(mdape({0, 0, 0, 0, 0}) == doctest::Approx(0.0));

    // majority is 0: the median is 0 too, so most measurements agree with it
    REQUIRE(mdape({0, 0, 0, 7, 9}) == doctest::Approx(0.0));

    // a single 0 in otherwise fine data must not destroy the whole metric
    REQUIRE(mdape({10, 10, 0, 10, 10}) == doctest::Approx(0.0));

    // a 0 measured where the median is not 0 has an unbounded error
    REQUIRE(std::isinf(mdape({0, 10})));

    // no zeros: the plain median of |(x - med) / x|
    REQUIRE(mdape({8, 9, 10, 11, 12}) == doctest::Approx(1.0 / 9.0));
}
