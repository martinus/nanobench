#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <set>
#include <unordered_set>

// Benchmarks insertion and removal in multiple different containers.
// This uses a very fast random generator.
TEST_CASE("example_containers") {
    ankerl::nanobench::Config cfg;
    cfg.title("random insert & erase in containers");

    // creates a random number generator to use in the benchmarks
    ankerl::nanobench::Rng rng;
    std::set<uint64_t> s;
    cfg.run("std::set",
            [&] {
                s.insert(rng() & 0xff);
                s.erase(rng() & 0xff);
            })
        .doNotOptimizeAway(s);

    std::unordered_set<uint64_t> us;
    cfg.run("std::unordered_set",
            [&] {
                us.insert(rng() & 0xff);
                us.erase(rng() & 0xff);
            })
        .doNotOptimizeAway(us);
}
