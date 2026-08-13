#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

// The mustache renderer is the whole of nanobench's "export to something else"
// story - the built-in json/csv/pyperf/htmlBoxplot templates go through it, and
// so does every custom template a user writes. What was tested of it was that
// the four built-in templates still match the copies in src/docs/_generated,
// and that rendering them does not crash. That leaves the command language
// itself - which measure a name resolves to, and what each command computes -
// entirely unchecked.
//
// Two consequences that were live when this was written: the unit-suffixed
// measures (`elapsedms`, `elapsedus`, `elapsedns`, added for issue #107 so that
// a template can report anything other than seconds) had no test at all, and
// neither did the `average`, `minimum` and `maximum` commands.
namespace {

using ankerl::nanobench::Result;

ankerl::nanobench::Config configFor(char const* name) {
    ankerl::nanobench::Config config;
    config.mBenchmarkTitle = "the title";
    config.mBenchmarkName = name;
    config.mUnit = "byte";
    config.mBatch = 2.0;
    config.mNumEpochs = 3;
    config.mWarmup = 7;
    config.mComplexityN = 128.0;
    config.mIsRelative = true;
    config.mContext["threads"] = "8";
    return config;
}

// One measurement per duration, a single iteration each, so `elapsed` of entry
// i is exactly nanos[i] nanoseconds.
Result resultOf(char const* name, std::vector<int64_t> const& nanos) {
    Result r{configFor(name)};
    auto& pc = ankerl::nanobench::detail::performanceCounters();
    for (auto ns : nanos) {
        r.add(std::chrono::nanoseconds(ns), 1, pc);
    }
    return r;
}

std::string render(char const* tpl, std::vector<Result> const& results) {
    std::ostringstream oss;
    ankerl::nanobench::render(tpl, results, oss);
    return oss.str();
}

std::string renderOne(char const* tpl, std::vector<int64_t> const& nanos) {
    return render(tpl, std::vector<Result>{resultOf("bench", nanos)});
}

// The renderer writes doubles at full precision, so comparing the text would
// pin down the formatting rather than the value. Parsing it back and comparing
// numerically is what these tests actually mean.
double renderedNumber(char const* tpl, std::vector<int64_t> const& nanos) {
    return std::stod(renderOne(tpl, nanos));
}

} // namespace

// NOLINTNEXTLINE
TEST_CASE("unit_render_measure_commands") {
    // 10, 20, 30, 40 ns - median 25, average 25, min 10, max 40, sum 100
    std::vector<int64_t> const data{10, 20, 30, 40};
    auto const ns = 1e-9;

    CHECK(renderedNumber("{{median(elapsed)}}", data) ==
          doctest::Approx(25 * ns));
    CHECK(renderedNumber("{{average(elapsed)}}", data) ==
          doctest::Approx(25 * ns));
    CHECK(renderedNumber("{{minimum(elapsed)}}", data) ==
          doctest::Approx(10 * ns));
    CHECK(renderedNumber("{{maximum(elapsed)}}", data) ==
          doctest::Approx(40 * ns));
    CHECK(renderedNumber("{{sum(elapsed)}}", data) ==
          doctest::Approx(100 * ns));

    // average and median differ as soon as the data is skewed, which is what
    // makes them two commands rather than an alias
    std::vector<int64_t> const skewed{10, 10, 10, 10, 1000};
    CHECK(renderedNumber("{{median(elapsed)}}", skewed) ==
          doctest::Approx(10 * ns));
    CHECK(renderedNumber("{{average(elapsed)}}", skewed) ==
          doctest::Approx(208 * ns));

    // two-argument command
    CHECK(renderedNumber("{{sumProduct(iterations, elapsed)}}", data) ==
          doctest::Approx(100 * ns));

    // iterations is a measure like any other
    CHECK(renderedNumber("{{sum(iterations)}}", data) == doctest::Approx(4.0));
    CHECK(renderedNumber("{{maximum(iterations)}}", data) ==
          doctest::Approx(1.0));
}

// A template has no arithmetic, so a report in anything but seconds needs the
// measure itself to carry the unit (issue #107).
// NOLINTNEXTLINE
TEST_CASE("unit_render_time_unit_suffixes") {
    std::vector<int64_t> const data{10, 20, 30, 40};

    auto const seconds = renderedNumber("{{median(elapsed)}}", data);
    REQUIRE(seconds > 0.0);

    CHECK(renderedNumber("{{median(elapsedms)}}", data) ==
          doctest::Approx(seconds * 1e3));
    CHECK(renderedNumber("{{median(elapsedus)}}", data) ==
          doctest::Approx(seconds * 1e6));
    CHECK(renderedNumber("{{median(elapsedns)}}", data) ==
          doctest::Approx(seconds * 1e9));

    // 25 nanoseconds, said four ways
    CHECK(renderedNumber("{{median(elapsedns)}}", data) ==
          doctest::Approx(25.0));

    // the suffix works for every command that returns a time, not just median
    CHECK(renderedNumber("{{average(elapsedns)}}", data) ==
          doctest::Approx(25.0));
    CHECK(renderedNumber("{{minimum(elapsedns)}}", data) ==
          doctest::Approx(10.0));
    CHECK(renderedNumber("{{maximum(elapsedns)}}", data) ==
          doctest::Approx(40.0));
    CHECK(renderedNumber("{{sum(elapsedus)}}", data) == doctest::Approx(0.1));

    // both arguments of a two-argument command scale
    CHECK(renderedNumber("{{sumProduct(iterations, elapsedns)}}", data) ==
          doctest::Approx(100.0));

    // ... but a relative error is the same number whatever the unit is, so
    // scaling it would be wrong
    auto const err =
        renderedNumber("{{medianAbsolutePercentError(elapsed)}}", data);
    CHECK(renderedNumber("{{medianAbsolutePercentError(elapsedns)}}", data) ==
          doctest::Approx(err));
    // |x - 25| / x for 10, 20, 30, 40 is 1.5, 0.25, 0.1667, 0.375;
    // the median of those four is (0.25 + 0.375) / 2
    CHECK(err == doctest::Approx(0.3125));

    // plain `elapsed` keeps meaning seconds, so existing templates are
    // unaffected
    CHECK(renderedNumber("{{median(elapsed)}}", data) ==
          doctest::Approx(25e-9));
}

// NOLINTNEXTLINE
TEST_CASE("unit_render_unknown_measure_is_zero") {
    std::vector<int64_t> const data{10, 20};

    // A name that is not a measure renders as 0 rather than throwing, which is
    // what keeps a template with a typo in it from taking the process down.
    CHECK(renderedNumber("{{median(nonsense)}}", data) == doctest::Approx(0.0));
    CHECK(renderedNumber("{{sumProduct(elapsed, nonsense)}}", data) ==
          doctest::Approx(0.0));
    CHECK(renderedNumber("{{sumProduct(nonsense, elapsed)}}", data) ==
          doctest::Approx(0.0));

    // "elapsed" with an unknown suffix is an unknown measure, not "elapsed"
    // with the suffix quietly dropped
    CHECK(renderedNumber("{{median(elapsedhours)}}", data) ==
          doctest::Approx(0.0));
    CHECK(renderedNumber("{{median(elapsedm)}}", data) == doctest::Approx(0.0));
}

// NOLINTNEXTLINE
TEST_CASE("unit_render_config_tags") {
    std::vector<Result> const results{resultOf("the name", {10, 20})};

    CHECK(render("{{title}}", results) == "the title");
    CHECK(render("{{name}}", results) == "the name");
    CHECK(render("{{unit}}", results) == "byte");
    CHECK(render("{{batch}}", results) == "2");
    CHECK(render("{{complexityN}}", results) == "128");
    CHECK(render("{{epochs}}", results) == "3");
    CHECK(render("{{warmup}}", results) == "7");
    CHECK(render("{{relative}}", results) == "1");
    CHECK(render("{{context(threads)}}", results) == "8");

    // clockResolution comes from the machine rather than the config, so only
    // its shape can be checked here
    CHECK(std::stod(render("{{clockResolution}}", results)) > 0.0);

    // surrounding content is passed through untouched
    CHECK(render("a {{name}} b", results) == "a the name b");
    CHECK(render("no tags at all", results) == "no tags at all");
    CHECK(render("", results).empty());
}

// NOLINTNEXTLINE
TEST_CASE("unit_render_sections") {
    std::vector<Result> const two{resultOf("first", {10}),
                                  resultOf("second", {20})};

    // {{#result}} repeats its body once per result
    CHECK(render("{{#result}}[{{name}}]{{/result}}", two) == "[first][second]");

    // -first and -last as sections: the body is written only for that entry
    CHECK(render("{{#result}}{{#-first}}F{{/-first}}{{name}}{{/result}}",
                 two) == "Ffirstsecond");
    CHECK(render("{{#result}}{{name}}{{#-last}}L{{/-last}}{{/result}}", two) ==
          "firstsecondL");

    // and as inverted sections, which is what the built-in templates use to
    // put a separator between entries but not after the last one
    CHECK(render("{{#result}}{{name}}{{^-last}}, {{/-last}}{{/result}}", two) ==
          "first, second");
    CHECK(render("{{#result}}{{^-first}}, {{/-first}}{{name}}{{/result}}",
                 two) == "first, second");

    // a single result is both the first and the last one
    std::vector<Result> const one{resultOf("only", {10})};
    CHECK(render("{{#result}}{{#-first}}F{{/-first}}{{#-last}}L{{/-last}}"
                 "{{/result}}",
                 one) == "FL");
    CHECK(render("{{#result}}{{^-last}}, {{/-last}}{{/result}}", one).empty());

    // no results at all: the section body is never written
    std::vector<Result> const none;
    CHECK(render("a{{#result}}{{name}}{{/result}}b", none) == "ab");
}

// NOLINTNEXTLINE
TEST_CASE("unit_render_measurement_section") {
    std::vector<Result> const one{resultOf("only", {10, 20, 30})};

    // {{#measurement}} at the top level walks the epochs of the single result
    CHECK(render("{{#measurement}}{{elapsedns}}{{^-last}};{{/-last}}"
                 "{{/measurement}}",
                 one) == "10;20;30");

    // and nested inside {{#result}} it walks that result's epochs
    std::vector<Result> const two{resultOf("a", {10, 20}), resultOf("b", {30})};
    CHECK(render("{{#result}}{{name}}:{{#measurement}}{{elapsedns}},"
                 "{{/measurement}}{{/result}}",
                 two) == "a:10,20,b:30,");

    // iterations is per epoch too
    CHECK(render("{{#measurement}}{{iterations}}{{/measurement}}", one) ==
          "111");

    // an unknown measure inside a measurement renders as 0, same as elsewhere
    CHECK(render("{{#measurement}}{{nonsense}}{{/measurement}}", one) == "000");
}

// NOLINTNEXTLINE
TEST_CASE("unit_render_tag_outside_a_result_section") {
    // With exactly one result, a bare tag can name a measure as well as a
    // config value - that is what makes the pyperf template's
    // {{sum(iterations)}} work outside any section.
    std::vector<Result> const one{resultOf("only", {10, 20})};
    CHECK(std::stod(render("{{sum(iterations)}}", one)) ==
          doctest::Approx(2.0));

    // With several results a bare tag falls back to the last one's config,
    // since there is no single measurement to report.
    std::vector<Result> const two{resultOf("first", {10}),
                                  resultOf("second", {20})};
    CHECK(render("{{name}}", two) == "second");
}

// NOLINTNEXTLINE
TEST_CASE("unit_render_string_overloads") {
    // render() and Bench::render() each take the template as char const* or as
    // std::string; the std::string ones simply had no caller in the tests.
    std::vector<Result> const one{resultOf("only", {10})};
    std::string const tpl = "{{name}}";

    std::ostringstream fromString;
    ankerl::nanobench::render(tpl, one, fromString);
    CHECK(fromString.str() == "only");

    ankerl::nanobench::Bench bench;
    bench.output(nullptr)
        .epochs(1)
        .epochIterations(1)
        .performanceCounters(false)
        .run("from bench", [] {});

    std::ostringstream benchCharPtr;
    bench.render("{{name}}", benchCharPtr);
    CHECK(benchCharPtr.str() == "from bench");

    std::ostringstream benchString;
    bench.render(tpl, benchString);
    CHECK(benchString.str() == "from bench");

    std::ostringstream resultsString;
    ankerl::nanobench::render(tpl, bench, resultsString);
    CHECK(resultsString.str() == "from bench");
}

// NOLINTNEXTLINE
TEST_CASE("unit_render_does_not_disturb_the_stream") {
    // render() sets precision and formatting on the stream it writes to, and
    // restores them afterwards - otherwise rendering a template would silently
    // change how everything written to that stream later looks.
    std::ostringstream oss;
    oss << std::setprecision(3) << std::fixed;
    oss << 1.25 << " ";

    std::vector<Result> const one{resultOf("only", {10})};
    ankerl::nanobench::render("{{name}}", one, oss);

    oss << " " << 1.25;
    CHECK(oss.str() == "1.250 only 1.250");
}
