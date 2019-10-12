# ankerl::nanobench [![Release](https://img.shields.io/github/release/martinus/nanobench.svg)](https://github.com/martinus/nanobench/releases) [![GitHub license](https://img.shields.io/github/license/martinus/nanobench.svg)](https://raw.githubusercontent.com/martinus/nanobench/master/LICENSE)

[![Travis CI Build Status](https://travis-ci.com/martinus/nanobench.svg?branch=master)](https://travis-ci.com/martinus/nanobench)
[![Appveyor Build Status](https://ci.appveyor.com/api/projects/status/github/martinus/nanobench?branch=master&svg=true)](https://ci.appveyor.com/project/martinus/nanobench)
[![Join the chat at https://gitter.im/nanobench/community](https://badges.gitter.im/nanobench/community.svg)](https://gitter.im/nanobench/community?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

`ankerl::nanobench` is a platform independent microbenchmarking library for C++11/14/17/20.

<!--ts-->
   * [Examples](#examples)
      * [Simple Example](#simple-example)
      * [Advanced Example](#advanced-example)
   * [Comparison](#comparison)
      * [Google Benchmark](#google-benchmark)
         * [Sourcecode](#sourcecode)
         * [Results](#results)
      * [nonius](#nonius)
         * [Sourcecode](#sourcecode-1)
         * [Results](#results-1)
      * [ankerl::nanobench](#ankerlnanobench)
         * [Sourcecode](#sourcecode-2)
         * [Results](#results-2)
      * [Celero](#celero)
   * [More Links](#more-links)

<!-- Added by: martinus, at: Sa Okt 12 19:12:01 CEST 2019 -->

<!--te-->

Simple, fast, accurate microbenchmarking functionality for C++11/14/17/20.

* Fast: runtime is based based on the clock's accuracy
* Accurate: overhead for measurements is kept as small as possible
* Robust: Multiple evaluations (epochs) give robust statistics which protects against outliers. Warns when System
  not prepared for benchmarking (turbo mode, frequency scaling, debug mode, ...)
* Fast to compile

# Features

* Single header library: you only need `nanobench.h`. Nothing else.
* Easily integratable in any unit test framework: Pure C++. no Macros, no runners, no global registrations
* Zero configuration: Automatically detects number of iterations for accurate measurements.
* Fast execution: runtime is based on the clock's accuracy
* Warns when system is not configured for benchmarking (currently only in Linux)
* Warns at unstable results - with suggestions
* Output in Markdown table format

# Examples

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

# Comparison

I've implemented three different benchmarks in [google Benchmark](https://github.com/google/benchmark), [nonius](https://github.com/libnonius/nonius), and [nanobench](https://github.com/martinus/nanobench) for comparison. All benchmarks are run on an i7-8700 CPU locked at 3.2GHz, using [pyperf system tune](https://pyperf.readthedocs.io/en/latest/system.html).

## Google Benchmark

### Sourcecode

```cpp
#include "benchmark.h"

#include <chrono>
#include <random>
#include <thread>

void ComparisonFast(benchmark::State& state) {
    uint64_t x = 1;
    for (auto _ : state) {
        x += x;
    }
    benchmark::DoNotOptimize(x);
}
BENCHMARK(ComparisonFast);

void ComparisonSlow(benchmark::State& state) {
    for (auto _ : state) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
BENCHMARK(ComparisonSlow);

void ComparisonFluctuating(benchmark::State& state) {
    std::random_device dev;
    std::mt19937_64 rng(dev());
    for (auto _ : state) {
        // each run, perform a random number of rng calls
        auto iterations = rng() & UINT64_C(0xff);
        for (uint64_t i = 0; i < iterations; ++i) {
            (void)rng();
        }
    }
}
BENCHMARK(ComparisonFluctuating);

BENCHMARK_MAIN();
```

### Results

Compiled & linked with `g++ -O2 main.cpp -L/home/martinus/git/benchmark/build/src -lbenchmark -lpthread -o gbench`, executing it gives this result:

```
2019-10-12 12:03:25
Running ./gbench
Run on (12 X 4600 MHz CPU s)
CPU Caches:
  L1 Data 32K (x6)
  L1 Instruction 32K (x6)
  L2 Unified 256K (x6)
  L3 Unified 12288K (x1)
Load Average: 0.21, 0.55, 0.60
***WARNING*** CPU scaling is enabled, the benchmark real time measurements may be noisy and will incur extra overhead.
----------------------------------------------------------------
Benchmark                      Time             CPU   Iterations
----------------------------------------------------------------
ComparisonFast             0.313 ns        0.313 ns   1000000000
ComparisonSlow          10137913 ns         3920 ns         1000
ComparisonFluctuating        993 ns          992 ns       706946
```

Running the tests individually takes 0.365s, 11.274 sec, 0.828sec.

## nonius

### Sourcecode

```cpp
#define NONIUS_RUNNER
#include <nonius/nonius_single.h++>

#include <chrono>
#include <random>
#include <thread>

NONIUS_PARAM(X, UINT64_C(1))

template <typename Fn>
struct volatilize_fn {
    Fn fn;
    auto operator()() const -> decltype(fn()) {
        volatile auto x = fn();
        return x;
    }
};

template <typename Fn>
auto volatilize(Fn&& fn) -> volatilize_fn<typename std::decay<Fn>::type> {
    return {std::forward<Fn>(fn)};
}

NONIUS_BENCHMARK("x += x", [](nonius::chronometer meter) {
    auto x = meter.param<X>();
    meter.measure(volatilize([&]() { return x += x; }));
})

NONIUS_BENCHMARK("sleep 10ms", [] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); })

NONIUS_BENCHMARK("random fluctuations", [](nonius::chronometer meter) {
    std::random_device dev;
    std::mt19937_64 rng(dev());
    meter.measure([&] {
        // each run, perform a random number of rng calls
        auto iterations = rng() & UINT64_C(0xff);
        for (uint64_t i = 0; i < iterations; ++i) {
            (void)rng();
        }
    });
})
```

The tests individually take 0.713sec, 1.883sec, 0.819sec. Plus a startup overhead of 1.611sec.

### Results

```
clock resolution: mean is 22.0426 ns (20480002 iterations)


new round for parameters
  X = 1

benchmarking x += x
collecting 100 samples, 56376 iterations each, in estimated 0 ns
mean: 0.391109 ns, lb 0.391095 ns, ub 0.391135 ns, ci 0.95
std dev: 9.50619e-05 ns, lb 6.25215e-05 ns, ub 0.000167224 ns, ci 0.95
found 4 outliers among 100 samples (4%)
variance is unaffected by outliers

benchmarking sleep 10ms
collecting 100 samples, 1 iterations each, in estimated 1013.66 ms
mean: 10.1258 ms, lb 10.1189 ms, ub 10.1313 ms, ci 0.95
std dev: 31.1777 μs, lb 26.5814 μs, ub 35.4952 μs, ci 0.95
found 13 outliers among 100 samples (13%)
variance is unaffected by outliers

benchmarking random fluctuations
collecting 100 samples, 23 iterations each, in estimated 2.2724 ms
mean: 1016.26 ns, lb 991.161 ns, ub 1041.66 ns, ci 0.95
std dev: 128.963 ns, lb 109.803 ns, ub 159.509 ns, ci 0.95
found 2 outliers among 100 samples (2%)
variance is severely inflated by outliers
```

## ankerl::nanobench

### Sourcecode
I use doctest as a unit test framework. 

```cpp
#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <chrono>
#include <random>
#include <thread>

TEST_CASE("comparison_fast") {
    uint64_t x = 1;
    ankerl::nanobench::Config().title("framework comparison").run("x += x", [&] { x += x; }).doNotOptimizeAway(x);
}

TEST_CASE("comparison_slow") {
    ankerl::nanobench::Config().title("framework comparison").run("sleep 10ms", [&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });
}

TEST_CASE("comparison_fluctuating") {
    std::random_device dev;
    std::mt19937_64 rng(dev());
    ankerl::nanobench::Config().title("framework comparison").run("random fluctuations", [&] {
        // each run, perform a random number of rng calls
        auto iterations = rng() & UINT64_C(0xff);
        for (uint64_t i = 0; i < iterations; ++i) {
            (void)rng();
        }
    });
}
```

### Results

```
| relative |               ns/op |                op/s |   MdAPE | framework comparison
|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
|          |                0.31 |    3,195,591,912.16 |    0.0% | `x += x`
|          |       10,141,464.00 |               98.61 |    0.0% | `sleep 10ms`
|          |              987.53 |        1,012,631.24 |    5.9% | :wavy_dash: `random fluctuations` Unstable with ~37.8 iters. Increase `minEpochIterations` to e.g. 378
```

| relative |               ns/op |                op/s |   MdAPE | framework comparison
|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
|          |                0.31 |    3,195,591,912.16 |    0.0% | `x += x`
|          |       10,141,464.00 |               98.61 |    0.0% | `sleep 10ms`
|          |              987.53 |        1,012,631.24 |    5.9% | :wavy_dash: `random fluctuations` Unstable with ~37.8 iters. Increase `minEpochIterations` to e.g. 378

The tests take 0.004s, 0.519s, 0.004s. Note that the last one shows a warning that results were unreliable due to fluctuating, it recommends increasing the minimum number of iterations per epoch. I do that and run the test again:

| relative |               ns/op |                op/s |   MdAPE | framework comparison
|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
|          |              995.14 |        1,004,882.58 |    1.8% | `random fluctuations`

Now it runs for 0.025ms and MdAPE has decreased, showing that the results are more stable.

## Celero

Unfortunately I couldn't get it working. I only got segmentation faults for my `x += x` benchmark.

# More Links
* [moodycamel::microbench](https://github.com/cameron314/microbench) moodycamel's microbench, probably closest to this library in spirit
* [folly Benchmark](https://github.com/facebook/folly/blob/master/folly/Benchmark.h) Part of facebook's folly
* [google Benchmark](https://github.com/google/benchmark) 
* [nonius](https://github.com/libnonius/nonius) Unmaintained?
* [celero](https://github.com/DigitalInBlue/Celero)
