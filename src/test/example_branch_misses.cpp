#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <fstream>

// This example should show about one branch per op, and about 50% branch misses since it is completely unpredictable.
TEST_CASE("example_branch_misses") {
    ankerl::nanobench::Rng rng;

    ankerl::nanobench::Config cfg;
    cfg.title("evaluating branch misses");

    // on average, rng() is called 1.5 times per loop. We ignore the & 1U check.
    cfg.batch(1.5)
        .run("50% forced misspredictions",
             [&] {
                 if (rng() & 1U) {
                     rng();
                 }
             })
        .doNotOptimizeAway(rng);

    cfg.batch(1).run("no forced misspredictions", [&] { rng(); }).doNotOptimizeAway(rng);

    std::ofstream fout("example_branch_misses.json");
    cfg.render(ankerl::nanobench::templates::json(), fout);
}
