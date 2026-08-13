#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <memory>
#include <sstream>
#include <string>

// Grouping benchmarks into several Bench objects with their own output streams
// used to lose headers: the "have the table settings changed" state was a
// single process-wide value, so whichever Bench wrote last overwrote it and the
// next table came out headerless (issue #112).
namespace {

size_t countHeaders(std::string const& markdown, std::string const& title) {
    size_t count = 0;
    size_t pos = markdown.find(title);
    while (pos != std::string::npos) {
        ++count;
        pos = markdown.find(title, pos + 1);
    }
    return count;
}

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
TEST_CASE("unit_multi_output_each_stream_gets_a_header") {
    std::ostringstream outA;
    std::ostringstream outB;

    ankerl::nanobench::Bench benchA;
    ankerl::nanobench::Bench benchB;
    configure(benchA, outA, "at()");
    configure(benchB, outB, "operator[]");

    // interleaved on purpose: this is what a per-stream header state has to
    // survive and a single shared one cannot
    for (int i = 0; i < 3; ++i) {
        benchA.run("map", [] {});
        benchB.run("map", [] {});
    }

    INFO("A:\n" << outA.str() << "\nB:\n" << outB.str());
    // exactly one header each, not one per row and not none
    CHECK(countHeaders(outA.str(), "at()") == 1);
    CHECK(countHeaders(outB.str(), "operator[]") == 1);
    // and each table really did get its own rows
    CHECK(countHeaders(outA.str(), "`map`") == 3);
    CHECK(countHeaders(outB.str(), "`map`") == 3);
}

// NOLINTNEXTLINE
TEST_CASE("unit_multi_output_same_stream_shares_one_header") {
    // The flip side, and the reason this is per stream rather than per Bench:
    // separate Bench objects writing to the same stream with the same settings
    // must still produce one table, not one header each.
    std::ostringstream out;

    for (int i = 0; i < 3; ++i) {
        ankerl::nanobench::Bench bench;
        configure(bench, out, "shared");
        bench.run("row", [] {});
    }

    INFO(out.str());
    CHECK(countHeaders(out.str(), "shared") == 1);
    CHECK(countHeaders(out.str(), "`row`") == 3);
}

// NOLINTNEXTLINE
TEST_CASE("unit_multi_output_state_dies_with_the_stream") {
    // A map keyed on the ostream* would keep the destroyed stream's entry and
    // hand it to whatever is allocated at that address next, which shows up as
    // a missing header. Reusing the same storage repeatedly is the cheap way to
    // provoke that.
    std::string first;
    for (int i = 0; i < 8; ++i) {
        auto out =
            std::unique_ptr<std::ostringstream>(new std::ostringstream());
        ankerl::nanobench::Bench bench;
        configure(bench, *out, "recycled");
        bench.run("row", [] {});

        INFO("iteration " << i << ":\n" << out->str());
        // every one of these is a brand new stream, so every one needs a header
        CHECK(countHeaders(out->str(), "recycled") == 1);
        if (i == 0) {
            first = out->str();
        }
    }
    CHECK_FALSE(first.empty());
}
