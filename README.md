# nanobench
Single header plugin-Microbenchmark library

This microbenchmarking framework is inteded to be used with other unit test frameworks like boost, google gtest, catch2, doctest, etc.

API examples:

```
// benchmarks sin(), and makes sure it is not optimized away.
double d = 1.0;
ankerl::benchmark("sin").run([&] {
    d = sin(d);
}).noopt(d);
```

```cpp

std::string text("hello, world");
size_t result = 0;

// prints something like "23.21 ns/B for string hash"
ankerl::benchmark("string hash").batch(text.size()).unit("B").run([&] {
    result += std::hash<std::string>{}(text);
}).noopt(result) << 
```

