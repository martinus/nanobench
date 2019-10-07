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
|          |                0.63 |    1,598,631,578.95 |    0.0% | `x += x`
|     6.4% |                9.80 |      102,053,712.48 |    0.1% | `std::sin(x)`
|    11.4% |                5.49 |      182,234,083.11 |   10.7% | :hand: `std::log(x)`
|    11.1% |                5.63 |      177,625,690.97 |    0.2% | `1/x`
|     9.1% |                6.88 |      145,331,087.48 |    0.0% | `std::sqrt(x)`


```
| relative |               ns/op |                op/s |   MdAPE | benchmark
|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
|          |                0.63 |    1,598,631,578.95 |    0.0% | `x += x`
|     6.4% |                9.80 |      102,053,712.48 |    0.1% | `std::sin(x)`
|    11.4% |                5.49 |      182,234,083.11 |   10.7% | :hand: `std::log(x)`
|    11.1% |                5.63 |      177,625,690.97 |    0.2% | `1/x`
|     9.1% |                6.88 |      145,331,087.48 |    0.0% | `std::sqrt(x)`
```

Inspirations:
* https://github.com/ikatyang/emoji-cheat-sheet/blob/master/README.md
* folly Benchmark https://github.com/facebook/folly/blob/master/folly/Benchmark.h
* google Benchmark
* nonius
* celero
* https://vorbrodt.blog/2019/03/18/micro-benchmarks/
