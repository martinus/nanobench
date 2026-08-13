// This example is a single file, so it compiles the implementation itself. Exactly one translation
// unit in a program may define ANKERL_NANOBENCH_IMPLEMENT; larger projects give it a file of its
// own instead, as full_example.cpp does.
#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

int main() {
    double d = 1.0;
    ankerl::nanobench::Bench().run("some double ops", [&] {
        d += 1.0 / d;
        if (d > 5.0) {
            d -= 5.0;
        }
        ankerl::nanobench::doNotOptimizeAway(d);
    });
}