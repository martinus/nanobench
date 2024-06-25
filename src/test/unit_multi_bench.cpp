#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <iostream>
#include <map>
#include <sstream>
#include <unordered_map>



template <typename Container>
static void runTest(std::string container_name, ankerl::nanobench::Bench& bench_at, ankerl::nanobench::Bench& bench_operator) {
    size_t const maxSize = 1000000;

    ankerl::nanobench::Rng rng;

    Container container;
    for (size_t i{0}; i < maxSize; ++i) {
        auto value = uint8_t(rng.bounded(256));
        container[i] = value;
    }
    bench_at.run(container_name, [&] {
        ankerl::nanobench::doNotOptimizeAway(container.at(rng.bounded(maxSize)));
    });
    bench_operator.run(container_name, [&] {
        ankerl::nanobench::doNotOptimizeAway(container[rng.bounded(maxSize)]);
    });
}

// NOLINTNEXTLINE
TEST_CASE("multi_bench") {
    // Suppress any stability warnings for later outputs
    ankerl::nanobench::Bench().run("suppress_warning", []{});

    ankerl::nanobench::Bench bench_at;
    std::stringstream output_at;
    bench_at
        .title("at()")
        .output(&output_at);

    ankerl::nanobench::Bench bench_operator;
    std::stringstream output_operator;
    bench_operator
        .title("operator[]")
        .output(&output_operator);


    runTest<std::map<size_t, uint8_t> >("std::map", bench_at, bench_operator);
    runTest<std::unordered_map<size_t, uint8_t> >("std::unordered_map", bench_at, bench_operator);

    size_t linect_at{};
    size_t linect_operator{};
    {
        std::string s;
        while(std::getline(output_at, s)) {
            linect_at += 1;
        }
        while(std::getline(output_operator, s)) {
            linect_operator += 1;
        }
    }
    CHECK(linect_at == 5);
    CHECK(linect_operator == 5);

    std::cout << output_at.str() << '\n'
              << output_operator.str() << '\n';
}
