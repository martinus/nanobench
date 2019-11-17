#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <set>

TEST_CASE("example_complexity") {

    ankerl::nanobench::Rng rng;
    ankerl::nanobench::Config cfg;
    for (uint64_t n = 10; n < 10000; n *= 2) {
        // prepare a set with range number of elements
        std::vector<uint64_t> data(n);
        for (size_t i = 0; i < n; ++i) {
            data[i] = rng();
        }

        // sort should be O(n log n), shuffle is O(n), so we expect O(n log n).
        cfg.complexityN(n).run("std::sort", [&] {
            std::shuffle(data.begin(), data.end(), rng);
            std::sort(data.begin(), data.end());
        });
    }

    // calculates bigO of all preconfigured complexity functions
    for (auto const& bigO : cfg.complexityBigO()) {
        std::cout << bigO << std::endl;
    }

    // calculate bigO only for O(n log n)
    // bigO = cfg.complexityBigO(ankerl::nanobench::complexity::oNLogN());

    // calculates bigO for a custom function
    // bigO = cfg.complexityBigO("O(log log n)", [](double n) { return std::log2(std::log2(n)); });
}

TEST_CASE("example_erase_front") {
    ankerl::nanobench::Config cfg;
    for (size_t range = 10; range <= 10000; range += 100) {
        std::vector<uint64_t> vec(range, 0);
        cfg.complexityN(range).run("std::vector erase front " + std::to_string(range), [&] {
            vec.erase(vec.begin());
            vec.push_back(range);
        });
    }
    for (auto const& bigO : cfg.complexityBigO()) {
        std::cout << bigO << std::endl;
    }
    // cfg.render(ankerl::nanobench::templates::csv(), std::cout);
}