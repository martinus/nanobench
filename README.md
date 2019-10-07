# nanobench
Single header plugin-Microbenchmark library

This microbenchmarking framework is inteded to be used with other unit test frameworks like boost, google gtest, catch2, doctest, etc.

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
|          |                6.26 |      159,711,728.47 |    0.1% | `std::vector<std::string> reserve(ptr) + release`
|    86.4% |                6.83 |      146,327,670.30 |    0.1% | `std::vector<std::string> reserve() + lookup + operator=`
|  1208.4% |               14.15 |       70,649,422.38 |    0.3% | `std::vector<std::string> emplace + release`
|        - |                   - |                   - |       - | :boom: overflow `std::vector<std::string> emplace + release`
|  1208.4% |               14.29 |       69,984,072.59 |    0.5% | `std::vector<std::string> moving out`
|  1208.4% |               17.26 |       57,935,477.67 |    0.6% | `std::vector<std::string> = std::string()`
|  1208.4% |               15.56 |       64,252,195.88 |   17.7% | :hand: `std::vector<std::string> dtor & ctor`
|  1208.4% |               15.60 |       64,113,063.62 |    0.5% | `std::vector<std::string> std::string().swap()`


```
| relative |               ns/op |               op/s  |  MdAPE  | benchmark
|---------:|--------------------:|--------------------:|--------:|-----------------------------------------------
|          |                6.26 |      159,711,728.47 |    0.1% | `std::vector<std::string> reserve(ptr) + release`
|    86.4% |                6.83 |      146,327,670.30 |    0.1% | `std::vector<std::string> reserve() + lookup + operator=`
|  1208.4% |               14.15 |       70,649,422.38 |    0.3% | `std::vector<std::string> emplace + release`
|        - |                   - |                   - |       - | :boom: overflow `std::vector<std::string> emplace + release`
|  1208.4% |               14.29 |       69,984,072.59 |    0.5% | `std::vector<std::string> moving out`
|  1208.4% |               17.26 |       57,935,477.67 |    0.6% | `std::vector<std::string> = std::string()`
|  1208.4% |               15.56 |       64,252,195.88 |   17.7% | :hand: `std::vector<std::string> dtor & ctor`
|  1208.4% |               15.60 |       64,113,063.62 |    0.5% | `std::vector<std::string> std::string().swap()`
```

Inspirations:
* https://github.com/ikatyang/emoji-cheat-sheet/blob/master/README.md
* folly Benchmark https://github.com/facebook/folly/blob/master/folly/Benchmark.h
* google Benchmark
* nonius
* celero
* https://vorbrodt.blog/2019/03/18/micro-benchmarks/
