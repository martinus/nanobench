#include <nanobench.h>

#include <fstream>
#include <iostream>
#include <random>
#include <thirdparty/doctest/doctest.h>

// Benchmarks how fast we can get 64bit random values from Rng
template <typename Rng>
void bench(ankerl::nanobench::Bench* bench, std::string name) {
    std::random_device dev;
    Rng rng(dev());
    uint64_t x = 0;
    bench->run(name, [&]() ANKERL_NANOBENCH_NO_SANITIZE("integer") { x += std::uniform_int_distribution<uint64_t>{}(rng); })
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
        uint64_t ha = a >> 32U;
        uint64_t hb = b >> 32U;
        uint64_t la = static_cast<uint32_t>(a);
        uint64_t lb = static_cast<uint32_t>(b);

        uint64_t rh = ha * hb;
        uint64_t rm0 = ha * lb;
        uint64_t rm1 = hb * la;
        uint64_t rl = la * lb;

        uint64_t t = rl + (rm0 << 32U);
        uint64_t lo = t + (rm1 << 32U);
        uint64_t c = t < rl;
        c += lo < t;
        *high = rh + (rm0 >> 32U) + (rm1 >> 32U) + c;
        return lo;
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
    ankerl::nanobench::Bench b;
    b.title("Random Number Generators").unit("uint64_t").warmup(100).relative(true);
    b.performanceCounters(true);

    // sets the first one as the baseline
    bench<std::default_random_engine>(&b, "std::default_random_engine");
    bench<std::mt19937>(&b, "std::mt19937");
    bench<std::mt19937_64>(&b, "std::mt19937_64");
    bench<std::ranlux24_base>(&b, "std::ranlux24_base");
    bench<std::ranlux48_base>(&b, "std::ranlux48_base");
    bench<std::ranlux24>(&b, "std::ranlux24_base");
    bench<std::ranlux48>(&b, "std::ranlux48");
    bench<std::knuth_b>(&b, "std::knuth_b");
    bench<ankerl::nanobench::Rng>(&b, "ankerl::nanobench::Rng");
    bench<WyRng>(&b, "WyRng");
    bench<NasamRng>(&b, "NasamRng");

    // Let's create a JSON file with all the results
    std::ofstream fout("example_random_number_generators.json");
    b.render(ankerl::nanobench::templates::json(), fout);
    fout.close();

    // A nice HTML graph too!
    fout.open("example_random_number_generators.html");
    b.render(ankerl::nanobench::templates::htmlBoxplot(), fout);
    fout.close();

    // finally, a CSV file for data reuse.
    fout.open("example_random_number_generators.csv");
    b.render(ankerl::nanobench::templates::csv(), fout);
    fout.close();

    // just generate a very simple overview of the results
    b.render("\n{{#benchmarks}}{{median_sec_per_unit}} for {{name}}\n{{/benchmarks}}", std::cout);
}
