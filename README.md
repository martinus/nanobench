# ankerl::nanobench [![Release](https://img.shields.io/github/release/martinus/nanobench.svg)](https://github.com/martinus/nanobench/releases) [![GitHub license](https://img.shields.io/github/license/martinus/nanobench.svg)](https://raw.githubusercontent.com/martinus/nanobench/master/LICENSE)

[![Travis CI Build Status](https://travis-ci.com/martinus/nanobench.svg?branch=master)](https://travis-ci.com/martinus/nanobench)
[![Appveyor Build Status](https://ci.appveyor.com/api/projects/status/github/martinus/nanobench?branch=master&svg=true)](https://ci.appveyor.com/project/martinus/nanobench)
[![Join the chat at https://gitter.im/nanobench/community](https://badges.gitter.im/nanobench/community.svg)](https://gitter.im/nanobench/community?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

`ankerl::nanobench` is a platform independent microbenchmarking library for C++11/14/17/20.

<!--ts-->
   * [Features](#features)
   * [Examples](#examples)
      * [Simple Example](#simple-example)
      * [Something Fast](#something-fast)
      * [Something Slow](#something-slow)
      * [Something Unstable](#something-unstable)
      * [Comparing Results](#comparing-results)
   * [Comparison](#comparison)
      * [Google Benchmark](#google-benchmark)
         * [Sourcecode](#sourcecode)
         * [Results](#results)
      * [nonius](#nonius)
         * [Sourcecode](#sourcecode-1)
         * [Results](#results-1)
      * [Celero](#celero)
      * [Picobench](#picobench)
         * [Sourcecode](#sourcecode-2)
         * [Results](#results-2)
      * [Catch2](#catch2)
         * [Sourcecode](#sourcecode-3)
         * [Results](#results-3)
   * [Links](#links)

<!-- Added by: martinus, at: So Okt 13 22:48:10 CEST 2019 -->

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

You can find more examples in [src/test](https://github.com/martinus/nanobench/tree/master/src/test).

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

In the remaining examples, I compile nanobench's implementation once in a separate cpp file 
[nanobench.cpp](src/test/app/nanobench.cpp). This compiles most of nanobench, and is relatively slow - but
only needs to be done once. The usage of nanobench compiles very fast.


```cpp
#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>
```

I use [doctest](https://github.com/onqtam/doctest) as a unit test framework, which is like [Catch2](https://github.com/catchorg/Catch2) - but compiles much faster. It pairs well with nanobench.

## Something Fast

Let's benchmarks how fast we can do `x += x` for `uint64_t`:

```cpp
TEST_CASE("comparison_fast_v1") {
    uint64_t x = 1;
    ankerl::nanobench::Config().run("x += x", [&] { x += x; });
}
```

After 0.2ms we get this output:

| relative |               ns/op |                op/s |   MdAPE | benchmark
|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
|        - |                   - |                   - |       - | :boom: iterations overflow. Maybe your code got optimized away? `x += x`

The compiler could optimize `x += x` away because we never used the output. Let's fix this:

```cpp
TEST_CASE("comparison_fast_v2") {
    uint64_t x = 1;
    ankerl::nanobench::Config().run("x += x", [&] { x += x; }).doNotOptimizeAway(x);
}
```

This time the benchmark runs for 2.2ms and gives us a good result:

| relative |               ns/op |                op/s |   MdAPE | framework comparison
|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
|          |                0.31 |    3,195,591,912.16 |    0.0% | `x += x`

It's a very stable result. One run the op/s is 3,196 million/sec, the next time I execute it I get 3,195 million/sec.

## Something Slow

Let's benchmark if sleeping for 10ms really takes 10ms.

```cpp
TEST_CASE("comparison_slow") {
    ankerl::nanobench::Config().run("sleep 10ms", [&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });
}
```

After 517ms I get

| relative |               ns/op |                op/s |   MdAPE | framework comparison
|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
|          |       10,141,835.00 |               98.60 |    0.0% | `sleep 10ms`

So we actually take 10.141ms instead of 10ms. Next time I run it, I get 10.141. Also a very stable result.

## Something Unstable

Lets create an extreme artifical test that's hard to benchmark, because runtime fluctuates randomly: Each iteration randomly skip between 0-254 random numbers:

```cpp
TEST_CASE("comparison_fluctuating_v1") {
    std::random_device dev;
    std::mt19937_64 rng(dev());
    ankerl::nanobench::Config().run("random fluctuations", [&] {
        // each run, perform a random number of rng calls
        auto iterations = rng() & UINT64_C(0xff);
        for (uint64_t i = 0; i < iterations; ++i) {
            (void)rng();
        }
    });
}
```

After 2.3ms, I get this result:

| relative |               ns/op |                op/s |   MdAPE | benchmark
|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
|          |            1,004.05 |          995,962.31 |    7.9% | :wavy_dash: `random fluctuations` Unstable with ~38.6 iters. Increase `minEpochIterations` to e.g. 386

So on average each loop takes about 1,004ns, but we get a warning that the results are unstable. The median percentage error is ~8% which is quite high. Executed again, I get 984 ns.

Let's use the suggestion and set the minimum number of iterations to 500, and try again:

```cpp
TEST_CASE("comparison_fluctuating_v2") {
    std::random_device dev;
    std::mt19937_64 rng(dev());
    ankerl::nanobench::Config().minEpochIterations(500).run("random fluctuations", [&] {
        // each run, perform a random number of rng calls
        auto iterations = rng() & UINT64_C(0xff);
        for (uint64_t i = 0; i < iterations; ++i) {
            (void)rng();
        }
    });
}
```

The fluctuations are much better:

| relative |               ns/op |                op/s |   MdAPE | benchmark
|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
|          |              987.19 |        1,012,971.22 |    1.9% | `random fluctuations`

The results are also more stable. This time the benchmark takes 27ms.

## Comparing Results

I have implemented a comparison of multiple random
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
    cfg.title("Random Number Generators").unit("uint64_t").warmup(100);

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

Runs for 18ms and prints this table:

| relative |         ns/uint64_t |          uint64_t/s |   MdAPE | Random Number Generators
|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
|          |               42.25 |       23,668,176.85 |    1.1% | `std::default_random_engine`
|   193.1% |               21.88 |       45,712,836.12 |    2.1% | `std::mt19937`
|   572.1% |                7.39 |      135,397,066.78 |    1.0% | `std::mt19937_64`
|    89.5% |               47.19 |       21,192,450.36 |    0.6% | `std::ranlux24_base`
|   119.9% |               35.23 |       28,384,568.54 |    0.6% | `std::ranlux48_base`
|    21.0% |              200.76 |        4,980,979.23 |    1.1% | `std::ranlux24_base`
|    11.4% |              369.46 |        2,706,636.37 |    1.8% | `std::ranlux48`
|    66.6% |               63.41 |       15,769,698.89 |    1.4% | `std::knuth_b`
| 2,049.4% |                2.06 |      485,045,939.09 |    0.1% | `ankerl::nanobench::Rng`

It shows that `ankerl::nanobench::Rng` is by far the fastest RNG, and has the least amount of
fluctuation. It takes only 2.06ns to generate a random `uint64_t`, so ~485 million calls per
seconds are possible.

# Comparison

I've implemented the three different benchmarks (slow, fast, unstable) in several frameworks for comparison. All benchmarks are run on an i7-8700 CPU locked at 3.2GHz, using [pyperf system tune](https://pyperf.readthedocs.io/en/latest/system.html).

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

## Celero

Unfortunately I couldn't get it working. I only got segmentation faults for my `x += x` benchmark.

## Picobench

### Sourcecode

It took me a while to figure out that I have to configure the slow test, otherwise it would run for a looong time
since the number of iterations is hardcoded.

```cpp
#define PICOBENCH_IMPLEMENT_WITH_MAIN
#include "picobench.hpp"

#include <chrono>
#include <random>
#include <thread>

PICOBENCH_SUITE("ComparisonFast");
static void ComparisonFast(picobench::state& state) {
    uint64_t x = 1;
    for (auto _ : state) {
        x += x;
    }
    state.set_result(x);
}
PICOBENCH(ComparisonFast);

PICOBENCH_SUITE("ComparisonSlow");
void ComparisonSlow(picobench::state& state) {
    for (auto _ : state) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
PICOBENCH(ComparisonSlow).iterations({1, 2, 5, 10});

PICOBENCH_SUITE("fluctuating");
void ComparisonFluctuating(picobench::state& state) {
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
PICOBENCH(ComparisonFluctuating);
```

### Results

```
ComparisonFast:
===============================================================================
   Name (baseline is *)   |   Dim   |  Total ms |  ns/op  |Baseline| Ops/second
===============================================================================
         ComparisonFast * |       8 |     0.000 |       7 |      - |129032258.1
         ComparisonFast * |      64 |     0.000 |       1 |      - |955223880.6
         ComparisonFast * |     512 |     0.000 |       0 |      - |2265486725.7
         ComparisonFast * |    4096 |     0.001 |       0 |      - |3112462006.1
         ComparisonFast * |    8192 |     0.003 |       0 |      - |3139900345.0
===============================================================================
ComparisonSlow:
===============================================================================
   Name (baseline is *)   |   Dim   |  Total ms |  ns/op  |Baseline| Ops/second
===============================================================================
         ComparisonSlow * |       1 |    10.089 |10088827 |      - |       99.1
         ComparisonSlow * |       2 |    20.282 |10141241 |      - |       98.6
         ComparisonSlow * |       5 |    50.713 |10142656 |      - |       98.6
         ComparisonSlow * |      10 |   101.246 |10124572 |      - |       98.8
===============================================================================
fluctuating:
===============================================================================
   Name (baseline is *)   |   Dim   |  Total ms |  ns/op  |Baseline| Ops/second
===============================================================================
  ComparisonFluctuating * |       8 |     0.009 |    1166 |      - |   857632.9
  ComparisonFluctuating * |      64 |     0.065 |    1016 |      - |   983405.0
  ComparisonFluctuating * |     512 |     0.500 |     976 |      - |  1024514.3
  ComparisonFluctuating * |    4096 |     4.037 |     985 |      - |  1014687.5
  ComparisonFluctuating * |    8192 |     8.190 |     999 |      - |  1000246.2
===============================================================================
```

## Catch2

### Sourcecode

```cpp
#define CATCH_CONFIG_ENABLE_BENCHMARKING
#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <chrono>
#include <random>
#include <thread>

TEST_CASE("comparison_fast") {
    uint64_t x = 1;
    BENCHMARK("x += x") {
        return x += x;
    };
}

TEST_CASE("comparison_slow") {
    BENCHMARK("sleep 10ms") {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    };
}

TEST_CASE("comparison_fluctuating_v2") {
    std::random_device dev;
    std::mt19937_64 rng(dev());
    BENCHMARK("random fluctuations") {
        // each run, perform a random number of rng calls
        auto iterations = rng() & UINT64_C(0xff);
        for (uint64_t i = 0; i < iterations; ++i) {
            (void)rng();
        }
    };
}
```

### Results

```
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
ca is a Catch v2.9.2 host application.
Run with -? for options

-------------------------------------------------------------------------------
comparison_fast
-------------------------------------------------------------------------------
catch.cpp:9
...............................................................................

benchmark name                                  samples       iterations    estimated
                                                mean          low mean      high mean
                                                std dev       low std dev   high std dev
-------------------------------------------------------------------------------
x += x                                                  100        11884    1.1884 ms 
                                                       1 ns         1 ns         1 ns 
                                                       0 ns         0 ns         0 ns 
                                                                                      

-------------------------------------------------------------------------------
comparison_slow
-------------------------------------------------------------------------------
catch.cpp:16
...............................................................................

benchmark name                                  samples       iterations    estimated
                                                mean          low mean      high mean
                                                std dev       low std dev   high std dev
-------------------------------------------------------------------------------
sleep 10ms                                              100            1    1.01294 s 
                                                 10.1364 ms   10.1317 ms   10.1394 ms 
                                                  18.767 us    13.381 us    25.245 us 
                                                                                      

-------------------------------------------------------------------------------
comparison_fluctuating_v2
-------------------------------------------------------------------------------
catch.cpp:22
...............................................................................

benchmark name                                  samples       iterations    estimated
                                                mean          low mean      high mean
                                                std dev       low std dev   high std dev
-------------------------------------------------------------------------------
random fluctuations                                     100           23    2.2724 ms 
                                                   1.006 us       979 ns     1.035 us 
                                                     143 ns       123 ns       173 ns 
                                                                                      

===============================================================================
test cases: 3 | 3 passed
assertions: - none -
```

Catch is nice that it does everything automatic as well. It also shows low and high.

# Links
* [moodycamel::microbench](https://github.com/cameron314/microbench) moodycamel's microbench, probably closest to this library in spirit
* [folly Benchmark](https://github.com/facebook/folly/blob/master/folly/Benchmark.h) Part of facebook's folly
* [google Benchmark](https://github.com/google/benchmark) 
* [nonius](https://github.com/libnonius/nonius) Unmaintained?
* [celero](https://github.com/DigitalInBlue/Celero)
* [picobench](https://github.com/iboB/picobench)
