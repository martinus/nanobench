#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <cstddef>
#include <sstream>
#include <string>

// The thousands separators in the table used to come from a std::numpunct facet
// imbued into the stream. Installing a facet goes through __dynamic_cast, which
// crashes when the consumer builds with -fno-rtti (issue #122), so the grouping
// is done by hand now. detail::fmt::Number only exists in the implementation
// translation unit, so this checks the property through the rendered table
// instead: every number in it still has to be grouped, and grouped correctly.
namespace {

// "1,234,567.00" -> true, "1,23,4567" -> false, and "1000.00" -> false once it
// is long enough to need a separator. The rule is purely positional: counting
// from the right of the integer part, every 4th character is a comma and the
// rest are digits.
bool isGroupedCorrectly(std::string const& number) {
    // substr treats npos as "to the end", so this is the whole integer part
    auto const integerPart = number.substr(0, number.find('.'));
    if (integerPart.empty() || ',' == integerPart[0]) {
        return false;
    }

    for (std::size_t i = 0; i < integerPart.size(); ++i) {
        bool const wantComma = 0 == ((integerPart.size() - i) % 4);
        char const c = integerPart[i];
        if (wantComma != (',' == c)) {
            return false;
        }
        if (!wantComma && (c < '0' || c > '9')) {
            return false;
        }
    }
    return true;
}

} // namespace

// NOLINTNEXTLINE
TEST_CASE("unit_number_format_helper") {
    REQUIRE(isGroupedCorrectly("0.00"));
    REQUIRE(isGroupedCorrectly("999.00"));
    REQUIRE(isGroupedCorrectly("1,000.00"));
    REQUIRE(isGroupedCorrectly("1,234,567.00"));

    REQUIRE_FALSE(isGroupedCorrectly("1000.00"));  // separator missing
    REQUIRE_FALSE(isGroupedCorrectly("1,23,456")); // wrong group size
    REQUIRE_FALSE(isGroupedCorrectly("1234,567")); // leading group too long
    REQUIRE_FALSE(isGroupedCorrectly(",123"));     // empty leading group
}

// NOLINTNEXTLINE
TEST_CASE("unit_number_format_table_is_grouped") {
    std::ostringstream oss;
    ankerl::nanobench::Bench bench;
    bench.output(&oss).epochs(2).performanceCounters(false).run("grouping", [] {
        ankerl::nanobench::doNotOptimizeAway(1);
    });

    auto const markdown = oss.str();
    INFO(markdown);

    // op/s of an operation this trivial is in the millions, so the table has to
    // contain at least one grouped number - if the grouping ever silently stops
    // happening, this is what notices.
    bool sawGroupedNumber = false;
    std::istringstream lines(markdown);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty() || '|' != line[0] ||
            std::string::npos != line.find("--")) {
            continue;
        }
        std::istringstream cells(line);
        std::string cell;
        while (std::getline(cells, cell, '|')) {
            // strip padding
            auto const first = cell.find_first_not_of(' ');
            if (std::string::npos == first) {
                continue;
            }
            auto const trimmed =
                cell.substr(first, cell.find_last_not_of(' ') - first + 1);
            if (trimmed.empty() || (trimmed[0] < '0' || trimmed[0] > '9')) {
                continue;
            }
            REQUIRE(isGroupedCorrectly(trimmed));
            if (std::string::npos != trimmed.find(',')) {
                sawGroupedNumber = true;
            }
        }
    }
    REQUIRE(sawGroupedNumber);
}
