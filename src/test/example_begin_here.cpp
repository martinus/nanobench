#include <app/doctest.h>
#include <nanobench.h>

#include <atomic>

// Demonstrates a very simple benchmark that evalutes the performance of a CAS operation using std::atomic.
// On my system, this prints:
//
// | relative |               ns/op |                op/s |   MdAPE | benchmark
// |---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
// |          |                5.63 |      177,553,749.61 |    0.0% | `CAS`
//
// example from https://github.com/cameron314/microbench
TEST_CASE("example_begin_here") {
    int y = 0;
    std::atomic<int> x(0);
    ankerl::nanobench::run("CAS", [&] { x.compare_exchange_strong(y, 0); });
}
