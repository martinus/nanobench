#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <iostream>
#include <random>

// Source: http://quick-bench.com/2dBt6SOQTSlztlqmlo0w7pv6iNM
// https://www.reddit.com/r/prng/comments/fchmfd/romu_fast_nonlinear_pseudorandom_number_generators/fl6lfw9/

namespace {

#define ROTL(d, lrot) (((d) << (lrot)) | ((d) >> (8 * sizeof(d) - (lrot))))

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-static-definition-in-anonymous-namespace)
static uint64_t xState = 1U, yState = 1U, zState = 1U;

ANKERL_NANOBENCH_NO_SANITIZE("integer") uint64_t romuTrio_random() {
    uint64_t const xp = xState;
    uint64_t const yp = yState;
    uint64_t const zp = zState;
    xState = 15241094284759029579U * zp;
    yState = yp - xp;
    yState = ROTL(yState, 12U);
    zState = zp - yp;
    zState = ROTL(zState, 44U);
    return xp;
}

ANKERL_NANOBENCH_NO_SANITIZE("integer") uint64_t romuDuo_random() {
    uint64_t const xp = xState;
    xState = 15241094284759029579U * yState;
    yState = ROTL(yState, 36U) + ROTL(yState, 15U) - xp;
    return xp;
}

ANKERL_NANOBENCH_NO_SANITIZE("integer") uint64_t romuDuoJr_random() {
    uint64_t const xp = xState;
    xState = 15241094284759029579U * yState;
    yState = yState - xp;
    yState = ROTL(yState, 27U);
    return xp;
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-static-definition-in-anonymous-namespace)
static uint64_t stateA = 1U, stateB = 1U;

ANKERL_NANOBENCH_NO_SANITIZE("integer") uint64_t tangle() {
    uint64_t const s = (stateA += 0xC6BC279692B5C323U);
    uint64_t const t = (stateB += 0x9E3779B97F4A7C16U);
    uint64_t const z = (s ^ s >> 31U) * t;
    return z ^ z >> 26U;
}

ANKERL_NANOBENCH_NO_SANITIZE("integer") uint64_t orbit() {
    uint64_t const s = (stateA += 0xC6BC279692B5C323U);
    uint64_t const t = ((s == 0U) ? stateB : (stateB += 0x9E3779B97F4A7C15U));
    uint64_t const z = (s ^ s >> 31U) * ((t ^ t >> 22U) | 1U);
    return z ^ z >> 26U;
}

ANKERL_NANOBENCH_NO_SANITIZE("integer") uint64_t splitmix64() {
    uint64_t z = (stateA += 0x9E3779B97F4A7C15U);
    z = (z ^ z >> 30U) * 0xbf58476d1ce4e5b9U;
    z = (z ^ z >> 27U) * 0x94d049bb133111ebU;
    return z ^ z >> 31U;
}

// NOLINTNEXTLINE
static uint64_t s[4];

ANKERL_NANOBENCH_NO_SANITIZE("integer") uint64_t xoshiroStarStar() {
    const uint64_t result = ROTL(s[1] * 5, 7U) * 9U;

    const uint64_t t = s[1] << 17U;

    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];

    s[2] ^= t;

    s[3] = ROTL(s[3], 45U);

    return result;
}

// NOLINTNEXTLINE
static uint64_t sr[4];

ANKERL_NANOBENCH_NO_SANITIZE("integer") uint64_t xoroshiroPlus() {
    const uint64_t s0 = sr[0];
    uint64_t s1 = sr[1];
    const uint64_t result = s0 + s1;

    s1 ^= s0;
    sr[0] = ROTL(s0, 24U) ^ s1 ^ (s1 << 16U); // a, b
    sr[1] = ROTL(s1, 37U);                    // c

    return result;
}

} // namespace

// NOLINTNEXTLINE
TEST_CASE("example_random2") {
    auto bench = ankerl::nanobench::Bench().relative(true);

    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
    std::mt19937_64 mt{};
    bench.run("std::mt19937_64", [&] {
        ankerl::nanobench::doNotOptimizeAway(mt());
    });

    bench.run("RomuTrio", [] {
        ankerl::nanobench::doNotOptimizeAway(romuTrio_random());
    });

    bench.run("RomuDuo", [] {
        ankerl::nanobench::doNotOptimizeAway(romuDuo_random());
    });

    bench.run("RomuDuoJr", [] {
        ankerl::nanobench::doNotOptimizeAway(romuDuoJr_random());
    });

    bench.run("Tangle", [] {
        ankerl::nanobench::doNotOptimizeAway(tangle());
    });

    bench.run("Orbit", [] {
        ankerl::nanobench::doNotOptimizeAway(orbit());
    });

    bench.run("SplitMix", [] {
        ankerl::nanobench::doNotOptimizeAway(splitmix64());
    });

    bench.run("XoshiroStarStar", [] {
        ankerl::nanobench::doNotOptimizeAway(xoshiroStarStar());
    });

    bench.run("XoroshiroPlus", [] {
        ankerl::nanobench::doNotOptimizeAway(xoroshiroPlus());
    });
}

class RomuMono32 {
public:
    explicit RomuMono32(uint32_t seed)
        : mState{(seed & UINT32_C(0x1fffffff)) + UINT32_C(1156979152)} {}

    uint16_t operator()() noexcept {
        auto const result = static_cast<uint16_t>(mState >> 16U);
        mState *= UINT32_C(3611795771);
        mState = ROTL(mState, 12U);
        return result;
    }

    uint32_t state() const noexcept {
        return mState;
    }

private:
    uint32_t mState;
};

// NOLINTNEXTLINE
TEST_CASE("romumono32_all_states" * doctest::skip()) {

    uint32_t n = 0;
    RomuMono32 rm(123);
    auto initialState = rm.state();
    do {
        rm();
        ++n;
    } while (rm.state() != initialState);

    std::cout << n << std::endl;
}
