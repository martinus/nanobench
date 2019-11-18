#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <set>

TEST_CASE("example_complexity_set") {
    ankerl::nanobench::Config cfg;

    ankerl::nanobench::Rng rng;
    for (size_t range = 10; range <= 1000; range = range * 3 / 2) {
        // create vector with random data
        std::set<uint64_t> set;
        for (size_t i = 0; i < range; ++i) {
            set.insert(rng());
        }

        cfg.complexityN(range).run("std::set find " + std::to_string(range),
                                   [&] { ankerl::nanobench::doNotOptimizeAway(set.find(rng())); });
    }
    std::cout << cfg.complexityBigO() << std::endl;
}

TEST_CASE("example_complexity_sort") {
    ankerl::nanobench::Rng rng;
    ankerl::nanobench::Config cfg;
    for (size_t n = 10; n < 10000; n *= 2) {
        // prepare a set with range number of elements
        std::vector<uint64_t> data(n);
        for (size_t i = 0; i < n; ++i) {
            data[i] = rng();
        }

        // sort should be O(n log n), shuffle is O(n), so we expect O(n log n).
        cfg.complexityN(n).run("std::sort " + std::to_string(n), [&] {
            std::shuffle(data.begin(), data.end(), rng);
            std::sort(data.begin(), data.end());
        });
    }

    // calculates bigO of all preconfigured complexity functions
    std::cout << cfg.complexityBigO() << std::endl;

    // calculates bigO for a custom function
    auto logLogN = cfg.complexityBigO("O(log log n)", [](double n) { return std::log2(std::log2(n)); });
    std::cout << logLogN << std::endl;
}

TEST_CASE("example_complexity_quadratic") {
    ankerl::nanobench::Config cfg;

    ankerl::nanobench::Rng rng;
    for (size_t range = 10; range <= 1000; range = range * 3 / 2) {
        // create vector with random data
        std::vector<uint64_t> vec(range, 0);
        for (auto& x : vec) {
            x = rng();
        }

        cfg.complexityN(range).run("std::vector erase front " + std::to_string(range), [&] {
            // find minimum pair
            uint64_t minVal = (std::numeric_limits<uint64_t>::max)();
            for (size_t i = 0; i < vec.size() - 1; ++i) {
                for (size_t j = 0; j < vec.size(); ++j) {
                    minVal = std::min(minVal, vec[i] - vec[j]);
                    minVal = std::min(minVal, vec[j] - vec[i]);
                }
            }
            ankerl::nanobench::doNotOptimizeAway(minVal);
        });
    }
    std::cout << cfg.complexityBigO() << std::endl;
}
