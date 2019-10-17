#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

char const* csvTemplate = R"DELIM("relative %"; "s/{{unit}}"; "MdAPE %"; "{{title}}"
{{#benchmarks}}{{relative}}; {{median_sec_per_unit}}; {{md_ape}}; "{{name}}"
{{/benchmarks}})DELIM";

char const* htmlBoxplotTemplate = R"DELIM(<html>

<head>
    <script src="https://cdn.plot.ly/plotly-latest.min.js"></script>
</head>

<body>
    <div id="myDiv" style="width:1024px; height:768px"></div>
    <script>
        var data = [
            {{#benchmarks}}{
                name: '{{name}}',
                y: [{{#results}} {{sec_per_unit}},{{/results}} ],
            },
            {{/benchmarks}}
        ];
        var title = '{{title}}';

        data = data.map(a => Object.assign(a, { boxpoints: 'all', pointpos: 0, type: 'box' }));
        var layout = { title: { text: title }, showlegend: false, yaxis: { title: 'time per {{unit}}', rangemode: 'tozero', autorange: true } }; Plotly.newPlot('myDiv', data, layout, {responsive: true});
    </script>
</body>

</html>)DELIM";

char const* jsonTemplate = R"DELIM({
 "title": "{{title}}",
 "unit": "{{unit}}",
 "batch": {{batch}},
 "benchmarks": [
{{#benchmarks}}  {
   "name": "{{name}}",
   "median_sec_per_unit": {{median_sec_per_unit}},
   "md_ape": {{md_ape}},
   "min": {{min}},
   "max": {{max}},
   "relative": {{relative}},
   "num_measurements": {{num_measurements}},
   "results": [
{{#results}}    { "sec_per_unit": {{sec_per_unit}}, "iters": {{iters}}, "elapsed_ns": {{elapsed_ns}} },
{{/results}}   ]
  },
{{/benchmarks}} ]
}
)DELIM";

// Accepts mustache templates and fills it with nanobench data.
class Mustache {};

struct Node {
    enum class Type { tag, content, section, inverted_section };

    char const* begin;
    char const* end;
    std::vector<Node> children;
    Type type;

    template <size_t N>
    bool operator==(char const (&str)[N]) const noexcept {
        return std::distance(begin, end) == N - 1 && 0 == strncmp(str, begin, N - 1);
    }
};

std::vector<Node> parseMustacheTemplate(char const*& tpl) {
    std::vector<Node> nodes;

    while (true) {
        auto begin = std::strstr(tpl, "{{");
        auto end = begin;
        if (begin != nullptr) {
            begin += 2;
            end = std::strstr(begin, "}}");
        }

        if (begin == nullptr || end == nullptr) {
            // nothing found, finish node
            nodes.emplace_back(Node{tpl, tpl + std::strlen(tpl), std::vector<Node>{}, Node::Type::content});
            return nodes;
        } else {
            nodes.emplace_back(Node{tpl, begin - 2, std::vector<Node>{}, Node::Type::content});

            // we found a tag
            tpl = end + 2;
            switch (*begin) {
            case '/':
                // finished! bail out
                return nodes;

            case '#':
                nodes.emplace_back(Node{begin + 1, end, parseMustacheTemplate(tpl), Node::Type::section});
                break;

            case '^':
                nodes.emplace_back(Node{begin + 1, end, parseMustacheTemplate(tpl), Node::Type::inverted_section});
                break;

            default:
                nodes.emplace_back(Node{begin, end, std::vector<Node>{}, Node::Type::tag});
                break;
            }
        }
    }
}

void generateMeasurement(std::vector<Node> const& nodes, ankerl::nanobench::Measurement const& measurement, std::ostream& out) {
    for (auto const& n : nodes) {
        switch (n.type) {
        case Node::Type::content:
            out.write(n.begin, std::distance(n.begin, n.end));
            break;

        case Node::Type::section:
            throw new std::runtime_error("got a section inside measurment");

        case Node::Type::inverted_section:
            if (n == "last" && isLast) {
                // TODO
                out << n.children.front();
            }
        case Node::Type::tag:
            if (n == "sec_per_unit") {
                out << measurement.secPerUnit().count();
            } else if (n == "iters") {
                out << measurement.numIters();
            } else if (n == "elapsed_ns") {
                out << measurement.elapsed().count();
            } else {
                throw std::runtime_error("unknown tag '" + std::string(n.begin, n.end) + "'");
            }
            break;
        }
    }
}

void generateBenchmark(std::vector<Node> const& nodes, std::vector<ankerl::nanobench::Result> const& results, size_t resultIdx,
                       std::ostream& out) {
    auto const& result = results[resultIdx];
    for (auto const& n : nodes) {
        switch (n.type) {
        case Node::Type::content:
            out.write(n.begin, std::distance(n.begin, n.end));
            break;

        case Node::Type::list:
            if (n == "#results") {
                for (auto const& measurement : result.sortedMeasurements()) {
                    generateMeasurement(n.children, measurement, out);
                }
            } else {
                throw std::runtime_error("unknown list '" + std::string(n.begin, n.end) + "'");
            }
            break;

        case Node::Type::tag:
            if (n == "name") {
                out << result.name();
            } else if (n == "median_sec_per_unit") {
                out << result.median().count();
            } else if (n == "md_ape") {
                out << result.medianAbsolutePercentError();
            } else if (n == "min") {
                out << result.minimum().count();
            } else if (n == "max") {
                out << result.maximum().count();
            } else if (n == "relative") {
                out << results.front().median() / result.median();
            } else if (n == "num_measurements") {
                out << result.sortedMeasurements().size();
            } else {
                throw std::runtime_error("unknown tag '" + std::string(n.begin, n.end) + "'");
            }
        }
    }
}

void generate(char const* mustacheTemplate, ankerl::nanobench::Config const& cfg, std::ostream& out) {
    auto nodes = parseMustacheTemplate(mustacheTemplate);
    for (auto const& n : nodes) {
        switch (n.type) {
        case Node::Type::content:
            out.write(n.begin, std::distance(n.begin, n.end));
            break;

        case Node::Type::list:
            if (n == "#benchmarks") {
                for (size_t i = 0; i < cfg.results().size(); ++i) {
                    generateBenchmark(n.children, cfg.results(), i, out);
                }
            } else {
                throw std::runtime_error("unknown tag '" + std::string(n.begin, n.end) + "'");
            }
            break;

        case Node::Type::tag:
            if (n == "unit") {
                out << cfg.unit();
            } else if (n == "title") {
                out << cfg.title();
            } else if (n == "batch") {
                out << cfg.batch();
            } else {
                throw std::runtime_error("unknown tag '" + std::string(n.begin, n.end) + "'");
                break;
            }
        }
    }
}

} // namespace

TEST_CASE("mustache") {
    (void)htmlBoxplotTemplate;
    (void)csvTemplate;
    (void)jsonTemplate;

    int y = 0;
    std::atomic<int> x(0);
    ankerl::nanobench::Config cfg;
    cfg.output(nullptr).run("compare_exchange_strong", [&] { x.compare_exchange_strong(y, 0); }).run("23sdf", [&] {
        x.compare_exchange_strong(y, 0);
    });

    generate(jsonTemplate, cfg, std::cout);
}
