#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <random>

// Benchmarks how fast we can get 64bit random values from Rng
template <typename Rng>
void bench(ankerl::nanobench::Config* cfg, std::string name) {
    std::random_device dev;
    Rng rng(dev());
    uint64_t x = 0;
    cfg->run(name, [&] { x += std::uniform_int_distribution<uint64_t>{}(rng); }).doNotOptimizeAway(x);
}

TEST_CASE("example_random_number_generators") {
    // perform a few warmup calls, and since the runtime is not always stable for each
    // generator, increase the number of epochs to get more accurate numbers.
    ankerl::nanobench::Config cfg;
    cfg.title("Random Number Generators").unit("uint64_t").warmup(100).relative(true);

    // sets the first one as the baseline
    bench<std::default_random_engine>(&cfg, "std::default_random_engine");
    bench<std::mt19937>(&cfg, "std::mt19937");
    bench<std::mt19937_64>(&cfg, "std::mt19937_64");
    bench<std::ranlux24_base>(&cfg, "std::ranlux24_base");
    bench<std::ranlux48_base>(&cfg, "std::ranlux48_base");
    bench<std::ranlux24>(&cfg, "std::ranlux24_base");
    bench<std::ranlux48>(&cfg, "std::ranlux48");
    bench<std::knuth_b>(&cfg, "std::knuth_b");
    bench<ankerl::nanobench::Rng>(&cfg, "ankerl::nanobench::Rng");

    // Let's create a JSON file with all the results
    // std::ofstream fout("example_random_number_generators.json");
    // cfg.printJson(fout);
}
