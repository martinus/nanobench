#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <cstdint>
#include <iostream>

namespace {

// Three integer hash finalizers, to be compared against each other.
ANKERL_NANOBENCH_NO_SANITIZE("integer")
uint64_t murmurHash3Finalizer(uint64_t h) {
    h ^= h >> 33U;
    h *= UINT64_C(0xff51afd7ed558ccd);
    h ^= h >> 33U;
    h *= UINT64_C(0xc4ceb9fe1a85ec53);
    h ^= h >> 33U;
    return h;
}

ANKERL_NANOBENCH_NO_SANITIZE("integer")
uint64_t splitMix64Finalizer(uint64_t h) {
    h ^= h >> 30U;
    h *= UINT64_C(0xbf58476d1ce4e5b9);
    h ^= h >> 27U;
    h *= UINT64_C(0x94d049bb133111eb);
    h ^= h >> 31U;
    return h;
}

// One multiply and one xorshift instead of two and three.
ANKERL_NANOBENCH_NO_SANITIZE("integer")
uint64_t cheapFinalizer(uint64_t h) {
    h *= UINT64_C(0x9e3779b97f4a7c15);
    h ^= h >> 29U;
    return h;
}

} // namespace

// NOLINTNEXTLINE
TEST_CASE("tutorial_compare") {
    uint64_t x = 1;

    // 52 rounds rather than the default 11. An epoch is about a millisecond, so
    // this costs a tenth of a second and buys an interval narrow enough to act
    // on. A block is one round per alternative - two here - and a count that is
    // not a whole number of blocks gets rounded up to the next one.
    auto const cheaper = ankerl::nanobench::Bench().epochs(52).compare(
        "murmurhash3",
        [&] {
            x = murmurHash3Finalizer(x);
        },
        "cheap",
        [&] {
            x = cheapFinalizer(x);
        });

    // Two finalizers of the same shape. The interesting answer here is usually
    // that the measurement cannot tell them apart, which is a thing worth being
    // told rather than a percentage to argue over.
    auto const sameShape = ankerl::nanobench::Bench().epochs(52).compare(
        "murmurhash3",
        [&] {
            x = murmurHash3Finalizer(x);
        },
        "splitmix64",
        [&] {
            x = splitMix64Finalizer(x);
        });
    ankerl::nanobench::doNotOptimizeAway(x);

    // compare() prints the table and the verdict itself. Everything behind it
    // is available as data too, which is what a script gating a pull request
    // would look at.
    for (auto const* result : {&cheaper, &sameShape}) {
        if (result->isSignificant(1)) {
            std::cout << (*result)[1].name << " vs " << (*result)[0].name
                      << ": " << (*result)[1].relative << "x (95% CI "
                      << (*result)[1].relativeLow << " .. "
                      << (*result)[1].relativeHigh << ")" << std::endl;
        } else {
            std::cout << (*result)[1].name << " vs " << (*result)[0].name
                      << ": no difference resolved" << std::endl;
        }
    }
}
