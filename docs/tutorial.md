<a id="top"></a>
# Tutorial

<!--ts-->
   * [Tutorial](#tutorial)
   * [Installation](#installation)
   * [Examples](#examples)
      * [Simple Example](#simple-example)
      * [Something Fast](#something-fast)
      * [Something Slow](#something-slow)
      * [Something Unstable](#something-unstable)
      * [Comparing Results](#comparing-results)
<!--te-->

# Installation

1. Download `nanobench.h` from the [releases](https://github.com/martinus/nanobench/releases) and make it available in your project.
1. Create a .cpp file, e.g. `nanobench.cpp`, where the bulk of nanobench is compiled:
   ```cpp
   #define ANKERL_NANOBENCH_IMPLEMENT
   #include <nanobench.h>
   ```
1. Wherever you want to use nanobench's functionality, simply `#include <nanobench.h>`. All functionality is within namespace `ankerl::nanobench`.

# Examples

You can find more examples in [src/test](https://github.com/martinus/nanobench/tree/master/src/test).

## Simple Example

This code from [full_example.cpp](https://github.com/martinus/nanobench/tree/master/src/scripts/full_example.cpp):

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

|               ns/op |                op/s |   MdAPE |         ins/op |         cyc/op |    IPC |    branches/op | missed% | benchmark
|--------------------:|--------------------:|--------:|---------------:|---------------:|-------:|---------------:|--------:|:----------------------------------------------
|                7.81 |      128,092,931.19 |    0.0% |           4.00 |          24.93 |  0.161 |           0.00 |    0.0% | `compare_exchange_strong`

Which means that one `x.compare_exchange_strong(y, 0);` call takes 7.81s on my machine, or ~128 million
operations per second. Runtime fluctuates by around 0.0%, so the results are very stable. Each call required 4 instructions, which took ~25 CPU cycles.
There were no branches in this code, so we also got no branch misspredictions.

In the remaining examples, I compile nanobench's implementation once in a separate cpp file 
[nanobench.cpp](https://github.com/martinus/nanobench/tree/master/src/test/app/nanobench.cpp). This compiles most of nanobench, and is relatively slow - but
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

|               ns/op |                op/s |   MdAPE |         ins/op |         cyc/op |    IPC |    branches/op | missed% | benchmark
|--------------------:|--------------------:|--------:|---------------:|---------------:|-------:|---------------:|--------:|:----------------------------------------------
|                   - |                   - |       - |              - |              - |      - |              - |       - | :boom: iterations overflow. Maybe your code got optimized away? `x += x`

The compiler could optimize `x += x` away because we never used the output. Let's fix this:

```cpp
TEST_CASE("comparison_fast_v2") {
    uint64_t x = 1;
    ankerl::nanobench::Config().run("x += x", [&] { x += x; }).doNotOptimizeAway(x);
}
```

This time the benchmark runs for 2.2ms and gives us a good result:

|               ns/op |                op/s |   MdAPE |         ins/op |         cyc/op |    IPC |    branches/op | missed% | benchmark
|--------------------:|--------------------:|--------:|---------------:|---------------:|-------:|---------------:|--------:|:----------------------------------------------
|                0.32 |    3,170,869,554.81 |    0.2% |           1.00 |           1.01 |  0.993 |           0.00 |    0.0% | `x += x`

It's a very stable result. One run the op/s is 3,170 million/sec, the next time I execute it I get 3,168 million/sec. It always takes 
1.00 instructions per operation on my machine, and can do this in ~1 cycle.

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

|               ns/op |                op/s |   MdAPE |         ins/op |         cyc/op |    IPC |    branches/op | missed% | framework comparison
|--------------------:|--------------------:|--------:|---------------:|---------------:|-------:|---------------:|--------:|:----------------------------------------------
|       10,145,437.00 |               98.57 |    0.0% |          28.00 |       2,394.00 |  0.012 |           8.00 |   87.5% | `sleep 10ms`

So we actually take 10.145ms instead of 10ms. Next time I run it, I get 10.141. Also a very stable result. Interestingly, sleep takes 28 instructions but 2394 cycles - so we only got 0.012 instructions per cycle. That's extremely low, but expected of `sleep`. It also required 8 branches, of which 87.5% were misspredicted on average.

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

|               ns/op |                op/s |   MdAPE |         ins/op |         cyc/op |    IPC |    branches/op | missed% | benchmark
|--------------------:|--------------------:|--------:|---------------:|---------------:|-------:|---------------:|--------:|:----------------------------------------------
|            1,026.41 |          974,269.30 |    7.0% |       6,018.97 |       3,277.26 |  1.837 |         792.72 |    8.6% | :wavy_dash: `random fluctuations` Unstable with ~38.7 iters. Increase `minEpochIterations` to e.g. 387

So on average each loop takes about 1,026.41ns, but we get a warning that the results are unstable. The median percentage error is ~7% which is quite high. Executed again, I get 987.86 ns.

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

|               ns/op |                op/s |   MdAPE |         ins/op |         cyc/op |    IPC |    branches/op | missed% | benchmark
|--------------------:|--------------------:|--------:|---------------:|---------------:|-------:|---------------:|--------:|:----------------------------------------------
|              988.96 |        1,011,165.38 |    0.9% |       5,861.14 |       3,147.65 |  1.862 |         772.10 |    8.6% | `random fluctuations`

The results are also more stable, with only 0.7% MdAPE. This time the benchmark takes 27ms.

## Comparing Results

I have implemented a comparison of multiple random
number generators in a test [example_random_number_generators.cpp](https://github.com/martinus/nanobench/tree/master/src/test/example_random_number_generators.cpp).
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

| relative |         ns/uint64_t |          uint64_t/s |   MdAPE |   ins/uint64_t |   cyc/uint64_t |    IPC |branches/uint64_t | missed% | Random Number Generators
|---------:|--------------------:|--------------------:|--------:|---------------:|---------------:|-------:|---------------:|--------:|:----------------------------------------------
|   100.0% |               42.24 |       23,671,446.65 |    1.5% |         184.72 |         134.90 |  1.369 |          15.50 |    2.8% | `std::default_random_engine`
|   195.8% |               21.57 |       46,351,638.16 |    1.2% |         174.93 |          68.88 |  2.540 |          23.99 |    4.3% | `std::mt19937`
|   550.5% |                7.67 |      130,317,142.34 |    1.3% |          43.48 |          24.50 |  1.774 |           4.99 |   10.2% | `std::mt19937_64`
|    92.1% |               45.86 |       21,803,766.11 |    0.6% |         211.58 |         146.49 |  1.444 |          26.51 |    5.6% | `std::ranlux24_base`
|   124.5% |               33.92 |       29,478,806.51 |    0.4% |         144.01 |         108.33 |  1.329 |          17.00 |    4.9% | `std::ranlux48_base`
|    21.2% |              199.49 |        5,012,780.11 |    0.9% |         716.43 |         637.00 |  1.125 |          95.08 |   15.8% | `std::ranlux24_base`
|    10.9% |              386.79 |        2,585,356.75 |    2.2% |       1,429.99 |       1,234.62 |  1.158 |         191.51 |   15.6% | `std::ranlux48`
|    65.2% |               64.76 |       15,442,579.88 |    1.3% |         356.97 |         206.55 |  1.728 |          33.05 |    0.8% | `std::knuth_b`
| 2,069.1% |                2.04 |      489,778,900.82 |    0.1% |          18.00 |           6.52 |  2.760 |           0.00 |    0.0% | `ankerl::nanobench::Rng`

It shows that `ankerl::nanobench::Rng` is by far the fastest RNG, and has the least amount of
fluctuation. It takes only 2.04ns to generate a random `uint64_t`, so ~489 million calls per
seconds are possible. Interestingly, it requires *zero* branches, so no chance for misspredictions.
