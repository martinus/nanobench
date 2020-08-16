#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <random>

TEST_CASE("shuffle_pyperf") {
    // input data for shuffling
    std::vector<uint64_t> data(500, 0);

    std::default_random_engine defaultRng(123);
    auto fout = std::ofstream("pyperf_shuffle_std.json");
    ankerl::nanobench::Bench()
        .epochs(100)
        .run("std::shuffle with std::default_random_engine",
             [&]() {
                 std::shuffle(data.begin(), data.end(), defaultRng);
             })
        .render(ankerl::nanobench::templates::pyperf(), fout);

    fout = std::ofstream("pyperf_shuffle_nanobench.json");
    ankerl::nanobench::Rng rng(123);
    ankerl::nanobench::Bench()
        .epochs(100)
        .run("ankerl::nanobench::Rng::shuffle",
             [&]() {
                 rng.shuffle(data);
             })
        .render(ankerl::nanobench::templates::pyperf(), fout);
}
