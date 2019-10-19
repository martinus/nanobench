# ankerl::nanobench [![Release](https://img.shields.io/github/release/martinus/nanobench.svg)](https://github.com/martinus/nanobench/releases) [![GitHub license](https://img.shields.io/github/license/martinus/nanobench.svg)](https://raw.githubusercontent.com/martinus/nanobench/master/LICENSE)

[![Travis CI Build Status](https://travis-ci.com/martinus/nanobench.svg?branch=master)](https://travis-ci.com/martinus/nanobench)
[![Appveyor Build Status](https://ci.appveyor.com/api/projects/status/github/martinus/nanobench?branch=master&svg=true)](https://ci.appveyor.com/project/martinus/nanobench)
[![Join the chat at https://gitter.im/nanobench/community](https://badges.gitter.im/nanobench/community.svg)](https://gitter.im/nanobench/community?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

`ankerl::nanobench` is a platform independent microbenchmarking library for C++11/14/17/20.

# Design Goals

* *Ease of use*: Simple but powerful API, fast compile times, easy to integrate anywhere.
* *Fast*: Get accurate results as fast as possible
* *Accurate*: Get deterministic, repeatable, and accurate results that you can make sound decisions on.
* *Robust*: Be robust against outliers, warn if results are not reliable.

# How to Use it

* [Tutorial](docs/tutorial.md#tutorial) - getting started
* [Reference](docs/reference.md#reference) - all the details
* [Comparison](docs/comparison.md#comparison) - comparison with other microbenchmark frameworks

# Features

* Single header library: you only need `nanobench.h`. Nothing else.
* Fast compile time
* Easily integratable in any unit test framework: Pure C++. no Macros, no runners, no global registrations
* Zero configuration: Automatically detects number of iterations for accurate measurements.
* Fast execution: runtime is based on the clock's accuracy
* Warns when system is not configured for benchmarking (currently only in Linux)
* Warns at unstable results - with suggestions
* Output in Markdown table format
