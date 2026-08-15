#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// What compare() *prints*, as opposed to what it computes.
//
// unit_compare.cpp asserts hard on the CompareResult and then checks the
// rendered text only for the presence of "95% CI". A mutation sweep over the
// whole header found 69 of the 75 mutants in the output path surviving,
// including one that inverts the headline claim into "`slow` ran 0.13x faster
// than `fast`" with the suite still green. Nothing here relates two
// measurements to each other: the sentence is checked against the very
// CompareResult it was rendered from, which is exact arithmetic.
//
// Most of it does not measure anything at all. CompareResult and its Entry are
// public and constructible, so a comparison with a chosen ratio and interval
// can be built outright and streamed - and then the expected sentence is a
// literal, not something recomputed from the same inputs that could agree with
// a broken formatter by sharing its mistake.
namespace {
namespace nb = ankerl::nanobench::detail;
using ankerl::nanobench::CompareResult;
using ankerl::nanobench::Result;

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

// A real Result to hang synthetic entries off: the table half reads a config
// and per-epoch measurements out of it, so a default-constructed one would not
// render. Which Result it is does not matter to the summary tests below - only
// the ratios do, and those are supplied.
Result measured(char const* name, int workPerOp, size_t rounds,
                char const* title = "title") {
    ankerl::nanobench::Bench bench;
    bench.output(nullptr)
        .epochs(rounds)
        .performanceCounters(false)
        .title(title)
        .minEpochTime(std::chrono::microseconds(200));
    Work w;
    bench.run(name, [&] {
        for (int i = 0; i < workPerOp; ++i) {
            w.step();
        }
    });
    return bench.results().front();
}

// The two entries carry *different* titles on purpose. The table heads itself
// from the baseline's config, and with one Result shared between both entries
// there would be nothing to tell "the baseline's" apart from "any entry's".
CompareResult twoWay(double relative, double low, double high,
                     size_t tiedRounds, size_t rounds) {
    std::vector<CompareResult::Entry> entries;
    entries.emplace_back("base", measured("x", 1, 4, "baseline title"), 1.0,
                         1.0, 1.0, 0U);
    entries.emplace_back("other", measured("y", 1, 4, "other title"), relative,
                         low, high, tiedRounds);
    return CompareResult(std::move(entries), rounds);
}

// everything after the "  Summary" banner, or "" when there is none - a helper
// that indexes past the end turns a failure into a crash
std::string summaryOf(std::string const& text) {
    auto const at = text.find("  Summary\n");
    if (std::string::npos == at) {
        return std::string();
    }
    return text.substr(at + std::string("  Summary\n").size());
}

std::string renderText(CompareResult const& compareResult) {
    std::ostringstream oss;
    oss << compareResult;
    return oss.str();
}

// every line of the markdown table, separator included
std::vector<std::string> tableLines(std::string const& text) {
    std::vector<std::string> found;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && '|' == line[0]) {
            found.push_back(line);
        }
    }
    return found;
}

// the cells of one row, padding stripped. A leading '|' yields an empty first
// field, so the first real cell is at index 1.
std::vector<std::string> cellsOf(std::string const& line) {
    std::vector<std::string> cells;
    std::istringstream fields(line);
    std::string cell;
    while (std::getline(fields, cell, '|')) {
        auto const first = cell.find_first_not_of(' ');
        cells.push_back(
            std::string::npos == first
                ? std::string()
                : cell.substr(first, cell.find_last_not_of(' ') - first + 1));
    }
    return cells;
}

} // namespace

// NOLINTNEXTLINE
TEST_CASE("unit_compare_output_states_the_direction") {
    // relative > 1 means the alternative did the same work in less time, so it
    // is stated as "faster" and the interval is printed as it stands
    SUBCASE("faster") {
        auto const text = summaryOf(renderText(twoWay(2.5, 2.2, 2.8, 0U, 12U)));
        CHECK(text == "    `other` ran 2.50x faster than `base`\n"
                      "    95% CI [2.20 .. 2.80], 12 paired rounds, "
                      "interleaved\n");
    }

    // and below 1 the whole sentence turns over: the ratio is inverted, so the
    // interval has to invert *and* swap ends with it, or the summary claims a
    // range that does not contain its own ratio
    SUBCASE("slower") {
        auto const text =
            summaryOf(renderText(twoWay(0.4, 0.35, 0.45, 0U, 12U)));
        CHECK(text == "    `other` ran 2.50x slower than `base`\n"
                      "    95% CI [2.22 .. 2.86], 12 paired rounds, "
                      "interleaved\n");
    }

    // an interval that straddles 1 resolved nothing, and saying so is the whole
    // point of printing an interval rather than a bare percentage
    SUBCASE("not resolved") {
        auto const text =
            summaryOf(renderText(twoWay(1.02, 0.95, 1.1, 0U, 12U)));
        CHECK(text == "    no difference resolved between `base` and `other`\n"
                      "    ratio 1.02x, 95% CI [0.95 .. 1.10], 12 paired "
                      "rounds, interleaved\n");
    }

    // rounds where both sides landed on the same clock tick cannot say which
    // was faster, and the reader is told how many there were
    SUBCASE("tied rounds") {
        auto const text = summaryOf(renderText(twoWay(2.5, 2.2, 2.8, 3U, 12U)));
        CHECK(text == "    `other` ran 2.50x faster than `base`\n"
                      "    95% CI [2.20 .. 2.80], 12 paired rounds, "
                      "interleaved\n"
                      "    up to 3 of 12 rounds tied at the clock's "
                      "resolution\n");
    }
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_output_names_the_winner_and_the_runner_up") {
    // Three alternatives take the other summary shape: a winner is a selection,
    // so what is claimed is the lead over the *runner-up*.
    //
    // The Results are measured, but nothing below compares one measurement to
    // another. They are captured first and then treated as fixed data, so both
    // the code and this test read the same numbers and must agree on them -
    // whichever way round the machine happened to order them.
    //
    // The order of the entries is the point of running this twice. The search
    // for the runner-up walks every entry that is not the winner, and the two
    // ways of getting that walk wrong need opposite arrangements to show up:
    // starting at 1 instead of 0 only misses a runner-up that sits at 0, and
    // walking backwards off the end only misses one that is not reached first.
    size_t const rounds = 16;
    std::vector<int> order;
    SUBCASE("runner-up first") {
        order = {4, 1, 16};
    }
    SUBCASE("winner first") {
        order = {1, 4, 16};
    }
    REQUIRE(order.size() == 3U);

    char const* const names[] = {"a", "b", "c"};
    std::vector<CompareResult::Entry> entries;
    for (size_t i = 0; i < order.size(); ++i) {
        entries.emplace_back(names[i], measured(names[i], order[i], rounds),
                             1.0, 1.0, 1.0, 0U);
    }
    CompareResult const compareResult(std::move(entries), rounds);

    auto const best = compareResult.fastest();
    REQUIRE(best < compareResult.size());

    // the fastest of everything that is not the winner
    size_t runnerUp = compareResult.size();
    auto runnerUpMedian = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < compareResult.size(); ++i) {
        if (i == best) {
            continue;
        }
        auto const median =
            compareResult[i].result.median(Result::Measure::elapsed);
        if (median < runnerUpMedian) {
            runnerUpMedian = median;
            runnerUp = i;
        }
    }
    REQUIRE(runnerUp < compareResult.size());
    REQUIRE(runnerUp != best);

    auto const logRatios = nb::pairedLogRatios(compareResult[runnerUp].result,
                                               compareResult[best].result);
    auto const interval = nb::medianInterval(
        logRatios, nb::bonferroniConfidence(compareResult.comparisons()));
    auto const lead = std::exp(nb::medianOf(logRatios));
    auto const leadLow = std::exp(interval.first);
    auto const leadHigh = std::exp(interval.second);

    auto twoDecimals = [](double value) {
        std::ostringstream ss;
        ss.imbue(std::locale::classic());
        ss << std::setprecision(2) << std::fixed << value;
        return ss.str();
    };

    auto const bestName = "`" + compareResult[best].name + "`";
    auto const runnerUpName = "`" + compareResult[runnerUp].name + "`";
    std::string expected = "    " + bestName;
    if (leadLow > 1.0) {
        expected += " is fastest of 3, " + twoDecimals(lead) + "x ahead of " +
                    runnerUpName + "\n";
    } else {
        expected += " and " + runnerUpName +
                    " are the two fastest of 3, and were not separated\n";
    }
    expected += "    95% CI [" + twoDecimals(leadLow) + " .. " +
                twoDecimals(leadHigh) +
                "], intervals corrected for 2 comparisons, 16 paired rounds, "
                "interleaved\n";

    auto const text = renderText(compareResult);
    INFO(text);
    CHECK(summaryOf(text) == expected);
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_output_table_has_a_row_per_entry") {
    auto const text = renderText(twoWay(2.5, 2.2, 2.8, 0U, 12U));
    INFO(text);
    auto const lines = tableLines(text);

    // a header, a separator, and one row per entry
    REQUIRE(lines.size() == 4U);
    auto const header = cellsOf(lines[0]);
    auto const baseline = cellsOf(lines[2]);
    auto const other = cellsOf(lines[3]);
    REQUIRE(header.size() >= 3U);
    REQUIRE(baseline.size() >= 3U);
    REQUIRE(other.size() >= 3U);

    CHECK(header[1] == "relative");
    CHECK(header[2] == "95% CI");

    // the ratio each row carries, as a percentage of the baseline
    CHECK(baseline[1] == "100.0%");
    CHECK(other[1] == "250.0%");

    // the baseline is the reference, so it has no interval of its own - and the
    // rows are in entry order, baseline first
    CHECK(baseline[2].empty());
    CHECK(other[2] == "220.0% .. 280.0%");
    CHECK(baseline.back() == "`base`");
    CHECK(other.back() == "`other`");

    // The table heads itself from the *baseline's* config, not from whichever
    // entry happens to be at hand - the two carry different titles so that this
    // can tell them apart.
    CHECK(header.back() == "baseline title");

    // The header holds no measured value, so it is the same on every machine
    // and its two comparison columns can be held to an exact width. Checking
    // the offsets against each other cannot do that: a width that changes
    // shifts every line by the same amount, and a uniform shift satisfies
    // every relative check there is.
    CHECK(lines[0] == "| relative |              95% CI |"
                      "               ns/op |                op/s |"
                      "    err% |     total | baseline title");

    // and the rest of the table still has to line up with it
    auto const bar = lines[0].rfind('|');
    REQUIRE(bar != std::string::npos);
    for (auto const& line : lines) {
        INFO(line);
        CHECK(line.rfind('|') == bar);
    }

    // the separator's last cell is a dash per title character plus one, which
    // is the one place the table is deliberately not rectangular
    CHECK(lines[1].substr(bar + 1) ==
          ":" + std::string(std::string("baseline title").size() + 1U, '-'));
}

// NOLINTNEXTLINE
TEST_CASE("unit_compare_output_writes_nothing_for_an_empty_result") {
    // nothing was compared, so there is no table to head and no verdict to
    // reach - and every accessor below would be reading entry 0 of nothing
    CompareResult const empty({}, 0U);
    CHECK(renderText(empty).empty());
}
