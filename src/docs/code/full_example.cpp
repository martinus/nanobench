// No ANKERL_NANOBENCH_IMPLEMENT here on purpose: this example is linked against a separately
// compiled nanobench.cpp, which is where the implementation lives. Defining it here as well would
// give duplicate symbols at link time. See full_example_simple.cpp for the single-file variant.
#include <nanobench.h>

#include <atomic>

int main() {
    int y = 0;
    std::atomic<int> x(0);
    ankerl::nanobench::Bench().run("compare_exchange_strong", [&] {
        x.compare_exchange_strong(y, 0);
    });
}
