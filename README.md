# nanobench
Single header plugin-Microbenchmark library

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

Compiled with `g++ -O2 -DNDEBUG full_example.cpp -I../include -o full_example` runs for 5ms and then prints this markdown table:

| relative |               ns/op |                op/s |   MdAPE | benchmark
|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
|          |                5.83 |      171,586,715.87 |    0.1% | `compare_exchange_strong`

Which means that one `x.compare_exchange_strong(y, 0);` call takes 5.83ns on my machine, or 171 million operations per second. Runtime fluctuates by around 0.1%, so the results are very stable.

## Complex Example

Easily integratable into any test framework like e.g. [doctest](https://github.com/onqtam/doctest). First put the implementation into a separate cpp file [nanobench.cpp](src/test/app/nanobench.cpp), so the benchmarks compile very fast:

```cpp
#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>
```

I have implemented a comparison of multiple random number generators in a test [example_random_number_generators.cpp](src/test/example_random_number_generators.cpp). Here several RNGs are compared to a baseline calculated from `std::default_random_engine`. 

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
|          |               44.00 |       22,728,914.36 |    1.6% | `std::default_random_engine`
|   195.2% |               22.54 |       44,374,030.10 |    4.0% | `std::mt19937`
|   549.5% |                8.01 |      124,897,086.47 |    2.1% | `std::mt19937_64`
|    93.0% |               47.31 |       21,138,957.42 |    0.6% | `std::ranlux24_base`
|   125.1% |               35.17 |       28,434,788.37 |    0.8% | `std::ranlux48_base`
|    21.5% |              204.57 |        4,888,285.22 |    1.8% | `std::ranlux24_base`
|    12.7% |              345.82 |        2,891,635.94 |    3.0% | `std::ranlux48`
|    65.8% |               66.82 |       14,965,403.16 |    1.6% | `std::knuth_b`
| 2,060.4% |                2.14 |      468,304,293.34 |    0.1% | `ankerl::nanobench::Rng`

It shows that `ankerl::nanobench::Rng` is by far the fastest RNG, and has the least amount of fluctuation. It takes only 2.14ns to generate a random `uint64_t`, so ~470 million calls per seconds are possible.



The goals are:
* fast & accurate: Benchmarks should run only as long as necessary to produce a level of accuracy thats good enough for decision making.
* Simple to use: `#include <nanobench.h>`, and use it.
* Work well with others
** frameworks like boost, gtest, catch2, doctest
** github: printing markdown tables

## ExAPI examples:


[atomic](src/test/example_atomic.cpp)

```cpp
double x = 123.0;
ankerl::NanoBench("sin(x)").run([&] {
    x = sin(x);
});
ankerl::NanoBench::doNotOptimizeAway(x);
```

Show speed of hashing, on a per-byte basis.
```cpp
std::string text("hello, world");
size_t result = 0;

// prints something like "23.21 ns/B for string hash"
ankerl::NanoBench("string hash").batch(text.size()).unit("B").run([&] {
    result += std::hash<std::string>{}(text);
});
ankerl::NanoBench::doNotOptimizeAway(result);
```

Full fledged example with random generator, and comparison to a baseline.

```cpp
// nanobench comes with a very fast random number generator. Use this in the benchmark. Initializes with random_device.
ankerl::NanoBench::Rng rng;

// run 1000 warmup iterations before doing any measurements. This fills the map so it's size is stable.
// remember results as the baseline
std::map<uint64_t, uint64_t> m;
auto baseline = ankerl::NanoBench("std::map").warmup(1000).run([&] {
    m[rng() & 0xff];
    m.erase(rng() & 0xff);
});
ankerl::NanoBench::doNotOptimizeAway(m);

std::unordered_map<uint64_t, uint64_t> uo;
ankerl::NanoBench("std::unordered_map").relative(baseline).warmup(1000).run([&] {
    uo[rng() & 0xff];
    uo.erase(rng() & 0xff);
});
ankerl::NanoBench::doNotOptimizeAway(uo);
```    

More helpers:

```cpp
// begins a new table, even when unit doesn#t change
ankerl::NanoBench::newTable(); 
```

Desired output is in markdown format:

* New table is automatically started when last setting for `unit` changes.
* `:boom:` marker added when we got an overflow (no exception thrown)
* `:hand:` marker added when MdAPE > 5%
* benchmark name backtick escaped with two backticks
* Width is used as much as possible (no begin spacing)

| relative |               ns/op |                op/s |   MdAPE | benchmark
|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
|          |                0.63 |    1,598,533,251.85 |    0.0% | `x += x`
|     6.4% |                9.80 |      102,051,025.51 |    0.1% | `std::sin(x)`
|    11.7% |                5.34 |      187,241,141.28 |   24.5% | :hand: `std::log(x)`
|    11.1% |                5.63 |      177,620,978.94 |    0.1% | `1/x`
|        - |                   - |                   - |       - | :boom: iterations overflow. Maybe your code got optimized away? `noop`
|     9.1% |                6.88 |      145,326,219.85 |    0.0% | `std::sqrt(x)`


```
| relative |               ns/op |                op/s |   MdAPE | benchmark
|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
|          |                0.63 |    1,598,533,251.85 |    0.0% | `x += x`
|     6.4% |                9.80 |      102,051,025.51 |    0.1% | `std::sin(x)`
|    11.7% |                5.34 |      187,241,141.28 |   24.5% | :hand: `std::log(x)`
|    11.1% |                5.63 |      177,620,978.94 |    0.1% | `1/x`
|        - |                   - |                   - |       - | :boom: iterations overflow. Maybe your code got optimized away? `noop`
|     9.1% |                6.88 |      145,326,219.85 |    0.0% | `std::sqrt(x)`
```


# Random Number Generator Benchmark

| relative |         ns/uint64_t |          uint64_t/s |   MdAPE | Random Number Generators
|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
|          |               42.52 |       23,518,566.93 |    4.1% | `std::default_random_engine`
|   187.8% |               22.64 |       44,167,287.68 |    2.4% | `std::mt19937`
|   587.2% |                7.24 |      138,093,729.87 |    6.3% | :wavy_dash: `std::mt19937_64`
|    93.9% |               45.28 |       22,085,691.51 |    1.5% | `std::ranlux24_base`
|   124.9% |               34.03 |       29,382,338.63 |    1.5% | `std::ranlux48_base`
|    21.4% |              198.68 |        5,033,285.71 |    5.3% | :wavy_dash: `std::ranlux24_base`
|    11.1% |              381.88 |        2,618,640.45 |    8.2% | :wavy_dash: `std::ranlux48`
|    67.4% |               63.08 |       15,853,913.77 |    4.2% | `std::knuth_b`
| 2,636.2% |                1.61 |      619,994,208.17 |    0.3% | `ankerl::nanobench::Rng`


Inspirations:
* https://github.com/ikatyang/emoji-cheat-sheet/blob/master/README.md
* folly Benchmark https://github.com/facebook/folly/blob/master/folly/Benchmark.h
* google Benchmark
* nonius
* celero
* https://vorbrodt.blog/2019/03/18/micro-benchmarks/
