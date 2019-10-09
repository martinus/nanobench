#include <app/doctest.h>
#include <nanobench.h>

#include <atomic>

// Demonstrates a very simple benchmark that evalutes the performance of a CAS operation using std::atomic.
// On my system, this prints:
//
// | relative |               ns/op |                op/s |   MdAPE | benchmark
// |---------:|--------------------:|--------------------:|--------:|:----------------------------------------------
// |          |                6.67 |      149,913,208.14 |    0.4% | `CAS`
//
// example from https://github.com/cameron314/microbench
TEST_CASE("example_begin_here") {
    int y = 0;
    std::atomic<int> x(0);
    ankerl::nanobench::run("CAS", [&] { x.compare_exchange_strong(y, 0); });
}
