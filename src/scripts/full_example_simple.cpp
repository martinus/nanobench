#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

int main() {
    uint64_t x = 1;
    ankerl::nanobench::Config().run("x += x", [&] { x += x; }).doNotOptimizeAway(x);
}
