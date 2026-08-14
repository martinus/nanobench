#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// The markdown table is what almost every user of nanobench actually looks at,
// and it is assembled by hand from padded columns. Two properties of it are
// easy to break and hard to notice: that a benchmark name is escaped so it
// cannot end the code span it sits in, and that the table stays rectangular.
namespace {

void configure(ankerl::nanobench::Bench& bench, std::ostream& os,
               char const* title) {
    bench.output(&os)
        .title(title)
        .warmup(0)
        .epochs(2)
        .epochIterations(1)
        .performanceCounters(false);
}

std::vector<std::string> tableLines(std::string const& markdown) {
    std::vector<std::string> lines;
    std::istringstream in(markdown);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && '|' == line[0]) {
            lines.push_back(line);
        }
    }
    return lines;
}

size_t countOf(std::string const& haystack, char needle) {
    return static_cast<size_t>(
        std::count(haystack.begin(), haystack.end(), needle));
}

size_t countOf(std::string const& haystack, std::string const& needle) {
    size_t count = 0;
    for (size_t pos = haystack.find(needle); pos != std::string::npos;
         pos = haystack.find(needle, pos + 1)) {
        ++count;
    }
    return count;
}

} // namespace

// NOLINTNEXTLINE
TEST_CASE("unit_markdown_name_is_escaped") {
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    configure(bench, oss, "escaping");

    // A backtick in the name would otherwise close the code span the name is
    // wrapped in, and the rest of the row would be rendered as prose.
    bench.run("a`b", [] {});
    INFO(oss.str());
    CHECK(oss.str().find("`a``b`") != std::string::npos);

    // one backtick in the name becomes two, plus the pair that wraps it
    bench.run("``", [] {});
    CHECK(oss.str().find("``````") != std::string::npos);

    // a name without backticks is wrapped but otherwise untouched
    bench.run("plain", [] {});
    CHECK(oss.str().find("`plain`") != std::string::npos);
}

// NOLINTNEXTLINE
TEST_CASE("unit_markdown_table_is_rectangular") {
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    configure(bench, oss, "rectangular");

    // err% is hidden on purpose, and the reason is the interesting part. A
    // value wider than the column it goes in widens the cell, and nanobench
    // cannot prevent that: the table is streamed a row at a time, so a column
    // cannot be widened retroactively once a later value turns out not to fit.
    // A benchmark this short can report an enormous relative error on a loaded
    // machine - a CI runner produced `err% = 10,139.9%`, one character too wide
    // for a column sized for "err%" - which shifted that row and made this test
    // fail for a property the library never promised. Every other column here
    // holds a value whose width is bounded.
    bench.hideColumn(ankerl::nanobench::Column::error);
    bench.contextColumn("threads");
    bench.context("threads", "8").run("with", [] {});
    bench.clearContext().run("without", [] {});

    auto const lines = tableLines(oss.str());
    INFO(oss.str());
    REQUIRE(lines.size() >= 4U); // header, separator, two rows

    // every line of one table has to have the same number of cell boundaries,
    // or the table stops being a table
    auto const expected = countOf(lines.front(), '|');
    for (auto const& line : lines) {
        INFO("line: " << line);
        CHECK(countOf(line, '|') == expected);
    }

    // Only the last cell is variable width - it holds the title on the header
    // line and the benchmark name on a data line - so what has to line up is
    // everything before it: the last '|' of every line sits at the same offset.
    auto const lastBar = lines.front().rfind('|');
    REQUIRE(lastBar != std::string::npos);
    for (auto const& line : lines) {
        INFO("line: " << line);
        CHECK(line.rfind('|') == lastBar);
    }

    // The header and the separator are built from the column widths alone, with
    // no measured value in them, so those two line up whatever the machine did.
    CHECK(lines[1].rfind('|') == lines[0].rfind('|'));
}

// NOLINTNEXTLINE
TEST_CASE("unit_markdown_table_is_rectangular_with_counters") {
    // Same property as above, with the performance counters left on. Which
    // counter columns appear is a property of the machine, so this asserts
    // alignment rather than any particular column - on a machine that has no
    // counters it simply degenerates to the case above.
    //
    // The trap it guards is a row losing a cell for what it *measured*: IPC
    // used to be emitted only when the instruction and cycle medians were both
    // above zero, so a benchmark that measured no instructions at all came out
    // one cell short, and every column after it shifted left by one under a
    // header that still had it.
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    bench.output(&oss)
        .title("rectangular_counters")
        .warmup(0)
        .epochs(7)
        // unbounded width on a loaded machine - see the test above
        .hideColumn(ankerl::nanobench::Column::error);

    // An op small enough that the calibrated counter overhead accounts for all
    // of it, so its instruction median comes out at exactly zero - the same
    // shape as test_exact_iters_and_epochs, which is where this was first seen.
    // Whether it really reaches zero depends on the machine, so the assertions
    // below are about alignment and hold either way.
    size_t x = 0;
    bench.epochIterations(123).run("nothing much", [&] {
        ++x;
    });
    // counts up rather than doubling: `y += y` wraps, and -fsanitize=integer
    // fails the run on that even though the wrap is well defined
    bench.epochIterations(1).run("something", [] {
        uint64_t y = 1234;
        for (size_t i = 0; i < 100; ++i) {
            ++y;
            ankerl::nanobench::doNotOptimizeAway(y);
        }
    });

    auto const lines = tableLines(oss.str());
    INFO(oss.str());
    REQUIRE(lines.size() >= 4U); // header, separator, two rows

    auto const expected = countOf(lines.front(), '|');
    auto const lastBar = lines.front().rfind('|');
    REQUIRE(lastBar != std::string::npos);
    for (auto const& line : lines) {
        INFO("line: " << line);
        CHECK(countOf(line, '|') == expected);
        CHECK(line.rfind('|') == lastBar);
    }
}

// NOLINTNEXTLINE
TEST_CASE("unit_markdown_header_state_survives_copyfmt") {
    // Whether a header still has to be written is kept in the stream's own
    // pword storage. copyfmt copies that array wholesale, so without the
    // copyfmt_event hook two streams would end up sharing one allocation:
    // the second table would come out headerless, and both streams would free
    // the same pointer on destruction.
    std::ostringstream first;
    ankerl::nanobench::Bench benchFirst;
    configure(benchFirst, first, "copyfmt");
    benchFirst.run("row", [] {});
    REQUIRE(countOf(first.str(), std::string("copyfmt")) == 1U);

    std::ostringstream second;
    second.copyfmt(first);

    ankerl::nanobench::Bench benchSecond;
    configure(benchSecond, second, "copyfmt");
    benchSecond.run("row", [] {});

    INFO("first:\n" << first.str() << "\nsecond:\n" << second.str());
    // the second stream is a different table and needs its own header
    CHECK(countOf(second.str(), std::string("copyfmt")) == 1U);
    CHECK(countOf(second.str(), std::string("`row`")) == 1U);
    // and writing to it must not have added anything to the first
    CHECK(countOf(first.str(), std::string("`row`")) == 1U);
}

// NOLINTNEXTLINE
TEST_CASE("unit_markdown_no_output_stream_writes_nothing") {
    // output(nullptr) has to skip the whole table building path, not build it
    // and throw it away - showResult() dereferences the stream.
    ankerl::nanobench::Bench bench;
    bench.output(nullptr)
        .warmup(0)
        .epochs(2)
        .epochIterations(1)
        .performanceCounters(false)
        .relative(true)
        .contextColumn("threads")
        .context("threads", "8");

    CHECK_NOTHROW(bench.run("silent", [] {}));
    CHECK(bench.results().size() == 1U);
}
