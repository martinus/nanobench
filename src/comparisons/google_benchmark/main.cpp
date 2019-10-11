#include "benchmark.h"

#include <chrono>
#include <random>
#include <thread>

// Build instructions: https://github.com/google/benchmark#installation
// g++ -O2 main.cpp -Lgit/benchmark/build/src -lbenchmark -lpthread -o m
void ShiftAdd(benchmark::State& state) {
    uint64_t x = 123;
    for (auto _ : state) {
        x += x >> 1;
    }
    benchmark::DoNotOptimize(x);
}
BENCHMARK(ShiftAdd);

void Sleeper(benchmark::State& state) {
    for (auto _ : state) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
BENCHMARK(Sleeper);

void RandomFluctuations(benchmark::State& state) {
    std::random_device dev;
    std::mt19937_64 rng(dev());
    for (auto _ : state) {
        // each run, perform a random number of rng calls
        auto iterations = rng() & 0xfff;
        for (uint64_t i = 0; i < iterations; ++i) {
            rng();
        }
    }
}
BENCHMARK(RandomFluctuations);

BENCHMARK_MAIN();