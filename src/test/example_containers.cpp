#include <app/doctest.h>
#include <nanobench.h>

#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

TEST_CASE("example_containers") {
    auto cfg = ankerl::nanobench::Config().title("random insert & erase in maps").warmup(10000);
    ankerl::nanobench::Rng rng;

    std::unordered_map<uint64_t, int> uo;
    cfg.run("std::unordered_map", [&] {
        uo[rng() & 0xff];
        uo.erase(rng() & 0xff);
    });
    ankerl::nanobench::doNotOptimizeAway(&uo);

    std::map<uint64_t, int> m;
    cfg.run("std::map", [&] {
        m[rng() & 0xff];
        m.erase(rng() & 0xff);
    });
    ankerl::nanobench::doNotOptimizeAway(&uo);

    std::unordered_set<uint64_t> us;
    cfg.run("std::unordered_set", [&] {
        us.insert(rng() & 0xff);
        us.erase(rng() & 0xff);
    });
    ankerl::nanobench::doNotOptimizeAway(&us);

    std::set<uint64_t> s;
    cfg.run("std::set", [&] {
        s.insert(rng() & 0xff);
        s.erase(rng() & 0xff);
    });
    ankerl::nanobench::doNotOptimizeAway(&s);
}
