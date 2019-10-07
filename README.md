# nanobench
Single header plugin-Microbenchmark library

This microbenchmarking framework is inteded to be used with other unit test frameworks like boost, google gtest, catch2, doctest, etc.

API examples:


```cpp
double x = 123.0;
ankerl::nanobench("sin(x)").run([&] {
    x = sin(x);
});
ankerl::nanobench::do_not_optimize_away(x);
```

Show speed of hashing, on a per-byte basis.
```cpp
std::string text("hello, world");
size_t result = 0;

// prints something like "23.21 ns/B for string hash"
ankerl::nanobench("string hash").batch(text.size()).unit("B").run([&] {
    result += std::hash<std::string>{}(text);
});
ankerl::nanobench::do_not_optimize_away(result);
```

Full fledged example with random generator, and comparison to a baseline.

```cpp
// nanobench comes with a very fast random number generator. Use this in the benchmark. Initializes with random_device.
ankerl::nanobench::rng rng;

// run 1000 warmup iterations before doing any measurements. This fills the map so it's size is stable.
// remember results as the baseline
std::map<uint64_t, uint64_t> m;
auto baseline = ankerl::nanobench("std::map").warmup(1000).run([&] {
    m[rng() & 0xff];
    m.erase(rng() & 0xff);
});
ankerl::nanobench::do_not_optimize_away(m);

std::unordered_map<uint64_t, uint64_t> uo;
ankerl::nanobench("std::unordered_map").relative(baseline).warmup(1000).run([&] {
    uo[rng() & 0xff];
    uo.erase(rng() & 0xff);
});
ankerl::nanobench::do_not_optimize_away(uo);
```    

More helpers:

```cpp
ankerl::nanobench::print_line(); // Draw a line for separation with -
ankerl::nanobench::print_line('='); // Draw a line with =
ankerl::nanobench::print_header(); 
```

Desired output is in markdown format:

|         | relative |               ns/op |               op/s  |  MdAPE  | benchmark
|---------|---------:|--------------------:|--------------------:|--------:|:-----------------------------------------------
|:snail:  |          |                6.26 |      159,711,728.47 |    0.1% | `std::vector<std::string> reserve(ptr) + release`
|         |   86.34% |                6.83 |      146,327,670.30 |    0.1% | `std::vector<std::string> reserve() + lookup + operator=`
|:rocket: | 1208.24% |               14.15 |       70,649,422.38 |    0.3% | `std::vector<std::string> emplace + release`
|:bug:    |        - |                   - |                   - |       - | overflow `std::vector<std::string> emplace + release`
|         | 1208.24% |               14.29 |       69,984,072.59 |    0.5% | `std::vector<std::string> moving out`
|         | 1208.24% |               17.26 |       57,935,477.67 |    0.6% | `std::vector<std::string> = std::string()`
|:warning:| 1208.24% |               15.56 |       64,252,195.88 |   17.7% | `std::vector<std::string> dtor & ctor`
|         | 1208.24% |               15.60 |       64,113,063.62 |    0.5% | `std::vector<std::string> std::string().swap()`


```
|         | relative |               ns/op |               op/s  |  MdAPE  | benchmark
|---------|---------:|--------------------:|--------------------:|--------:|:-----------------------------------------------
|:snail:  |          |                6.26 |      159,711,728.47 |    0.1% | `std::vector<std::string> reserve(ptr) + release`
|         |   86.34% |                6.83 |      146,327,670.30 |    0.1% | `std::vector<std::string> reserve() + lookup + operator=`
|:rocket: | 1208.24% |               14.15 |       70,649,422.38 |    0.3% | `std::vector<std::string> emplace + release`
|:bug:    |        - |                   - |                   - |       - | overflow `std::vector<std::string> emplace + release`
|         | 1208.24% |               14.29 |       69,984,072.59 |    0.5% | `std::vector<std::string> moving out`
|         | 1208.24% |               17.26 |       57,935,477.67 |    0.6% | `std::vector<std::string> = std::string()`
|:warning:| 1208.24% |               15.56 |       64,252,195.88 |   17.7% | `std::vector<std::string> dtor & ctor`
|         | 1208.24% |               15.60 |       64,113,063.62 |    0.5% | `std::vector<std::string> std::string().swap()`
```

Inspirations:
* folly Benchmark https://github.com/facebook/folly/blob/master/folly/Benchmark.h
* google Benchmark
* nonius
* celero
* https://vorbrodt.blog/2019/03/18/micro-benchmarks/
