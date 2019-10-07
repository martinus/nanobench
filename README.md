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

```cpp
std::string text("hello, world");
size_t result = 0;

// prints something like "23.21 ns/B for string hash"
ankerl::nanobench("string hash").batch(text.size()).unit("B").run([&] {
    result += std::hash<std::string>{}(text);
});
ankerl::nanobench::do_not_optimize_away(result);
```

```cpp
// compare unordered_map with a baseline, use a very fast random number generator
std::map<uint64_t, uint64_t> m;
auto rng = ankerl::nanobench::rng();
auto baseline = ankerl::nanobench("std::map").run([&] {
    m[rng() & 0xfff] = 1;
    m.erase(rng() & 0xfff);
});
ankerl::nanobench::do_not_optimize_away(m);

std::unordered_map<uint64_t, uint64_t> uo;
ankerl::nanobench("std::unordered_map").relative(baseline).run([&] {
    uo[rng() & 0xfff] = 1;
    uo.erase(rng() & 0xfff);
});
ankerl::nanobench::do_not_optimize_away(uo);
```    


Inspirations:
* folly Benchmark https://github.com/facebook/folly/blob/master/folly/Benchmark.h
* google Benchmark
* nonius
* celero
* https://vorbrodt.blog/2019/03/18/micro-benchmarks/
