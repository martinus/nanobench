#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

// Rng is part of the public API and is documented as reproducible: the same
// seed gives the same sequence, which is what makes a benchmark's input data
// comparable between two runs of the same binary and between machines. Nothing
// used to check that. This file pins the algorithm down two ways - against an
// independent transcription of the reference generator, and against values
// recorded from a known-good build - so that a change to the arithmetic cannot
// pass silently and quietly alter everyone's benchmark inputs.
//
// This replaces a file that carried a copy of the *RomuTrio* reference
// implementation and never compared it against anything: it printed a hundred
// numbers to stdout and asserted nothing. nanobench uses RomuDuoJr, which is a
// different generator, so even reading it did not say whether Rng was right.
namespace {

// RomuDuoJr, transcribed from romu-random.org. Deliberately written out again
// rather than calling Rng: two copies of the same bug would agree with each
// other, two independent transcriptions of the paper do not.
// Not called rotl: C++20's <bit> declares std::rotl, and a name clash between a
// test helper and something the standard library puts in scope has broken this
// suite before.
ANKERL_NANOBENCH_NO_SANITIZE("integer", "undefined")
uint64_t referenceRotl(uint64_t x, unsigned k) noexcept {
    return (x << k) | (x >> (64U - k));
}

class ReferenceRomuDuoJr {
public:
    ReferenceRomuDuoJr(uint64_t x, uint64_t y) noexcept
        : mX(x)
        , mY(y) {}

    ANKERL_NANOBENCH_NO_SANITIZE("integer", "undefined")
    uint64_t operator()() noexcept {
        auto const xp = mX;
        mX = UINT64_C(15241094284759029579) * mY;
        mY = referenceRotl(mY - xp, 27);
        return xp;
    }

private:
    uint64_t mX;
    uint64_t mY;
};

// splitMix64, as used by Rng's seeding. Also transcribed independently.
ANKERL_NANOBENCH_NO_SANITIZE("integer", "undefined")
uint64_t referenceSplitMix64(uint64_t& state) noexcept {
    uint64_t z = (state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27U)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31U);
}

// The documented seeding procedure: two splitMix64 draws for the state, then
// ten outputs discarded to wash out the seed.
ReferenceRomuDuoJr referenceSeeded(uint64_t seed) noexcept {
    auto const x = referenceSplitMix64(seed);
    auto const y = referenceSplitMix64(seed);
    ReferenceRomuDuoJr rng(x, y);
    for (int i = 0; i < 10; ++i) {
        rng();
    }
    return rng;
}

} // namespace

// NOLINTNEXTLINE
TEST_CASE("unit_rng_matches_reference_implementation") {
    for (uint64_t seed :
         {UINT64_C(0), UINT64_C(1), UINT64_C(123), UINT64_C(0xdeadbeef),
          UINT64_C(18446744073709551615)}) {
        INFO("seed " << seed);
        ankerl::nanobench::Rng rng(seed);
        auto reference = referenceSeeded(seed);
        for (int i = 0; i < 100; ++i) {
            REQUIRE(rng() == reference());
        }
    }
}

// Independent transcriptions could in principle both drift if the generator
// were replaced wholesale, so the exact sequence is also written down. These
// numbers come from a build that was known good; they are pure 64 bit integer
// arithmetic, so every platform nanobench supports has to produce them.
// NOLINTNEXTLINE
TEST_CASE("unit_rng_golden_values") {
    ankerl::nanobench::Rng rng(123);
    std::vector<uint64_t> const expected{
        UINT64_C(2222826058396098479),  UINT64_C(2602822925082020875),
        UINT64_C(13335048783801050212), UINT64_C(4695492016416831756),
        UINT64_C(18018038420000216227), UINT64_C(13968402377300554245),
        UINT64_C(6913358089879074968),  UINT64_C(14116198576347357923),
    };
    for (auto e : expected) {
        REQUIRE(rng() == e);
    }

    // a different seed has to give a different stream - a seeding step that
    // accidentally ignored its argument would still pass everything above
    ankerl::nanobench::Rng other(124);
    CHECK(other() != UINT64_C(2222826058396098479));

    // seed 0 is not special cased away into a fixed state
    ankerl::nanobench::Rng zero(0);
    CHECK(zero() == UINT64_C(12088876436681977425));
}

// NOLINTNEXTLINE
TEST_CASE("unit_rng_state_roundtrip") {
    ankerl::nanobench::Rng rng{};
    for (size_t i = 0; i < 10; ++i) {
        rng();
    }
    auto const state = rng.state();
    REQUIRE(state.size() == 2U);

    ankerl::nanobench::Rng rngLoaded(state);
    for (size_t i = 0; i < 100; ++i) {
        REQUIRE(rng() == rngLoaded());
    }

    // state() is a snapshot, not a live view: draining the original must not
    // change what was already read out of it
    auto const snapshot = rng.state();
    rng();
    CHECK(rng.state() != snapshot);
    CHECK(snapshot.size() == 2U);
}

// NOLINTNEXTLINE
TEST_CASE("unit_rng_copy_is_independent") {
    ankerl::nanobench::Rng rng(4711);
    rng();

    auto copy = rng.copy();
    // same state, so the same continuation
    for (int i = 0; i < 20; ++i) {
        REQUIRE(rng() == copy());
    }

    // and advancing one must not advance the other
    auto frozen = rng.copy();
    auto const frozenState = frozen.state();
    for (int i = 0; i < 5; ++i) {
        rng();
    }
    CHECK(frozen.state() == frozenState);
    CHECK(frozen.state() != rng.state());
}

// NOLINTNEXTLINE
TEST_CASE("unit_rng_bounded_stays_in_range") {
    ankerl::nanobench::Rng rng(123);

    // bounded(range) must never return range itself, which is the classic
    // off-by-one in this kind of multiply-shift reduction
    for (uint32_t range : {UINT32_C(1), UINT32_C(2), UINT32_C(7),
                           UINT32_C(1000), UINT32_C(0x7fffffff)}) {
        INFO("range " << range);
        for (int i = 0; i < 2000; ++i) {
            REQUIRE(rng.bounded(range) < range);
        }
    }

    // bounded(1) has exactly one legal answer
    for (int i = 0; i < 100; ++i) {
        REQUIRE(rng.bounded(1) == UINT32_C(0));
    }
}

// NOLINTNEXTLINE
TEST_CASE("unit_rng_bounded_covers_its_range") {
    // Not a statistical test - just enough to catch a reduction that collapses
    // onto a few values, which "always < range" would happily accept.
    ankerl::nanobench::Rng rng(123);
    std::vector<int> seen(10, 0);
    for (int i = 0; i < 10000; ++i) {
        ++seen[rng.bounded(10)];
    }
    for (size_t i = 0; i < seen.size(); ++i) {
        INFO("value " << i << " seen " << seen[i] << " times");
        REQUIRE(seen[i] > 500);
    }
}

// NOLINTNEXTLINE
TEST_CASE("unit_rng_uniform01_range_and_values") {
    ankerl::nanobench::Rng rng(123);
    for (int i = 0; i < 10000; ++i) {
        auto const x = rng.uniform01();
        REQUIRE(x >= 0.0);
        REQUIRE(x < 1.0);
    }

    // uniform01 builds a double from the bit pattern of one draw, so its values
    // are as fixed as the integer sequence is
    ankerl::nanobench::Rng golden(123);
    CHECK(golden.uniform01() == doctest::Approx(0.12049964207852182));
    CHECK(golden.uniform01() == doctest::Approx(0.14109931349844995));
    CHECK(golden.uniform01() == doctest::Approx(0.72289444307986406));
}

// NOLINTNEXTLINE
TEST_CASE("unit_rng_shuffle_is_a_permutation") {
    ankerl::nanobench::Rng rng(123);

    // odd and even sizes both matter: shuffle swaps two elements per loop
    // iteration, so an off-by-one shows up at one parity only
    for (size_t n = 0; n < 33; ++n) {
        INFO("size " << n);
        std::vector<int> data;
        for (size_t i = 0; i < n; ++i) {
            data.push_back(static_cast<int>(i));
        }
        auto const original = data;
        rng.shuffle(data);

        auto sorted = data;
        std::sort(sorted.begin(), sorted.end());
        REQUIRE(sorted == original);
    }
}

// NOLINTNEXTLINE
TEST_CASE("unit_rng_shuffle_is_deterministic_and_shuffles") {
    auto shuffled = [](uint64_t seed) {
        ankerl::nanobench::Rng rng(seed);
        std::vector<int> data;
        for (int i = 0; i < 10; ++i) {
            data.push_back(i);
        }
        rng.shuffle(data);
        return data;
    };

    CHECK(shuffled(123) == std::vector<int>{5, 4, 6, 8, 3, 2, 0, 7, 1, 9});

    // same seed, same order
    CHECK(shuffled(123) == shuffled(123));
    // different seed, different order (a shuffle that did nothing would pass
    // the permutation test above)
    CHECK(shuffled(123) != shuffled(124));
}

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)

// NOLINTNEXTLINE
TEST_CASE("unit_rng_state_of_wrong_size_is_rejected") {
    // Rng(std::vector) is how a saved state is restored, so handing it
    // something that is not a state has to be an error rather than a silently
    // half initialized generator.
    for (size_t n : {size_t(0), size_t(1), size_t(3), size_t(10)}) {
        INFO("size " << n);
        std::vector<uint64_t> const state(n, UINT64_C(1));
        CHECK_THROWS_AS(ankerl::nanobench::Rng{state}, std::runtime_error);
    }

    // the message names the size it got, so a wrong state is diagnosable
    std::vector<uint64_t> const three(3, UINT64_C(1));
    CHECK_THROWS_WITH(
        ankerl::nanobench::Rng{three},
        doctest::Contains("needed exactly 2 entries in data, but got 3"));

    // exactly 2 is accepted
    std::vector<uint64_t> const ok{UINT64_C(1), UINT64_C(2)};
    CHECK_NOTHROW(ankerl::nanobench::Rng{ok});
}

#endif
