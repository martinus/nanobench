#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

// A table line is a row of cells; the one made of dashes is the separator
// between the header and the data.
bool isTableLine(std::string const& line) {
    return line.size() > 1 && '|' == line[0];
}

bool isSeparatorLine(std::string const& line) {
    return isTableLine(line) && std::string::npos != line.find("--");
}

// "|  a |  b |" -> {"a", "b"}
std::vector<std::string> splitCells(std::string const& line) {
    std::vector<std::string> cells;
    std::istringstream cellStream(line);
    std::string cell;
    std::getline(cellStream, cell, '|'); // before the first '|'
    while (std::getline(cellStream, cell, '|')) {
        auto const first = cell.find_first_not_of(' ');
        cells.push_back(
            first == std::string::npos
                ? std::string()
                : cell.substr(first, cell.find_last_not_of(' ') - first + 1));
    }
    return cells;
}

// The header line of the table, split into trimmed cell names.
std::vector<std::string> headerCells(std::string const& markdown) {
    std::istringstream lines(markdown);
    std::string line;
    while (std::getline(lines, line)) {
        if (isTableLine(line) && !isSeparatorLine(line)) {
            return splitCells(line);
        }
    }
    return {};
}

// Every table line, unsplit. Splitting into cells throws the padding away, and
// the padding *is* the column width.
std::vector<std::string> rawTableLines(std::string const& markdown) {
    std::vector<std::string> lines;
    std::istringstream in(markdown);
    std::string line;
    while (std::getline(in, line)) {
        if (isTableLine(line)) {
            lines.push_back(line);
        }
    }
    return lines;
}

// Digits after the decimal point of a formatted number, ignoring the grouping
// commas and a trailing '%'. -1 when there is no point at all.
int decimalsOf(std::string const& cell) {
    auto text = cell;
    if (!text.empty() && '%' == text.back()) {
        text.pop_back();
    }
    text.erase(std::remove(text.begin(), text.end(), ','), text.end());
    auto const point = text.find('.');
    if (std::string::npos == point) {
        return -1;
    }
    return static_cast<int>(text.size() - point - 1);
}

// A formatted cell back as a number: grouping commas and a trailing '%' off.
// NaN when there is no number in it, which no comparison then satisfies.
double numberOf(std::string const& cell) {
    auto text = cell;
    if (!text.empty() && '%' == text.back()) {
        text.pop_back();
    }
    text.erase(std::remove(text.begin(), text.end(), ','), text.end());
    try {
        return std::stod(text);
    } catch (std::exception const&) {
        return std::numeric_limits<double>::quiet_NaN();
    }
}

// first cell, or a marker - the helpers must never index an empty vector, or a
// missing table turns a failed CHECK into a crashed test case.
std::string firstCell(std::vector<std::string> const& cells) {
    return cells.empty() ? std::string("<no row>") : cells.front();
}

bool hasCell(std::vector<std::string> const& cells, std::string const& name) {
    return cells.end() != std::find(cells.begin(), cells.end(), name);
}

// Rows are the table lines after the separator.
std::vector<std::string> dataCells(std::string const& markdown,
                                   size_t rowIndex) {
    std::istringstream lines(markdown);
    std::string line;
    bool seenSeparator = false;
    size_t seen = 0;
    while (std::getline(lines, line)) {
        if (isSeparatorLine(line)) {
            seenSeparator = true;
        } else if (seenSeparator && isTableLine(line) && seen++ == rowIndex) {
            return splitCells(line);
        }
    }
    return {};
}

// singletonHeaderHash is process wide and the header is only reprinted when the
// table's shape changes, so two test cases that configure the table identically
// would leave the second one without a header at all. Giving each its own title
// keeps them independent of the order doctest runs them in.
void configure(ankerl::nanobench::Bench& bench, std::ostream& os,
               char const* title) {
    bench.output(&os)
        .title(title)
        .warmup(0)
        .epochs(2)
        .epochIterations(1)
        .performanceCounters(false);
}

// Same, but leaving the performance counters at their default. Whether they are
// actually available is a property of the machine, so no test may require the
// counter columns to be there - only that both tables agree about them.
void configureWithCounters(ankerl::nanobench::Bench& bench, std::ostream& os,
                           char const* title) {
    bench.output(&os).title(title).warmup(0).epochs(2).epochIterations(1);
}

// The columns a comparison shares with an ordinary table: everything except the
// title and the two cells that *are* the comparison.
std::vector<std::string> measurementCells(std::string const& markdown) {
    auto cells = headerCells(markdown);
    if (!cells.empty()) {
        cells.pop_back(); // the title, which differs between the two tables
    }
    std::vector<std::string> measurements;
    for (auto const& cell : cells) {
        if (cell != "relative" && cell != "95% CI") {
            measurements.push_back(cell);
        }
    }
    return measurements;
}

} // namespace

// NOLINTNEXTLINE
TEST_CASE("unit_columns_hide_and_show") {
    using ankerl::nanobench::Column;

    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    configure(bench, oss, "hide_and_show");

    REQUIRE(bench.isColumnVisible(Column::total));
    bench.hideColumn(Column::total).hideColumn(Column::error);
    REQUIRE_FALSE(bench.isColumnVisible(Column::total));
    REQUIRE_FALSE(bench.isColumnVisible(Column::error));

    bench.run("hidden", [] {});
    auto cells = headerCells(oss.str());
    INFO(oss.str());
    REQUIRE_FALSE(cells.empty());
    CHECK_FALSE(hasCell(cells, "total"));
    CHECK_FALSE(hasCell(cells, "err%"));
    CHECK(hasCell(cells, "ns/op"));
    CHECK(hasCell(cells, "op/s"));

    // showColumn puts it back, and the table gets a fresh header because its
    // shape changed
    std::ostringstream restored;
    ankerl::nanobench::Bench back;
    configure(back, restored, "hide_and_show_restored");
    back.hideColumn(Column::total).showColumn(Column::total);
    REQUIRE(back.isColumnVisible(Column::total));
    back.run("restored", [] {});
    CHECK(hasCell(headerCells(restored.str()), "total"));
}

// NOLINTNEXTLINE
TEST_CASE("unit_columns_hiding_does_not_touch_results") {
    using ankerl::nanobench::Column;

    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    configure(bench, oss, "results_untouched");
    bench.hideColumn(Column::timePerUnit).hideColumn(Column::unitPerSecond);
    bench.run("still measured", [] {});

    // the column is gone from the table, but the measurement is still there
    CHECK_FALSE(hasCell(headerCells(oss.str()), "ns/op"));
    REQUIRE(bench.results().size() == 1);
    CHECK(bench.results().back().median(
              ankerl::nanobench::Result::Measure::elapsed) >= 0.0);
}

// NOLINTNEXTLINE
TEST_CASE("unit_columns_context") {
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    configure(bench, oss, "context");
    bench.contextColumn("threads");

    // naming the same variable twice must not give two columns
    bench.contextColumn("threads");

    bench.context("threads", "8").run("with context", [] {});

    auto const markdown = oss.str();
    INFO(markdown);
    auto const header = headerCells(markdown);
    CHECK(std::count(header.begin(), header.end(), std::string("threads")) ==
          1);
    CHECK(firstCell(header) == "threads");
    CHECK(firstCell(dataCells(markdown, 0)) == "8");
}

// NOLINTNEXTLINE
TEST_CASE("unit_columns_context_missing_is_blank") {
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    configure(bench, oss, "context_missing");
    bench.contextColumn("threads");
    bench.context("threads", "8").run("has it", [] {});
    bench.clearContext();
    bench.run("missing", [] {});

    auto const markdown = oss.str();
    INFO(markdown);
    // the column stays, so the table is still rectangular, and the cell is
    // empty
    CHECK(firstCell(dataCells(markdown, 0)) == "8");
    CHECK(firstCell(dataCells(markdown, 1)).empty());

    std::ostringstream after;
    ankerl::nanobench::Bench cleared;
    configure(cleared, after, "context_cleared");
    cleared.contextColumn("threads").clearContextColumns().run("none", [] {});
    CHECK_FALSE(hasCell(headerCells(after.str()), "threads"));
}

// NOLINTNEXTLINE
TEST_CASE("unit_columns_default_set") {
    // The default table, pinned in order. Not a style assertion: the columns
    // are assembled by hand, and a refactoring that reorders, renames or drops
    // one is otherwise invisible until somebody reads a table and finds op/s
    // where ns/op used to be.
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    configure(bench, oss, "default_set");
    bench.run("plain", [] {});

    INFO(oss.str());
    CHECK(measurementCells(oss.str()) ==
          std::vector<std::string>{"ns/op", "op/s", "err%", "total"});
}

// NOLINTNEXTLINE
TEST_CASE("unit_columns_compare_shows_the_same_measurements") {
    // A comparison measures exactly what an ordinary run measures - including
    // the performance counters, which it collects around every epoch either
    // way. Whatever an ordinary table shows for a config, the comparison table
    // for that same config has to show too.
    //
    // Deliberately not a list of expected column titles: the counters are only
    // available on some machines, and hardcoding them would make this test
    // assert the machine rather than the library.
    auto op = [] {
        uint64_t x = 1;
        ankerl::nanobench::doNotOptimizeAway(x += x);
    };

    std::ostringstream normal;
    ankerl::nanobench::Bench single;
    configureWithCounters(single, normal, "same_measurements_run");
    single.run("one", op);

    std::ostringstream compared;
    ankerl::nanobench::Bench pair;
    configureWithCounters(pair, compared, "same_measurements_compare");
    pair.compare("a", op, "b", op);

    INFO("run:\n" << normal.str() << "\ncompare:\n" << compared.str());
    auto const expected = measurementCells(normal.str());
    REQUIRE_FALSE(expected.empty());
    CHECK(measurementCells(compared.str()) == expected);

    // and the two comparison-only columns are still there, in front
    auto const header = headerCells(compared.str());
    REQUIRE(header.size() >= 2U);
    CHECK(header[0] == "relative");
    CHECK(header[1] == "95% CI");
}

// NOLINTNEXTLINE
TEST_CASE("unit_columns_compare_context") {
    // contextColumn() said nothing about being for run() only, and a comparison
    // of the same benchmark across a context variable is exactly what it is
    // for.
    auto op = [] {
        uint64_t x = 1;
        ankerl::nanobench::doNotOptimizeAway(x += x);
    };

    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    configure(bench, oss, "compare_context");
    bench.contextColumn("threads").context("threads", "8");
    bench.compare("a", op, "b", op);

    auto const markdown = oss.str();
    INFO(markdown);
    CHECK(hasCell(headerCells(markdown), "threads"));
    CHECK(dataCells(markdown, 0).size() > 2U);
}

// NOLINTNEXTLINE
TEST_CASE("unit_columns_header_pins_the_measurement_widths") {
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    configure(bench, oss, "column_widths");
    bench.run("x", [] {});

    // The header and the separator are built from the column widths alone,
    // with no measured value in them, so they read the same on every machine -
    // which makes them the one place a width can be held to an exact number.
    //
    // Nothing else can. A width that drifts moves every column after it by the
    // same amount, and the rectangularity checks compare each line against the
    // header's *own* last '|', so a uniform shift is invisible to them. A
    // mutation sweep changed all four of these widths one at a time and the
    // whole suite stayed green.
    auto const lines = rawTableLines(oss.str());
    INFO(oss.str());
    REQUIRE(lines.size() >= 2U);
    CHECK(lines[0] == "|               ns/op |                op/s |"
                      "    err% |     total | column_widths");
    CHECK(lines[1] == "|--------------------:|--------------------:|"
                      "--------:|----------:|:--------------");
}

// NOLINTNEXTLINE
TEST_CASE("unit_columns_context_width_follows_the_variable_name") {
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    configure(bench, oss, "context_widths");

    // Two names on purpose. A context column is as wide as its name plus three,
    // or eleven, whichever is larger - so a short name pins the floor and a
    // long one pins the arithmetic, and neither catches the other's mutants.
    bench.contextColumn("threads").contextColumn("manythreadshere");
    bench.context("threads", "8").context("manythreadshere", "9");
    bench.run("x", [] {});

    auto const lines = rawTableLines(oss.str());
    INFO(oss.str());
    REQUIRE(lines.size() >= 2U);
    CHECK(lines[0] == "|  threads | manythreadshere |               ns/op |"
                      "                op/s |    err% |     total |"
                      " context_widths");
    CHECK(lines[1] == "|---------:|----------------:|--------------------:|"
                      "--------------------:|--------:|----------:|"
                      ":---------------");
}

// NOLINTNEXTLINE
TEST_CASE("unit_columns_complexity_width") {
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    configure(bench, oss, "complexity_widths");
    bench.complexityN(1234567890).run("x", [] {});

    auto const lines = rawTableLines(oss.str());
    INFO(oss.str());
    REQUIRE(lines.size() >= 2U);
    CHECK(lines[0] ==
          "| complexityN |               ns/op |                op/s |"
          "    err% |     total | complexity_widths");
    CHECK(lines[1] ==
          "|------------:|--------------------:|--------------------:|"
          "--------:|----------:|:------------------");
}

// NOLINTNEXTLINE
TEST_CASE("unit_columns_precision_is_fixed") {
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    configure(bench, oss, "precision");

    // Work that no clock rounds to zero. An empty op at one iteration measures
    // exactly 0 wherever the clock is coarse enough - a macOS runner did - and
    // an epoch of zero makes the percentage error a division by zero, so the
    // cell reads `inf%` and has no decimals to count. The value here does not
    // matter, only that it is a number.
    bench.complexityN(1234567890).run("x", [] {
        for (uint64_t i = 0; i < 20000U; ++i) {
            ankerl::nanobench::doNotOptimizeAway(i);
        }
    });

    // How many decimals a column shows does not depend on what was measured,
    // which is what makes this assertable where the values themselves are not.
    // A value too wide for its column widens the cell - a loaded runner has
    // produced `err% = 10,139.9%` - but it still shows one decimal.
    auto const header = headerCells(oss.str());
    auto const row = dataCells(oss.str(), 0);
    REQUIRE(header.size() == row.size());
    REQUIRE(row.size() >= 5U);

    for (size_t i = 0; i < header.size(); ++i) {
        INFO("column " << header[i] << " = " << row[i]);
        if ("complexityN" == header[i]) {
            CHECK(decimalsOf(row[i]) == -1); // an integer count, no point
        } else if ("err%" == header[i]) {
            CHECK(decimalsOf(row[i]) == 1);
        } else if ("ns/op" == header[i] || "op/s" == header[i] ||
                   "total" == header[i]) {
            CHECK(decimalsOf(row[i]) == 2);
        }
    }
}

// NOLINTNEXTLINE
TEST_CASE("unit_columns_values_agree_with_the_result") {
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    configure(bench, oss, "values");

    // Each epoch does more work than the last, so the spread across epochs is
    // real rather than whatever the clock happened to do. err% would otherwise
    // come out an exact zero often enough to matter, and zero is the one value
    // that cannot tell a percentage from a fraction - scaling it by 100 or by
    // 1/100 leaves it zero either way, so the assertion below would hold for a
    // broken one. The REQUIRE says so out loud rather than passing vacuously.
    uint64_t epoch = 0;
    bench.epochs(4).epochIterations(1).run("x", [&] {
        ++epoch;
        for (uint64_t i = 0; i < epoch * 20000U; ++i) {
            ankerl::nanobench::doNotOptimizeAway(i);
        }
    });

    auto const markdown = oss.str();
    INFO(markdown);
    auto const header = headerCells(markdown);
    auto const row = dataCells(markdown, 0);
    REQUIRE(header.size() == row.size());

    auto cell = [&](char const* name) {
        auto const at = std::find(header.begin(), header.end(), name);
        REQUIRE(at != header.end());
        return numberOf(row[static_cast<size_t>(at - header.begin())]);
    };

    // ns/op and op/s are two readings of one median, so their product is fixed
    // whatever the machine measured - a nanosecond per operation is a billion
    // operations per second. Nothing related two measurements here: both cells
    // come off the same row. This is what notices op/s losing its guard and
    // reporting a flat zero, or dividing the wrong way round.
    auto const perOp = cell("ns/op");
    auto const perSecond = cell("op/s");
    REQUIRE(perOp > 0.0);
    CHECK(perOp * perSecond == doctest::Approx(1e9).epsilon(0.01));

    // the other two against the Result they were rendered from, which is exact
    // arithmetic with only the cell's rounding to allow for
    REQUIRE(bench.results().size() == 1U);
    auto const& result = bench.results().front();
    using Measure = ankerl::nanobench::Result::Measure;
    auto const mdape = result.medianAbsolutePercentError(Measure::elapsed);
    REQUIRE(mdape > 0.0);
    CHECK(cell("err%") == doctest::Approx(mdape * 100.0).epsilon(0.01));
    CHECK(cell("total") ==
          doctest::Approx(
              result.sumProduct(Measure::iterations, Measure::elapsed))
              .epsilon(0.01));
}

// NOLINTNEXTLINE
TEST_CASE("unit_columns_complexity_zero_is_not_a_complexity") {
    // complexityN defaults to -1, so the ordinary table says nothing about
    // where the boundary sits. Zero is the case that does: it is a number the
    // caller can actually pass, and it means there is no complexity to report.
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    configure(bench, oss, "complexity_zero");
    bench.complexityN(0).run("x", [] {});
    CHECK_FALSE(hasCell(headerCells(oss.str()), "complexityN"));
}
