#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

// Bench is configured through a fluent chain of setters, and almost every one
// of them has a matching getter that the rest of the library reads back. A
// setter that writes the wrong field, or a getter that reads a neighbouring
// one, produces a benchmark that quietly runs with settings nobody asked for -
// there is no output that says "minEpochTime was ignored".
//
// Several of these had no caller anywhere in the test suite at all:
// minEpochTime, maxEpochTime, clockResolutionMultiple, config(Config const&),
// unit(std::string) and context(std::string, std::string).
namespace {

// A Bench that prints nothing and finishes immediately, for the cases that only
// care about bookkeeping around run().
ankerl::nanobench::Bench quiet() {
    ankerl::nanobench::Bench bench;
    bench.output(nullptr)
        .warmup(0)
        .epochs(1)
        .epochIterations(1)
        .performanceCounters(false);
    return bench;
}

} // namespace

// NOLINTNEXTLINE
TEST_CASE("unit_bench_config_defaults") {
    ankerl::nanobench::Bench const bench;

    // The documented defaults. They are part of the API - changing one changes
    // every benchmark's runtime or output.
    CHECK(bench.title() == "benchmark");
    CHECK(bench.name() == "noname");
    CHECK(bench.unit() == "op");
    CHECK(bench.timeUnitName() == "ns");
    CHECK(bench.batch() == doctest::Approx(1.0));
    CHECK(bench.epochs() == 11U);
    CHECK(bench.clockResolutionMultiple() == 1000U);
    CHECK(bench.maxEpochTime() == std::chrono::milliseconds(100));
    CHECK(bench.minEpochTime() == std::chrono::milliseconds(1));
    CHECK(bench.minEpochIterations() == UINT64_C(1));
    CHECK(bench.epochIterations() == UINT64_C(0));
    CHECK(bench.warmup() == UINT64_C(0));
    CHECK_FALSE(bench.relative());
    CHECK(bench.performanceCounters());
    CHECK(bench.complexityN() == doctest::Approx(-1.0));

    // a fresh Bench writes to stdout unless told otherwise
    CHECK(bench.output() == &std::cout);
    CHECK(bench.results().empty());
}

// NOLINTNEXTLINE
TEST_CASE("unit_bench_config_setters_round_trip") {
    ankerl::nanobench::Bench bench;
    std::ostringstream oss;

    bench.title("a title")
        .name("a name")
        .unit("byte")
        .batch(1234)
        .epochs(3)
        .clockResolutionMultiple(42)
        .maxEpochTime(std::chrono::milliseconds(77))
        .minEpochTime(std::chrono::microseconds(500))
        .minEpochIterations(999)
        .epochIterations(17)
        .warmup(5)
        .relative(true)
        .performanceCounters(false)
        .output(&oss)
        .timeUnit(std::chrono::microseconds(1), "us");

    CHECK(bench.title() == "a title");
    CHECK(bench.name() == "a name");
    CHECK(bench.unit() == "byte");
    CHECK(bench.batch() == doctest::Approx(1234.0));
    CHECK(bench.epochs() == 3U);
    CHECK(bench.clockResolutionMultiple() == 42U);
    CHECK(bench.maxEpochTime() == std::chrono::milliseconds(77));
    CHECK(bench.minEpochTime() == std::chrono::microseconds(500));
    CHECK(bench.minEpochIterations() == UINT64_C(999));
    CHECK(bench.epochIterations() == UINT64_C(17));
    CHECK(bench.warmup() == UINT64_C(5));
    CHECK(bench.relative());
    CHECK_FALSE(bench.performanceCounters());
    CHECK(bench.output() == &oss);
    CHECK(bench.timeUnitName() == "us");
    CHECK(bench.timeUnit().count() == doctest::Approx(1e-6));

    // the std::string overloads have to land in the same places as the
    // char const* ones
    std::string const asString = "from std::string";
    bench.title(asString);
    CHECK(bench.title() == asString);
    bench.name(asString);
    CHECK(bench.name() == asString);
    bench.unit(std::string("kbyte"));
    CHECK(bench.unit() == "kbyte");
}

// NOLINTNEXTLINE
TEST_CASE("unit_bench_config_min_epoch_iterations_is_never_zero") {
    // Zero iterations per epoch would divide by zero when the elapsed time is
    // turned into a per-iteration number, so the setter clamps.
    ankerl::nanobench::Bench bench;
    bench.minEpochIterations(0);
    CHECK(bench.minEpochIterations() == UINT64_C(1));

    bench.minEpochIterations(5);
    CHECK(bench.minEpochIterations() == UINT64_C(5));
    bench.minEpochIterations(0);
    CHECK(bench.minEpochIterations() == UINT64_C(1));

    // epochIterations is a different setting: 0 there means "no exact count
    // requested", so it is kept as it is
    bench.epochIterations(0);
    CHECK(bench.epochIterations() == UINT64_C(0));
}

// NOLINTNEXTLINE
TEST_CASE("unit_bench_config_context_variables") {
    ankerl::nanobench::Bench bench = quiet();

    bench.context("threads", "8");
    bench.context(std::string("mode"), std::string("release"));
    bench.run("with context", [] {});

    auto const& result = bench.results().back();
    CHECK(result.context("threads") == "8");
    CHECK(result.context("mode") == "release");

    // setting the same variable again replaces it rather than adding a second
    bench.context("threads", "16").run("again", [] {});
    CHECK(bench.results().back().context("threads") == "16");

    // clearContext removes them all, and the next result carries none
    bench.clearContext().run("cleared", [] {});
    CHECK(bench.results().back().config().mContext.empty());
}

// Changing what is being measured invalidates results that are already there,
// because relative() and the markdown table both compare rows against the
// first one. Changing only the *name* does not - that is the normal case of
// running several benchmarks into one table.
// NOLINTNEXTLINE
TEST_CASE("unit_bench_config_results_are_cleared_when_the_meaning_changes") {
    auto bench = quiet();
    bench.run("first", [] {}).run("second", [] {});
    REQUIRE(bench.results().size() == 2U);

    // a new name keeps them
    bench.name("third");
    CHECK(bench.results().size() == 2U);

    // a new unit means the old numbers are not comparable any more
    bench.unit("byte");
    CHECK(bench.results().empty());

    bench.run("a", [] {}).run("b", [] {});
    REQUIRE(bench.results().size() == 2U);

    // setting the *same* unit again is not a change, so results survive
    bench.unit("byte");
    CHECK(bench.results().size() == 2U);
    bench.unit(std::string("byte"));
    CHECK(bench.results().size() == 2U);

    // a new title starts a new table
    bench.title("another table");
    CHECK(bench.results().empty());

    bench.run("c", [] {});
    REQUIRE(bench.results().size() == 1U);
    bench.title("another table");
    CHECK(bench.results().size() == 1U);
    bench.title(std::string("another table"));
    CHECK(bench.results().size() == 1U);
}

// NOLINTNEXTLINE
TEST_CASE("unit_bench_config_assigning_a_whole_config") {
    // config(Config const&) replaces every setting at once, which is how a
    // caller stores a configuration and applies it to several Bench objects.
    ankerl::nanobench::Bench source;
    std::ostringstream oss;
    source.title("shared")
        .unit("byte")
        .epochs(3)
        .warmup(9)
        .minEpochTime(std::chrono::milliseconds(4))
        .output(&oss);

    ankerl::nanobench::Bench target;
    target.config(source.config());

    CHECK(target.title() == "shared");
    CHECK(target.unit() == "byte");
    CHECK(target.epochs() == 3U);
    CHECK(target.warmup() == UINT64_C(9));
    CHECK(target.minEpochTime() == std::chrono::milliseconds(4));
    CHECK(target.output() == &oss);

    // config() is a copy, not a link: changing the source afterwards must not
    // reach into the target
    source.epochs(7);
    CHECK(target.epochs() == 3U);
}

// NOLINTNEXTLINE
TEST_CASE("unit_bench_config_copy_and_move") {
    auto original = quiet();
    original.title("copied").unit("byte").run("measured", [] {});
    REQUIRE(original.results().size() == 1U);

    // copy carries both the settings and the results collected so far
    ankerl::nanobench::Bench copy(original);
    CHECK(copy.title() == "copied");
    CHECK(copy.unit() == "byte");
    REQUIRE(copy.results().size() == 1U);

    // and the two are independent afterwards
    copy.run("only in the copy", [] {});
    CHECK(copy.results().size() == 2U);
    CHECK(original.results().size() == 1U);

    // copy assignment
    ankerl::nanobench::Bench assigned;
    assigned = copy;
    CHECK(assigned.title() == "copied");
    CHECK(assigned.results().size() == 2U);

    // move: the moved-to object has what the source had
    ankerl::nanobench::Bench moved(std::move(copy));
    CHECK(moved.title() == "copied");
    CHECK(moved.results().size() == 2U);

    ankerl::nanobench::Bench moveAssigned;
    moveAssigned = std::move(moved);
    CHECK(moveAssigned.title() == "copied");
    CHECK(moveAssigned.results().size() == 2U);
}

// NOLINTNEXTLINE
TEST_CASE("unit_bench_config_output_can_be_turned_off") {
    // output(nullptr) is the documented way to run a benchmark without
    // printing, and it has to still collect results.
    auto bench = quiet();
    REQUIRE(bench.output() == nullptr);
    bench.run("silent", [] {});
    CHECK(bench.results().size() == 1U);

    // switching it back on writes a table again
    std::ostringstream oss;
    bench.output(&oss).run("loud", [] {});
    CHECK_FALSE(oss.str().empty());
    CHECK(oss.str().find("`loud`") != std::string::npos);
    // the silent one was never written anywhere
    CHECK(oss.str().find("`silent`") == std::string::npos);
}
