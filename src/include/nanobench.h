//  __   _ _______ __   _  _____  ______  _______ __   _ _______ _     _
//  | \  | |_____| | \  | |     | |_____] |______ | \  | |       |_____|
//  |  \_| |     | |  \_| |_____| |_____] |______ |  \_| |_____  |     |
//
// Microbenchmark framework for C++11/14/17/20
// https://github.com/martinus/nanobench
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2019 Martin Ankerl <http://martin.ankerl.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef ANKERL_NANOBENCH_H_INCLUDED
#define ANKERL_NANOBENCH_H_INCLUDED

// see https://semver.org/
#define ANKERL_NANOBENCH_VERSION_MAJOR 0 // incompatible API changes
#define ANKERL_NANOBENCH_VERSION_MINOR 0 // backwards-compatible changes
#define ANKERL_NANOBENCH_VERSION_PATCH 1 // backwards-compatible bug fixes

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ankerl {
namespace nanobench {
namespace detail {

inline std::string& lastUnit() {
    static std::string sUnit = {};
    return sUnit;
}

#if defined(_MSC_VER)
// see https://docs.microsoft.com/en-us/cpp/preprocessor/optimize
#    pragma optimize("", off)
inline void doNotOptimizeAway_sink(void const*) {}
#    pragma optimize("", on

#else

template <typename T>
struct DoNotOptimizeAwayNeedsIndirect {
    using D = typename std::decay<T>::type;
    constexpr static bool value = !std::is_trivially_copyable<D>::value || sizeof(D) > sizeof(long) || std::is_pointer<D>::value;
};

template <typename T>
auto doNotOptimizeAway_sink(T const& val) -> typename std::enable_if<!DoNotOptimizeAwayNeedsIndirect<T>::value>::type {
    // see https://github.com/facebook/folly/blob/master/folly/Benchmark.h
    // Tells the compiler that we read val from memory and might read/write
    // from any memory location.
    asm volatile("" ::"m"(val) : "memory");
}

template <typename T>
auto doNotOptimizeAway_sink(T const& val) -> typename std::enable_if<DoNotOptimizeAwayNeedsIndirect<T>::value>::type {
    // the "r" forces compiler to make val available in a register, so it must have been loaded.
    // Only works when small enough (<= sizeof(long)), trivial, and no pointer
    asm volatile("" ::"r"(val));
}

#endif

// determines best resolution of the given clock
template <typename Clock>
typename Clock::duration calcClockResolution(size_t numEvaluations) {
    auto bestDuration = Clock::duration::max();
    for (size_t i = 0; i < numEvaluations; ++i) {
        auto begin = Clock::now();
        auto end = Clock::now();
        while (begin == end) {
            end = Clock::now();
        }
        bestDuration = (std::min)(bestDuration, end - begin);
    }
    return bestDuration;
}

template <typename Clock>
typename Clock::duration clockResolution() {
    static typename Clock::duration dur = calcClockResolution<Clock>(20);
    return dur;
}

namespace fmt {

class num_sep : public std::numpunct<char> {
public:
    num_sep(char sep)
        : m_sep(sep) {}

    char do_thousands_sep() const {
        return m_sep;
    }

    std::string do_grouping() const {
        return "\003";
    }

private:
    char m_sep;
};

class streamstate_restorer {
public:
    explicit streamstate_restorer(std::ostream& s)
        : m_stream(s)
        , m_locale(s.getloc())
        , m_precision(s.precision())
        , m_width(s.width())
        , m_fill(s.fill())
        , m_fmt_flags(s.flags()) {}

    ~streamstate_restorer() {
        restore();
    }

    void restore() {
        m_stream.imbue(m_locale);
        m_stream.fill(m_fill);
        m_stream.width(m_width);
        m_stream.precision(m_precision);
        m_stream.flags(m_fmt_flags);
    }

    streamstate_restorer(streamstate_restorer const&) = delete;

private:
    std::ostream& m_stream;
    std::locale m_locale;
    std::streamsize const m_precision;
    std::streamsize const m_width;
    std::ostream::char_type const m_fill;
    std::ostream::fmtflags const m_fmt_flags;
};

struct num {
    num(int width, int precision, double value)
        : mWidth(width)
        , mPrecision(precision)
        , mValue(value) {}

    int mWidth;
    int mPrecision;
    double mValue;
};

inline std::ostream& operator<<(std::ostream& os, num const& n) {
    streamstate_restorer sr(os);
    os.imbue(std::locale(std::cout.getloc(), new num_sep(',')));
    os << std::setw(n.mWidth) << std::setprecision(n.mPrecision) << std::fixed << n.mValue;
    return os;
}

struct MarkDownCode {
    MarkDownCode(std::string what)
        : mWhat(std::move(what)) {}

    std::string mWhat;
};

inline std::ostream& operator<<(std::ostream& os, MarkDownCode const& mdCode) {
    os.put('`');
    for (char c : mdCode.mWhat) {
        os.put(c);
        if ('`' == c) {
            os.put('`');
        }
    }
    os.put('`');
    return os;
}

} // namespace fmt
} // namespace detail

template <typename... Args>
static void doNotOptimizeAway(Args&&... args) {
    (void)std::initializer_list<int>{(detail::doNotOptimizeAway_sink(args), 0)...};
}

struct OverflowError : public std::runtime_error {
    inline explicit OverflowError(std::string const& msg)
        : std::runtime_error(msg) {}
    inline explicit OverflowError(const char* msg)
        : std::runtime_error(msg) {}

    OverflowError(const OverflowError&) = default;
    OverflowError& operator=(const OverflowError&) = default;
    OverflowError(OverflowError&&) = default;
    OverflowError& operator=(OverflowError&&) = default;
    virtual ~OverflowError() = default;
};

class Result {
public:
    Result(double ops) noexcept
        : mOps(ops) {}

    Result() = default;

    double ops() const {
        return mOps;
    }

    // convenience wrapper
    template <typename... Args>
    Result& doNotOptimizeAway(Args&&... args) {
        ::ankerl::nanobench::doNotOptimizeAway(std::forward<Args>(args)...);
        return *this;
    }

private:
    double mOps = {-1};
};

class Cfg {
    using Clock = std::chrono::high_resolution_clock;

    std::string m_name{"unspecified name"};
    double m_batch{1.0};
    std::string m_unit{"op"};
    size_t m_epochs{21};
    uint64_t m_clock_resolution_multiple{1000};
    std::chrono::nanoseconds m_max_epoch_time{std::chrono::milliseconds{100}};
    Result m_relative{};

public:
    Cfg& name(std::string n) noexcept {
        m_name = std::move(n);
        return *this;
    }

    template <typename T>
    typename std::enable_if<std::is_arithmetic<T>::value, Cfg&>::type batch(T b) noexcept {
        m_batch = static_cast<double>(b);
        return *this;
    }
    Cfg& batch(size_t b) noexcept {
        m_batch = static_cast<double>(b);
        return *this;
    }

    Cfg& relative(Result const& rel) noexcept {
        m_relative = rel;
        return *this;
    }

    Cfg& unit(std::string unit) {
        m_unit = std::move(unit);
        return *this;
    }

    Cfg& epochs(size_t num_epochs) noexcept {
        m_epochs = num_epochs;
        return *this;
    }

    // how much should we be above the clock resolution?
    Cfg& clock_resolution_multiple(size_t multiple) noexcept {
        m_clock_resolution_multiple = multiple;
        return *this;
    }

    Cfg& max_epoch_time(std::chrono::nanoseconds t) noexcept {
        m_max_epoch_time = t;
        return *this;
    }

    template <typename Op>
    Result run(Op op) const {
        auto target_runtime = detail::clockResolution<Clock>() * m_clock_resolution_multiple;

        if (target_runtime > m_max_epoch_time) {
            target_runtime = m_max_epoch_time;
        }

        std::vector<double> sec_per_iter;
        sec_per_iter.reserve(m_epochs);

        size_t num_iters = 1;

        while (sec_per_iter.size() != m_epochs) {
            auto n = num_iters;

            // the code between before and after is very time critical. Make sure we only call op on
            // one occation, so that it is easy to inline.
            auto before = Clock::now();
            while (n > 0) {
                op();
                --n;
            }
            auto after = Clock::now();

            auto elapsed = after - before;
            // adapt n
            if (elapsed * 10 < target_runtime) {
                if (num_iters * 10 < num_iters) {
                    return show_results(sec_per_iter, "iterations overflow. Maybe your code got optimized away?");
                }
                num_iters *= 10;
            } else {
                auto mult = num_iters * static_cast<size_t>(target_runtime.count());
                if (mult < num_iters) {
                    return show_results(sec_per_iter, "iterations overflow. Maybe your code got optimized away?");
                }
                num_iters = (num_iters * target_runtime) / elapsed;
                if (num_iters == 0) {
                    num_iters = 1;
                }
            }

            // if we are within 2/3 of the target runtime, add it.
            if (elapsed * 3 >= target_runtime * 2) {
                auto result =
                    std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count() / static_cast<double>(num_iters);

                sec_per_iter.push_back(result);
            }
        }
        return show_results(sec_per_iter, "");
    }

private:
    double calc_median(std::vector<double> const& results) const {
        auto mid = results.size() / 2;
        if (results.size() & 1) {
            return results[mid];
        }
        return (results[mid - 1] + results[mid]) / 2;
    }

    double calc_median_absolute_percentage_error(std::vector<double> const& results, double median) const {
        std::vector<double> absolute_percentage_errors;
        for (auto r : results) {
            absolute_percentage_errors.push_back(std::fabs(r - median) / r);
        }
        std::sort(absolute_percentage_errors.begin(), absolute_percentage_errors.end());

        return calc_median(absolute_percentage_errors);
    }

    Result show_results(std::vector<double>& sec_per_iter, std::string errorMessage) const {
        if (!errorMessage.empty()) {
            std::cout << "|        - |                   - |                   - |       - | :boom: " << errorMessage << ' '
                      << detail::fmt::MarkDownCode(m_name) << std::endl;
            return Result(-1);
        }
        std::vector<double> iter_per_sec;
        for (auto t : sec_per_iter) {
            iter_per_sec.push_back(1.0 / t);
        }

        std::sort(iter_per_sec.begin(), iter_per_sec.end());
        auto const med_iter_per_sec = calc_median(iter_per_sec);

        std::sort(sec_per_iter.begin(), sec_per_iter.end());
        auto const med_sec_per_iter = calc_median(sec_per_iter);
        auto const mdaps_sec_per_iter = calc_median_absolute_percentage_error(sec_per_iter, med_sec_per_iter);

        detail::fmt::streamstate_restorer restoreStream(std::cout);

        // show final result, the median
        auto const med_ns_per_op = 1e9 * med_sec_per_iter / m_batch;
        auto const med_ops = med_iter_per_sec * m_batch;

        if (detail::lastUnit() != m_unit) {
            detail::lastUnit() = m_unit;
            std::cout
                << std::endl
                << "| relative |" << std::setw(20) << std::right << ("ns/" + m_unit) << " |" << std::setw(20) << std::right
                << (m_unit + "/s") << " |   MdAPE | benchmark" << std::endl
                << "|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------"
                << std::endl;
        }

        // |  1208.4% |               14.15 |       70,649,422.38 |    0.3% | `std::vector<std::string> emplace + release`
        std::cout << '|';
        if (m_relative.ops() <= 0) {
            std::cout << "          |";
        } else {
            std::cout << detail::fmt::num(8, 1, med_ops / m_relative.ops() * 100) << "% |";
        }
        std::cout << detail::fmt::num(20, 2, med_ns_per_op) << " |" << detail::fmt::num(20, 2, med_ops) << " |"
                  << detail::fmt::num(7, 1, mdaps_sec_per_iter * 100) << "% | ";

        if (mdaps_sec_per_iter >= 0.05) {
            // >=5%
            std::cout << ":hand: ";
        }
        std::cout << detail::fmt::MarkDownCode(m_name) << std::endl;

        return med_ops;
    }
};

inline Cfg name(std::string n) noexcept {
    Cfg cfg;
    cfg.name(std::move(n));
    return cfg;
}

inline void tableHeader() {
    // reset lastUnit, so it's printed next time
    detail::lastUnit() = "";
}

} // namespace nanobench
} // namespace ankerl

#endif
