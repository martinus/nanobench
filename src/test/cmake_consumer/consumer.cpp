// Consumes nanobench the way a user does: include the header, link the target,
// run a benchmark. Deliberately does *not* define ANKERL_NANOBENCH_IMPLEMENT -
// the implementation comes from the nanobench library target, and this file
// failing to link is exactly the regression worth catching.
#include <nanobench.h>

#include <cstdint>

int main() {
    uint64_t x = 1;
    ankerl::nanobench::Bench().epochs(1).epochIterations(10).run(
        "consumer", [&] {
            x = x * UINT64_C(6364136223846793005) + 1;
            ankerl::nanobench::doNotOptimizeAway(x);
        });
    return 0;
}
