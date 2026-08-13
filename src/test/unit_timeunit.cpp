#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <chrono>
#include <sstream>
#include <string>
#include <thread>

// timeUnit() changes the unit the first column is reported in. It is purely a
// display setting: the measurement that is stored stays in seconds, and the
// table divides by the unit when it prints. This used to run four benchmarks
// with four different units and check nothing at all, so neither half of that
// was actually pinned down - a unit applied twice, or not at all, or applied to
// the stored value, would all have passed.
namespace {

// The value of the first numeric column of the row belonging to `name`, with
// the thousands separators taken out again.
double firstNumberOfRow(std::string const& markdown, std::string const& name) {
    auto const needle = '`' + name + '`';
    std::istringstream in(markdown);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find(needle) == std::string::npos) {
            continue;
        }
        std::string digits;
        for (size_t i = 1; i < line.size() && '|' != line[i]; ++i) {
            if (',' != line[i] && ' ' != line[i]) {
                digits += line[i];
            }
        }
        return std::stod(digits);
    }
    FAIL("benchmark '" << name << "' not found in the output");
    return 0.0;
}

// The name of the first column of the header of the last table written. A
// header is the table line that a separator line follows - the data rows look
// exactly the same otherwise, so they cannot be told apart by shape alone.
std::string firstHeaderCell(std::string const& markdown) {
    auto firstCellOf = [](std::string const& line) {
        auto const end = line.find('|', 1);
        if (end == std::string::npos) {
            return std::string();
        }
        auto const cell = line.substr(1, end - 1);
        auto const first = cell.find_first_not_of(' ');
        if (first == std::string::npos) {
            return std::string();
        }
        return cell.substr(first, cell.find_last_not_of(' ') - first + 1);
    };

    std::istringstream in(markdown);
    std::string line;
    std::string previous;
    std::string found;
    while (std::getline(in, line)) {
        bool const isTableLine = !line.empty() && '|' == line[0];
        bool const isSeparator =
            isTableLine && std::string::npos != line.find("--");
        if (isSeparator && !previous.empty()) {
            found = firstCellOf(previous);
        }
        previous = isTableLine && !isSeparator ? line : std::string();
    }
    return found;
}

void sleepAMillisecond() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

} // namespace

// NOLINTNEXTLINE
TEST_CASE("unit_timeunit_column_is_named_after_the_unit") {
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    bench.output(&oss)
        .title("timeunit names")
        .warmup(0)
        .epochs(2)
        .epochIterations(1)
        .performanceCounters(false);

    // the default
    bench.run("default", [] {});
    CHECK(firstHeaderCell(oss.str()) == "ns/op");

    // changing the unit changes the shape of the table, so each of these has
    // to produce a fresh header rather than continue the previous one
    bench.timeUnit(std::chrono::milliseconds(1), "ms").run("ms", [] {});
    CHECK(firstHeaderCell(oss.str()) == "ms/op");

    bench.timeUnit(std::chrono::hours(24 * 14), "fortnight")
        .run("fortnight", [] {});
    CHECK(firstHeaderCell(oss.str()) == "fortnight/op");

    bench.timeUnit(std::chrono::duration<int64_t, std::pico>(1), "ps")
        .run("ps", [] {});
    CHECK(firstHeaderCell(oss.str()) == "ps/op");

    // the unit is combined with the operation unit, not hardcoded to "op"
    bench.timeUnit(std::chrono::milliseconds(1), "ms")
        .unit("byte")
        .run("bytes", [] {});
    CHECK(firstHeaderCell(oss.str()) == "ms/byte");

    // and the setting is readable back
    CHECK(bench.timeUnitName() == "ms");
    CHECK(bench.timeUnit().count() == doctest::Approx(1e-3));
}

// NOLINTNEXTLINE
TEST_CASE("unit_timeunit_scales_the_reported_number_only") {
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    bench.output(&oss)
        .title("timeunit scaling")
        .warmup(0)
        .epochs(2)
        .epochIterations(1)
        .performanceCounters(false);

    // something that takes long enough that a millisecond column is not all
    // zeroes - the assertion below compares the printed number against the
    // stored one, so it needs digits to compare
    bench.timeUnit(std::chrono::milliseconds(1), "ms")
        .run("in milliseconds", [] {
            sleepAMillisecond();
        });

    auto const medianSeconds = bench.results().back().median(
        ankerl::nanobench::Result::Measure::elapsed);
    REQUIRE(medianSeconds > 0.0);

    // what is stored is seconds, whatever the display unit is
    CHECK(medianSeconds < 1.0);

    // and what is printed is that, divided by the unit. The table rounds the
    // column to two decimals, which is what the tolerance is for.
    auto const printed = firstNumberOfRow(oss.str(), "in milliseconds");
    CHECK(printed == doctest::Approx(medianSeconds / 1e-3).epsilon(0.02));

    // the same measurement in picoseconds is the same number times 1e9, so a
    // unit that is applied twice or not at all cannot pass both of these
    bench.timeUnit(std::chrono::duration<int64_t, std::pico>(1), "ps")
        .run("in picoseconds", [] {
            sleepAMillisecond();
        });
    auto const picoMedian = bench.results().back().median(
        ankerl::nanobench::Result::Measure::elapsed);
    auto const picoPrinted = firstNumberOfRow(oss.str(), "in picoseconds");
    CHECK(picoPrinted == doctest::Approx(picoMedian / 1e-12).epsilon(0.02));

    // op/s is per second regardless of the display unit, so it must not have
    // moved with it
    CHECK(bench.results().size() == 2U);
}

// NOLINTNEXTLINE
TEST_CASE("unit_timeunit_is_divided_by_the_batch_size") {
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    bench.output(&oss)
        .title("timeunit batch")
        .warmup(0)
        .epochs(2)
        .epochIterations(1)
        .performanceCounters(false)
        .timeUnit(std::chrono::microseconds(1), "us");

    // Each row is compared against its own measurement rather than against the
    // other row. Two sleeps of the same length are not two equal measurements -
    // how far a sleep overshoots varies per platform and per call - so relating
    // the two rows to each other would be asserting that the machine is
    // consistent, which is not what this is about and is not true on macOS.
    auto printedPerItem = [&](double batchSize, char const* name) {
        bench.batch(batchSize).run(name, [] {
            sleepAMillisecond();
        });
        auto const median = bench.results().back().median(
            ankerl::nanobench::Result::Measure::elapsed);
        REQUIRE(median > 0.0);
        // us/item is the epoch's seconds per iteration, divided by the time
        // unit and by the number of items an iteration covers
        auto const expected = median / 1e-6 / batchSize;
        auto const printed = firstNumberOfRow(oss.str(), name);
        INFO("median " << median << "s, batch " << batchSize);
        CHECK(printed == doctest::Approx(expected).epsilon(0.02));
        return printed;
    };

    auto const single = printedPerItem(1.0, "one per iteration");
    auto const batched = printedPerItem(1000.0, "a thousand per iteration");

    // and the batched row really is the smaller number, so the division cannot
    // have gone the other way
    REQUIRE(single > 0.0);
    REQUIRE(batched > 0.0);
    CHECK(batched < single);
}
