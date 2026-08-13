#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <cstddef>
#include <sstream>
#include <string>

namespace {

// Number of the "relative" column of the markdown row that belongs to the given
// benchmark. It is the first column, so the row starts with '|' and then the
// number, which std::stod conveniently stops parsing at the trailing '%'.
double relativeOf(std::string const& markdown, std::string const& name) {
    auto const needle = '`' + name + '`';
    std::istringstream in(markdown);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find(needle) != std::string::npos) {
            return std::stod(line.substr(1));
        }
    }
    FAIL("benchmark '" << name << "' not found in the output");
    return 0.0;
}

} // namespace

// The relative column used to compare the raw epoch runtimes, while every other
// column of the same row is per unit. So as soon as the baseline used a
// different batch size than the run compared against it, the percentage was
// off by exactly the ratio of the batch sizes.
// See https://github.com/martinus/nanobench/issues/131
//
// The expected percentage is derived from the two medians rather than assumed
// to be 100%. Both benchmarks do the same work per item, but that only makes
// them equally fast on an idle machine: on a CI runner the first one pays for
// the frequency ramp and the second one does not, which produced a 38% gap
// between two measurements that were individually stable at err% 0.5. The bug
// this guards against halves the percentage whatever the timings are, so
// deriving the expectation costs nothing in sensitivity.
// NOLINTNEXTLINE
TEST_CASE("relative_takes_batch_into_account") {
    ankerl::nanobench::Rng rng(123);
    auto work = [&rng](size_t n) {
        for (size_t i = 0; i < n; ++i) {
            ankerl::nanobench::doNotOptimizeAway(rng());
        }
    };

    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    bench.output(&oss).relative(true).unit("item");

    // Both benchmarks need the same time per item, they just process a
    // different number of items per iteration.
    bench.batch(100).run("baseline", [&] {
        work(100);
    });
    bench.batch(200).run("twice the batch", [&] {
        work(200);
    });

    auto const markdown = oss.str();
    INFO(markdown);
    REQUIRE(relativeOf(markdown, "baseline") == doctest::Approx(100.0));

    // Every column of the row is per unit, so the percentage has to be the
    // ratio of the per unit medians. Comparing the raw epoch times instead -
    // the bug - gives exactly half of that here, because the batch is twice as
    // large, so a 1% tolerance for the rounding in the table is plenty.
    auto const& results = bench.results();
    REQUIRE(results.size() == 2);
    auto perItem = [](ankerl::nanobench::Result const& r) {
        return r.median(ankerl::nanobench::Result::Measure::elapsed) /
               r.config().mBatch;
    };
    auto const expected = 100.0 * perItem(results[0]) / perItem(results[1]);

    REQUIRE(relativeOf(markdown, "twice the batch") ==
            doctest::Approx(expected).epsilon(0.01));
}
