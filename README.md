# nanobench
Single header plugin-Microbenchmark library

This microbenchmarking framework is inteded to be used with other unit test frameworks like boost, google gtest, catch2, doctest, etc.

The goals are:
* fast & accurate: Benchmarks should run only as long as necessary to produce a level of accuracy thats good enough for decision making.
* Simple to use: `#include <nanobench.h>`, and use it.
* Work well with others
** frameworks like boost, gtest, catch2, doctest
** github: printing markdown tables

API examples:


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

| relative |         ns/uint64_t |          uint64_t/s |   MdAPE | benchmark
|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
|          |               42.28 |       23,653,053.43 |    4.2% | :wavy_dash: `std::default_random_engine`
|   186.2% |               22.71 |       44,039,280.14 |    3.0% | :wavy_dash: `std::mt19937`
|   577.4% |                7.32 |      136,578,690.38 |    7.0% | :question: `std::mt19937_64`
|    93.3% |               45.34 |       22,056,497.46 |    1.7% | `std::ranlux24_base`
|   122.9% |               34.41 |       29,064,582.45 |    1.6% | `std::ranlux48_base`
|    21.1% |              200.49 |        4,987,797.26 |    5.9% | :question: `std::ranlux24_base`
|    11.0% |              385.19 |        2,596,107.64 |   10.3% | :bangbang: `std::ranlux48`
|    66.8% |               63.31 |       15,795,708.84 |    4.6% | :wavy_dash: `std::knuth_b`
| 2,620.8% |                1.61 |      619,906,882.38 |    0.4% | `ankerl::nanobench::Rng`


Inspirations:
* https://github.com/ikatyang/emoji-cheat-sheet/blob/master/README.md
* folly Benchmark https://github.com/facebook/folly/blob/master/folly/Benchmark.h
* google Benchmark
* nonius
* celero
* https://vorbrodt.blog/2019/03/18/micro-benchmarks/
