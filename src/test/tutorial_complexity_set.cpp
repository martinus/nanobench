#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <iostream>
#include <set>

TEST_CASE("tutorial_complexity_set_find") {
    // Create a single benchmark instance that is used in multiple benchmark
    // runs, with different settings for complexityN.
    ankerl::nanobench::Bench bench;

    // use a fast RNG to generate input data
    ankerl::nanobench::Rng rng;

    std::set<uint64_t> set;

    // Running the benchmark multiple times, with different number of elements
    // in the set. We successively scale numElementsInSet between 10 and
    // 1000000.
    for (size_t numElementsInSet = 10; numElementsInSet <= 1000000;
         numElementsInSet = numElementsInSet * 5 / 4) {

        // fill up the set with random data
        while (set.size() < numElementsInSet) {
            set.insert(rng());
        }

        // Run the benchmark, and provide the numElementsInSet as the scaling
        // variable.
        bench.complexityN(numElementsInSet).run("std::set find", [&] {
            ankerl::nanobench::doNotOptimizeAway(set.find(rng()));
        });
    }

    // Finally, calculate BigO complexy and print the results
    std::cout << bench.complexityBigO() << std::endl;
}
