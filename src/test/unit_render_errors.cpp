#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// A mustache template is written by hand and only ever exercised at runtime, so
// what the renderer does with a malformed one is part of its contract: it has
// to say which construct it did not understand, not render something plausible
// and wrong. Every one of these diagnostics was unreachable from the test
// suite, which also meant nothing checked that a template mistake is reported
// at all rather than silently producing an empty field.
//
// The whole file is conditional because nanobench compiles with exceptions
// disabled too (ANKERL_NANOBENCH_THROW becomes std::abort()), and a test that
// aborts the process is not a test.
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)

namespace {

using ankerl::nanobench::Result;

Result resultOf(char const* name) {
    ankerl::nanobench::Config config;
    config.mBenchmarkName = name;
    Result r{config};
    auto& pc = ankerl::nanobench::detail::performanceCounters();
    r.add(std::chrono::nanoseconds(10), 1, pc);
    return r;
}

std::vector<Result> oneResult() {
    return std::vector<Result>{resultOf("only")};
}

std::vector<Result> twoResults() {
    return std::vector<Result>{resultOf("first"), resultOf("second")};
}

void renderIgnoringOutput(char const* tpl, std::vector<Result> const& results) {
    std::ostringstream oss;
    ankerl::nanobench::render(tpl, results, oss);
}

} // namespace

// NOLINTNEXTLINE
TEST_CASE("unit_render_error_unknown_command") {
    auto const one = oneResult();

    // a command that is not one of median/average/... but whose argument is a
    // real measure
    CHECK_THROWS_WITH_AS(
        renderIgnoringOutput("{{#result}}{{mean(elapsed)}}{{/result}}", one),
        "command 'mean(elapsed)' not understood", std::runtime_error);

    // a two argument command that is not sumProduct
    CHECK_THROWS_WITH_AS(
        renderIgnoringOutput(
            "{{#result}}{{product(elapsed, iterations)}}{{/result}}", one),
        "command 'product(elapsed, iterations)' not understood",
        std::runtime_error);

    // a tag inside a result that is neither a config value nor a command
    CHECK_THROWS_WITH_AS(
        renderIgnoringOutput("{{#result}}{{whatever}}{{/result}}", one),
        "command 'whatever' not understood", std::runtime_error);

    // an unbalanced parenthesis is not silently treated as a bare name
    CHECK_THROWS_WITH_AS(
        renderIgnoringOutput("{{#result}}{{median(elapsed}}{{/result}}", one),
        "command 'median(elapsed' not understood", std::runtime_error);
}

// NOLINTNEXTLINE
TEST_CASE("unit_render_error_sections_in_the_wrong_place") {
    auto const one = oneResult();

    // {{#result}} may contain {{#measurement}} and nothing else
    CHECK_THROWS_WITH_AS(
        renderIgnoringOutput("{{#result}}{{#other}}x{{/other}}{{/result}}",
                             one),
        "got a section inside result", std::runtime_error);
    CHECK_THROWS_WITH_AS(
        renderIgnoringOutput("{{#result}}{{^other}}x{{/other}}{{/result}}",
                             one),
        "got a inverted section inside result", std::runtime_error);

    // {{#measurement}} is the innermost level, so it may contain no section at
    // all - except the -first/-last markers, which are handled before this
    CHECK_THROWS_WITH_AS(
        renderIgnoringOutput(
            "{{#measurement}}{{#other}}x{{/other}}{{/measurement}}", one),
        "got a section inside measurement", std::runtime_error);
    CHECK_THROWS_WITH_AS(
        renderIgnoringOutput(
            "{{#measurement}}{{^other}}x{{/other}}{{/measurement}}", one),
        "got a inverted section inside measurement", std::runtime_error);

    // ... and the markers really are still accepted there
    CHECK_NOTHROW(renderIgnoringOutput(
        "{{#measurement}}{{^-last}},{{/-last}}{{/measurement}}", one));
}

// NOLINTNEXTLINE
TEST_CASE("unit_render_error_top_level_sections") {
    auto const one = oneResult();

    CHECK_THROWS_WITH_AS(renderIgnoringOutput("{{#other}}x{{/other}}", one),
                         "render: unknown section 'other'", std::runtime_error);
    CHECK_THROWS_WITH_AS(renderIgnoringOutput("{{^other}}x{{/other}}", one),
                         "unknown list 'other'", std::runtime_error);
}

// NOLINTNEXTLINE
TEST_CASE("unit_render_error_measurement_needs_a_single_result") {
    // {{#measurement}} outside {{#result}} reaches into "the" result, so it
    // only has a meaning when there is exactly one. The message names the count
    // it actually got.
    CHECK_NOTHROW(renderIgnoringOutput(
        "{{#measurement}}{{elapsed}}{{/measurement}}", oneResult()));

    CHECK_THROWS_WITH_AS(
        renderIgnoringOutput("{{#measurement}}{{elapsed}}{{/measurement}}",
                             twoResults()),
        "render: can only use section 'measurement' here if there is a single "
        "result, but there are 2",
        std::runtime_error);

    std::vector<Result> const none;
    CHECK_THROWS_WITH_AS(
        renderIgnoringOutput("{{#measurement}}{{elapsed}}{{/measurement}}",
                             none),
        "render: can only use section 'measurement' here if there is a single "
        "result, but there are 0",
        std::runtime_error);
}

// NOLINTNEXTLINE
TEST_CASE("unit_render_error_unknown_top_level_tag") {
    // With several results a bare tag can only be a config value, so an unknown
    // one is an error rather than the "not understood" message that the single
    // result path produces.
    CHECK_THROWS_WITH_AS(renderIgnoringOutput("{{nosuchthing}}", twoResults()),
                         "unknown tag 'nosuchthing'", std::runtime_error);

    // a real config tag is still fine there
    CHECK_NOTHROW(renderIgnoringOutput("{{name}}", twoResults()));
}

// NOLINTNEXTLINE
TEST_CASE("unit_render_error_leaves_the_stream_usable") {
    // The renderer restores the stream's formatting from a destructor, so an
    // exception on the way out must not leave the caller's stream in whatever
    // state rendering had put it in.
    std::ostringstream oss;
    oss << std::setprecision(3) << std::fixed;

    auto const one = oneResult();
    CHECK_THROWS_AS(ankerl::nanobench::render(
                        "{{#result}}{{whatever}}{{/result}}", one, oss),
                    std::runtime_error);

    oss.clear();
    oss.str("");
    oss << 1.25;
    CHECK(oss.str() == "1.250");
}

#endif
