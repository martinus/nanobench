#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>
#include <cmath>

int main() {
    double d = 1.0;
    ankerl::nanobench::Config().run("d += std::sin(d)", [&] {
        d += std::sin(d);
    }).doNotOptimizeAway(d);
}
