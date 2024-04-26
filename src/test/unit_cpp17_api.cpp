#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#if __cplusplus < 201606L // __cpp_lib_string_view

TEST_CASE("Placeholder case for C++17") {
    INFO("C++17 tests skiped");
}

#else

#    include <string_view>

#    ifdef _MSC_VER
// Compiler known issue
// warning C4455: 'operator ""sv': literal suffix identifiers that do not start
// with an underscore are reserved
#        pragma warning(push)
#        pragma warning(disable : 4455)
#    endif
using std::string_view_literals::operator""sv;
#    ifdef _MSC_VER
#        pragma warning(pop)
#    endif

TEST_CASE("Set and change name from string_view") {
    ankerl::nanobench::Bench bench;
    bench.name("string view 1"sv).run([] {
        std::vector<int> const v{{11, 5, 5, 5, 5, 5, 5, 5}};
    });

    bench.name("string view 2"sv);
    bench.run([] {
        std::vector<int> const v{{11, 5, 5, 5, 5, 5, 5, 5}};
    });
}

TEST_CASE("Set name from string_view") {
    ankerl::nanobench::Bench bench;
    bench.run("string view 3"sv, [] {
        std::vector<int> const v{{11, 5, 5, 5, 5, 5, 5, 5}};
    });
}

#endif // __cplusplus < 201606L
