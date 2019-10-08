#include <app/doctest.h>
#include <nanobench.h>

#include <random>

namespace {

// performs
template <typename Rng>
ankerl::nanobench::Result bench(ankerl::nanobench::Config const& cfg) {
    Rng rng;
    uint64_t x = 0;
    return cfg.run([&] { x += std::uniform_int_distribution<uint64_t>{}(rng); }).doNotOptimizeAway(x);
}

} // namespace

TEST_CASE("random_comparison") {
    // perform a few warmup calls, and since the runtime is not always stable for each
    // generator, increase the number of epochs to get more accurate numbers.
    auto cfg = ankerl::nanobench::create().unit("uint64_t").warmup(10000).epochs(1000);

    // Get the baseline against which the other random engines are compared
    auto baseline = bench<std::default_random_engine>(cfg.name("std::default_random_engine"));
    cfg.relative(baseline);

    // benchmark all remaining random engines
    bench<std::mt19937>(cfg.name("std::mt19937"));
    bench<std::mt19937_64>(cfg.name("std::mt19937_64"));
    bench<std::ranlux24_base>(cfg.name("std::ranlux24_base"));
    bench<std::ranlux48_base>(cfg.name("std::ranlux48_base"));
    bench<std::ranlux24>(cfg.name("std::ranlux24_base"));
    bench<std::ranlux48>(cfg.name("std::ranlux48"));
    bench<std::knuth_b>(cfg.name("std::knuth_b"));
    bench<ankerl::nanobench::Rng>(cfg.name("ankerl::nanobench::Rng"));
}
