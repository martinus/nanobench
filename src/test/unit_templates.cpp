#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <fstream>
#include <iostream>

namespace {

std::string readFile(std::string const& filename) {
    auto fin = std::ifstream{filename};
    return std::string{std::istreambuf_iterator<char>(fin),
                       std::istreambuf_iterator<char>()};
}

// path where the template files are
std::string tplPath() {
    // not using std::filesystem becase we have to be compatible to c++11 :-/
    auto path = std::string{__FILE__};
    auto idx = path.find("unit_templates.cpp");
    return path.substr(0, idx) + "../docs/code/";
}

} // namespace

TEST_CASE("unit_templates_generate" * doctest::skip()) {
    auto fout = std::ofstream{tplPath() + "template-json.txt"};
    fout << ankerl::nanobench::templates::json();

    fout = std::ofstream{tplPath() + "template-htmlBoxplot.txt"};
    fout << ankerl::nanobench::templates::htmlBoxplot();

    fout = std::ofstream{tplPath() + "template-csv.txt"};
    fout << ankerl::nanobench::templates::csv();

    fout = std::ofstream{tplPath() + "template-pyperf.txt"};
    fout << ankerl::nanobench::templates::pyperf();
}

TEST_CASE("unit_templates") {
    REQUIRE(readFile(tplPath() + "template-json.txt") ==
            std::string{ankerl::nanobench::templates::json()});

    REQUIRE(readFile(tplPath() + "template-htmlBoxplot.txt") ==
            std::string{ankerl::nanobench::templates::htmlBoxplot()});

    REQUIRE(readFile(tplPath() + "template-csv.txt") ==
            std::string{ankerl::nanobench::templates::csv()});

    REQUIRE(readFile(tplPath() + "template-pyperf.txt") ==
            std::string{ankerl::nanobench::templates::pyperf()});
}
