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
