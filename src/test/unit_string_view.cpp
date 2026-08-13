#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <string>

// The string_view overloads only exist from C++17 on, and the test suite is
// built for C++11 too, so the whole file has to compile either way. Note this
// uses nanobench's own ANKERL_NANOBENCH(CXX) rather than __cplusplus directly:
// MSVC reports __cplusplus as 199711L unless /Zc:__cplusplus is passed, which
// is exactly the trap that made the original guard never fire there.
#if ANKERL_NANOBENCH(HAS_STRING_VIEW)

#    include <string_view>

// NOLINTNEXTLINE
TEST_CASE("unit_string_view_name") {
    std::string_view const asView = "from a string_view";

    ankerl::nanobench::Bench bench;
    bench.output(nullptr).epochs(1).epochIterations(1).name(asView);
    CHECK(bench.name() == "from a string_view");

    // a view over part of a larger buffer must not drag the rest along
    std::string const backing = "abcdefghij";
    bench.name(std::string_view(backing).substr(2, 3));
    CHECK(bench.name() == "cde");
    CHECK(bench.name().size() == 3);
}

// NOLINTNEXTLINE
TEST_CASE("unit_string_view_run") {
    std::string_view const asView = "run from a string_view";

    ankerl::nanobench::Bench bench;
    bench.output(nullptr).epochs(1).epochIterations(1).performanceCounters(
        false);
    bench.run(asView, [] {});

    REQUIRE(bench.results().size() == 1);
    CHECK(bench.results().back().config().mBenchmarkName ==
          "run from a string_view");
}

// NOLINTNEXTLINE
TEST_CASE("unit_string_view_overloads_stay_unambiguous") {
    // The point of these: adding the string_view overload must not make a
    // literal or a std::string ambiguous. If it did, this would not compile.
    ankerl::nanobench::Bench bench;
    bench.output(nullptr).epochs(1).epochIterations(1).performanceCounters(
        false);

    std::string const asString = "std::string";
    std::string_view const asView = "std::string_view";

    bench.name("char const*");
    CHECK(bench.name() == "char const*");
    bench.name(asString);
    CHECK(bench.name() == "std::string");
    bench.name(asView);
    CHECK(bench.name() == "std::string_view");

    bench.run("char const*", [] {});
    bench.run(asString, [] {});
    bench.run(asView, [] {});
    CHECK(bench.results().size() == 3);
}

#else

// NOLINTNEXTLINE
TEST_CASE("unit_string_view_needs_cpp17") {
    // Not skipped silently: the placeholder in the original PR meant that on
    // MSVC - where the guard never fired - the suite reported a pass while
    // testing nothing at all.
    MESSAGE("string_view overloads need C++17, built as "
            << ANKERL_NANOBENCH(CXX));
    CHECK(ANKERL_NANOBENCH(CXX) < ANKERL_NANOBENCH(CXX17));
}

#endif
