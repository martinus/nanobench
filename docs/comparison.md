# Comparison

<!--ts-->
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

<!-- Added by: martinus, at: Sa Okt 19 12:28:06 CEST 2019 -->

<!--te-->

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
