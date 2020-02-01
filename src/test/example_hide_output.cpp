#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <atomic>
#include <iostream>

// Demonstrates a very simple benchmark that evalutes the performance of a CAS operation using std::atomic.
// On my system, this prints:
//
// | relative |               ns/op |                op/s |   MdAPE | benchmark
// |---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
// |          |                5.63 |      177,553,749.61 |    0.0% | `compare_exchange_strong`
//
// example from https://github.com/cameron314/microbench
TEST_CASE("example_atomic") {
    int y = 0;
    std::atomic<int> x(0);
    ankerl::nanobench::Bench bench;

    // don't write anything
    bench.output(nullptr);

    bench.run("compare_exchange_strong", [&] { x.compare_exchange_strong(y, 0); });
    std::cout << "result: " << bench.results().front().median().count() << "s/" << bench.unit() << std::endl;
}
