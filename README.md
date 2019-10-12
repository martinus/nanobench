# ankerl::nanobench [![Release](https://img.shields.io/github/release/martinus/nanobench.svg)](https://github.com/martinus/nanobench/releases) [![GitHub license](https://img.shields.io/github/license/martinus/nanobench.svg)](https://raw.githubusercontent.com/martinus/nanobench/master/LICENSE)

[![Travis CI Build Status](https://travis-ci.com/martinus/nanobench.svg?branch=master)](https://travis-ci.com/martinus/nanobench)
[![Appveyor Build Status](https://ci.appveyor.com/api/projects/status/github/martinus/nanobench?branch=master&svg=true)](https://ci.appveyor.com/project/martinus/nanobench)


Simple, fast, accurate microbenchmarking functionality for C++11/14/17/20.

Most C++ projects often already have a unit test frameworks in place, like 
[Boost.Test](https://www.boost.org/doc/libs/1_71_0/libs/test/doc/html/index.html) or
[gtest](https://github.com/google/googletest), but no good functionality for benchmarking.
While libraries like [google/benchmark](https://github.com/google/benchmark) exist,
integrating these in an existing codebase if often not simple. [nanobench](https://github.com/martinus/nanobench/)
makes this easy.

**ankerl::nanobench** is:
* Easy to integrate - single header.
* Easy to use: Automatic configuration
* Fast: runtime is based based on the clock's accuracy.
* Accurate: overhead for measurements is kept as small as possible
* Robust: Multiple evaluations (epochs) give robust statistics which protects against outliers. Warns when System
  not prepared for benchmarking (turbo mode, frequency scaling, debug mode, ...)
* Fast to compile

## Simple Example

This code from [full_example.cpp](src/scripts/full_example.cpp):

```cpp
#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <atomic>

int main() {
    int y = 0;
    std::atomic<int> x(0);
    ankerl::nanobench::Config().run("compare_exchange_strong", [&] { x.compare_exchange_strong(y, 0); });
}
```

Compiled with `g++ -O2 -DNDEBUG full_example.cpp -I../include -o full_example` runs for 5ms and then
prints this markdown table:

| relative |               ns/op |                op/s |   MdAPE | benchmark
|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
|          |                5.83 |      171,586,715.87 |    0.1% | `compare_exchange_strong`

Which means that one `x.compare_exchange_strong(y, 0);` call takes 5.83ns on my machine, or 171 million
operations per second. Runtime fluctuates by around 0.1%, so the results are very stable.

## Advanced Example

Easily integratable into any test framework like e.g. [doctest](https://github.com/onqtam/doctest). First
put the implementation into a separate cpp file [nanobench.cpp](src/test/app/nanobench.cpp):

```cpp
#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>
```

This compiles most of nanobench, and is relatively slow - but only needs to be done once.

The useage of nanobench benchmarking compiles very fast. I have implemented a comparison of multiple random
number generators in a test [example_random_number_generators.cpp](src/test/example_random_number_generators.cpp).
Here several RNGs are compared to a baseline calculated from `std::default_random_engine`:

```cpp
#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <random>

// Benchmarks how fast we can get 64bit random values from Rng
template <typename Rng>
ankerl::nanobench::Result bench(ankerl::nanobench::Config const& cfg, std::string name) {
    Rng rng;
    uint64_t x = 0;
    return cfg.run(name, [&] { x += std::uniform_int_distribution<uint64_t>{}(rng); }).doNotOptimizeAway(x);
}

TEST_CASE("example_random_number_generators") {
    // perform a few warmup calls, and since the runtime is not always stable for each
    // generator, increase the number of epochs to get more accurate numbers.
    ankerl::nanobench::Config cfg;
    cfg.title("Random Number Generators").unit("uint64_t").warmup(10000).epochs(100);

    // Get the baseline against which the other random engines are compared
    auto baseline = bench<std::default_random_engine>(cfg, "std::default_random_engine");
    cfg.relative(baseline);

    // benchmark all remaining random engines
    bench<std::mt19937>(cfg, "std::mt19937");
    bench<std::mt19937_64>(cfg, "std::mt19937_64");
    bench<std::ranlux24_base>(cfg, "std::ranlux24_base");
    bench<std::ranlux48_base>(cfg, "std::ranlux48_base");
    bench<std::ranlux24>(cfg, "std::ranlux24_base");
    bench<std::ranlux48>(cfg, "std::ranlux48");
    bench<std::knuth_b>(cfg, "std::knuth_b");
    bench<ankerl::nanobench::Rng>(cfg, "ankerl::nanobench::Rng");
}
```

Runs for 30ms and prints this table:

| relative |         ns/uint64_t |          uint64_t/s |   MdAPE | Random Number Generators
|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
|          |               42.35 |       23,610,881.52 |    1.2% | `std::default_random_engine`
|   194.0% |               21.83 |       45,806,510.84 |    1.3% | `std::mt19937`
|   550.3% |                7.70 |      129,937,795.08 |    1.5% | `std::mt19937_64`
|    93.1% |               45.50 |       21,979,820.90 |    0.4% | `std::ranlux24_base`
|   125.1% |               33.84 |       29,547,222.95 |    0.4% | `std::ranlux48_base`
|    21.0% |              201.67 |        4,958,600.91 |    1.3% | `std::ranlux24_base`
|    11.2% |              377.41 |        2,649,623.47 |    1.8% | `std::ranlux48`
|    68.2% |               62.06 |       16,112,467.22 |    1.3% | `std::knuth_b`
| 2,054.4% |                2.06 |      485,071,457.01 |    0.1% | `ankerl::nanobench::Rng`

It shows that `ankerl::nanobench::Rng` is by far the fastest RNG, and has the least amount of
fluctuation. It takes only 2.06ns to generate a random `uint64_t`, so ~485 million calls per
seconds are possible.

# Alternatives
* [moodycamel::microbench](https://github.com/cameron314/microbench) moodycamel's microbench, probably closest to this library in spirit
* [folly Benchmark](https://github.com/facebook/folly/blob/master/folly/Benchmark.h) Part of facebook's folly
* [google Benchmark](https://github.com/google/benchmark) 
* [nonius](https://github.com/libnonius/nonius) Unmaintained?
* [celero](https://github.com/DigitalInBlue/Celero)
