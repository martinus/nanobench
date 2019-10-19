#include "microbench.h"

#include <chrono>
#include <iostream>
#include <random>
#include <thread>

// g++ -O2 -c systemtime.cpp
// g++ -O2 -c microbench.cpp
// g++ microbench.o systemtime.o -o mb
int main(int, char**) {
    // something fast
    uint64_t x = 1;
    std::cout << moodycamel::microbench([&]() { x += x; }, 10000000, 51) << " sec x += x (x==" << x << ")" << std::endl;

    std::cout << moodycamel::microbench([&] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }) << " sec sleep 10ms"
              << std::endl;

    std::random_device dev;
    std::mt19937_64 rng(dev());
    std::cout << moodycamel::microbench(
                     [&] {
                         // each run, perform a random number of rng calls
                         auto iterations = rng() & UINT64_C(0xff);
                         for (uint64_t i = 0; i < iterations; ++i) {
                             (void)rng();
                         }
                     },
                     1000, 51)
              << " sec random fluctuations" << std::endl;
}
