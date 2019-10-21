<a id="top"></a>
![ankerl::nanobench logo](docs/logo-nanobench.png)

[![Release](https://img.shields.io/github/release/martinus/nanobench.svg)](https://github.com/martinus/nanobench/releases)
[![GitHub license](https://img.shields.io/github/license/martinus/nanobench.svg)](https://raw.githubusercontent.com/martinus/nanobench/master/LICENSE)
[![Travis CI Build Status](https://travis-ci.com/martinus/nanobench.svg?branch=master)](https://travis-ci.com/martinus/nanobench)
[![Appveyor Build Status](https://ci.appveyor.com/api/projects/status/github/martinus/nanobench?branch=master&svg=true)](https://ci.appveyor.com/project/martinus/nanobench)
[![Join the chat at https://gitter.im/nanobench/community](https://badges.gitter.im/nanobench/community.svg)](https://gitter.im/nanobench/community?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

`ankerl::nanobench` is a platform independent microbenchmarking library for C++11/14/17/20.

```cpp
#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

int main() {
    uint64_t x = 1;
    ankerl::nanobench::Config().run("x += x", [&] {
        x += x;
    }).doNotOptimizeAway(x);
}
```

Runs for 4ms, then prints

```markdown
|               ns/op |                op/s |   MdAPE | benchmark
|--------------------:|--------------------:|--------:|:----------------------------------------------
|                0.31 |    3,195,677,932.63 |    0.0% | `x += x`
```

Which github renders as

|               ns/op |                op/s |   MdAPE | benchmark
|--------------------:|--------------------:|--------:|:----------------------------------------------
|                0.31 |    3,195,677,932.63 |    0.0% | `x += x`

# Design Goals

* **Ease of use**: Simple but powerful API, fast compile times, easy to integrate anywhere.
* **Fast**: Get accurate results as fast as possible
* **Accurate**: Get deterministic, repeatable, and accurate results that you can make sound decisions on.
* **Robust**: Be robust against outliers, warn if results are not reliable.

# How to Use it

* [Tutorial](docs/tutorial.md#top) - getting started
* [Reference](docs/reference.md#top) - all the details
* [Comparison](docs/comparison.md#top) - comparison with other microbenchmark frameworks

# More

* [Code of Conduct](CODE_OF_CONDUCT.md) - Contributor Covenant Code of Conduct
* I need a better logo. Currently I use a small bench. Nanobench. Ha ha.

