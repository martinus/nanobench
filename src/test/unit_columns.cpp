#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <algorithm>
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
