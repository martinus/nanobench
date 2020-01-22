#include <nanobench.h>

#include <fstream>
#include <iostream>
#include <random>
#include <thirdparty/doctest/doctest.h>

// Benchmarks how fast we can get 64bit random values from Rng
template <typename Rng>
void bench(ankerl::nanobench::Config* cfg, std::string name) {
    std::random_device dev;
    Rng rng(dev());
    uint64_t x = 0;
    cfg->run(name, [&]() ANKERL_NANOBENCH_NO_SANITIZE("integer") { x += std::uniform_int_distribution<uint64_t>{}(rng); })
        .doNotOptimizeAway(x);
}

class WyRng {
public:
    using result_type = uint64_t;

    static constexpr uint64_t(min)() {
        return 0;
    }
    static constexpr uint64_t(max)() {
        return UINT64_C(0xffffffffffffffff);
    }

    WyRng(uint64_t seed) noexcept
        : mState(seed) {}

    uint64_t operator()() noexcept {
        // static constexpr uint64_t wyp0 = UINT64_C(0xa0761d6478bd642f);
        static constexpr uint64_t wyp1 = UINT64_C(0xe7037ed1a0b428db);

        ++mState;
        return mumx(mState ^ wyp1, mState);
    }

private:
    // 128bit multiply a and b, xor high and low result
    static uint64_t mumx(uint64_t a, uint64_t b) noexcept {
        uint64_t h;
        uint64_t l = umul128(a, b, &h);
        return h ^ l;
    }

    static uint64_t umul128(uint64_t a, uint64_t b, uint64_t* high) noexcept {
#if defined(__SIZEOF_INT128__)
#    if defined(__GNUC__) || defined(__clang__)
#        pragma GCC diagnostic push
#        pragma GCC diagnostic ignored "-Wpedantic"
        using uint128_t = unsigned __int128;
#        pragma GCC diagnostic pop
#    endif

        auto result = static_cast<uint128_t>(a) * static_cast<uint128_t>(b);
        *high = static_cast<uint64_t>(result >> 64U);
        return static_cast<uint64_t>(result);

#elif (defined(_MSC_VER) && SIZE_MAX == UINT64_MAX)
#    include <intrin.h> // for __umulh
#    pragma intrinsic(__umulh)
#    ifndef _M_ARM64
#        pragma intrinsic(_umul128)
#    endif
#    ifdef _M_ARM64
        *high = __umulh(a, b);
        return ((uint64_t)(a)) * (b);
#    else
        return _umul128(a, b, high);
#    endif
#else
#    error No hardware umul
#endif
    }
    uint64_t mState;
};

class NasamRng {
public:
    using result_type = uint64_t;

    static constexpr uint64_t(min)() {
        return 0;
    }
    static constexpr uint64_t(max)() {
        return UINT64_C(0xffffffffffffffff);
    }

    NasamRng(uint64_t seed) noexcept
        : mState(seed) {}

    uint64_t operator()() noexcept {
        auto x = mState++;

        // rotr(a, r) is a 64-bit rotation of a by r bits.
        x ^= rotr(x, 25) ^ rotr(x, 47);
        x *= 0x9E6C63D0676A9A99UL;
        x ^= x >> 23 ^ x >> 51;
        x *= 0x9E6D62D06F6A9A9BUL;
        x ^= x >> 23 ^ x >> 51;

        return x;
    }

private:
    // rotate right
    template <typename T>
    static T rotr(T x, size_t k) {
        return (x >> k) | (x << (8U * sizeof(T) - k));
    }

    uint64_t mState;
};

TEST_CASE("example_random_number_generators") {
    // perform a few warmup calls, and since the runtime is not always stable for each
    // generator, increase the number of epochs to get more accurate numbers.
    ankerl::nanobench::Config cfg;
    cfg.title("Random Number Generators").unit("uint64_t").warmup(100).relative(true);
    cfg.performanceCounters(true);

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
    bench<WyRng>(&cfg, "WyRng");
    bench<NasamRng>(&cfg, "NasamRng");

    // Let's create a JSON file with all the results
    std::ofstream fout("example_random_number_generators.json");
    cfg.render(ankerl::nanobench::templates::json(), fout);
    fout.close();

    // A nice HTML graph too!
    fout.open("example_random_number_generators.html");
    cfg.render(ankerl::nanobench::templates::htmlBoxplot(), fout);
    fout.close();

    // finally, a CSV file for data reuse.
    fout.open("example_random_number_generators.csv");
    cfg.render(ankerl::nanobench::templates::csv(), fout);
    fout.close();

    // just generate a very simple overview of the results
    cfg.render("\n{{#benchmarks}}{{median_sec_per_unit}} for {{name}}\n{{/benchmarks}}", std::cout);
}
