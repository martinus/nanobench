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

namespace detail {

#if defined(_MSC_VER)
// see https://docs.microsoft.com/en-us/cpp/preprocessor/optimize
#    pragma optimize("", off)
inline void do_not_optimize_away_sink(void const*) {}
#    pragma optimize("", on

#else

template <typename T>
struct DoNotOptimizeAwayNeedsIndirect {
    using D = typename std::decay<T>::type;
    constexpr static bool value = !std::is_trivially_copyable<D>::value ||
                                  sizeof(D) > sizeof(long) || std::is_pointer<D>::value;
};

template <typename T>
auto do_not_optimize_away_sink(T const& val) ->
    typename std::enable_if<!DoNotOptimizeAwayNeedsIndirect<T>::value>::type {
    // see https://github.com/facebook/folly/blob/master/folly/Benchmark.h
    // Tells the compiler that we read val from memory and might read/write
    // from any memory location.
    asm volatile("" ::"m"(val) : "memory");
}

template <typename T>
auto do_not_optimize_away_sink(T const& val) ->
    typename std::enable_if<DoNotOptimizeAwayNeedsIndirect<T>::value>::type {
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

static void throwOverflow();

} // namespace detail

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

#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wpadded"
#endif

class streamstate_restorer {
public:
    explicit streamstate_restorer(std::ostream& s)
        : m_stream(s)
        , m_precision(s.precision())
        , m_width(s.width())
        , m_fill(s.fill())
        , m_fmt_flags(s.flags()) {}

    ~streamstate_restorer() {
        restore();
    }

    void restore() {
        m_stream.fill(m_fill);
        m_stream.width(m_width);
        m_stream.precision(m_precision);
        m_stream.flags(m_fmt_flags);
    }

    streamstate_restorer(streamstate_restorer const&) = delete;

private:
    std::ostream& m_stream;
    std::streamsize const m_precision;
    std::streamsize const m_width;
    std::ostream::char_type const m_fill;
    std::ostream::fmtflags const m_fmt_flags;
};

#if defined(__clang__)
#    pragma clang diagnostic pop
#endif

} // namespace fmt

class nanobench {
    using Clock = std::chrono::high_resolution_clock;

    std::string m_name{};
    double m_batch{1.0};
    std::string m_unit{"op"};

public:
    struct overflow_error : public std::runtime_error {
        inline explicit overflow_error(std::string const& msg)
            : std::runtime_error(msg) {}
        inline explicit overflow_error(const char* msg)
            : std::runtime_error(msg) {}

        overflow_error(const overflow_error&) = default;
        overflow_error& operator=(const overflow_error&) = default;
        overflow_error(overflow_error&&) = default;
        overflow_error& operator=(overflow_error&&) = default;
        virtual ~overflow_error() = default;
    };

    explicit nanobench(std::string name)
        : m_name(std::move(name)) {}

    nanobench& batch(double b) noexcept {
        m_batch = b;
        return *this;
    }

    nanobench& batch(uint64_t b) noexcept {
        return batch(static_cast<double>(b));
    }

    nanobench& unit(std::string unit) {
        m_unit = std::move(unit);
        return *this;
    }

    template <typename Op>
    nanobench const& run(Op op) const {
        size_t const numEvals = 21;
        auto const targetRuntime = detail::clockResolution<Clock>() * 1000;

        std::vector<double> sec_per_iter;
        sec_per_iter.reserve(numEvals);

        size_t numIters = 1;

        while (sec_per_iter.size() != numEvals) {
            auto n = numIters;

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
            if (elapsed * 10 < targetRuntime) {
                if (numIters * 10 < numIters) {
                    detail::throwOverflow();
                    return *this;
                }
                numIters *= 10;
            } else {
                auto mult = numIters * static_cast<size_t>(targetRuntime.count());
                if (mult < numIters) {
                    detail::throwOverflow();
                    return *this;
                }
                numIters = (numIters * targetRuntime) / elapsed;
                if (numIters == 0) {
                    numIters = 1;
                }
            }

            // if we are within 2/3 of the target runtime, add it.
            if (elapsed * 3 >= targetRuntime * 2) {
                auto result =
                    std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count() /
                    static_cast<double>(numIters);

                sec_per_iter.push_back(result);
            }
        }

        show_results(sec_per_iter);
        return *this;
    }

    template <typename... Args>
    static void do_not_optimize_away(Args&&... args) {
        (void)std::initializer_list<int>{(detail::do_not_optimize_away_sink(args), 0)...};
    }

private:
    double calc_median(std::vector<double> const& results) const {
        auto mid = results.size() / 2;
        if (results.size() & 1) {
            return results[mid];
        }
        return (results[mid - 1] + results[mid]) / 2;
    }

    double calc_median_absolute_percentage_error(std::vector<double> const& results,
                                                 double median) const {
        std::vector<double> absolute_percentage_errors;
        for (auto r : results) {
            absolute_percentage_errors.push_back(std::fabs(r - median) / r);
        }
        std::sort(absolute_percentage_errors.begin(), absolute_percentage_errors.end());

        return calc_median(absolute_percentage_errors);
    }

    void show_results(std::vector<double>& sec_per_iter) const {

        std::vector<double> iter_per_sec;
        for (auto t : sec_per_iter) {
            iter_per_sec.push_back(1.0 / t);
        }

        std::sort(iter_per_sec.begin(), iter_per_sec.end());
        auto const med_iter_per_sec = calc_median(iter_per_sec);
        // auto const mdaps_iter_per_sec = calc_median_absolute_percentage_error(iter_per_sec,
        // med_iter_per_sec);

        std::sort(sec_per_iter.begin(), sec_per_iter.end());
        auto const med_sec_per_iter = calc_median(sec_per_iter);
        auto const mdaps_sec_per_iter =
            calc_median_absolute_percentage_error(sec_per_iter, med_sec_per_iter);

        fmt::streamstate_restorer restoreStream(std::cout);
        std::cout.imbue(std::locale(std::cout.getloc(), new fmt::num_sep(',')));

        // show final result, the median

        auto const med_ns_per_op = 1e9 * med_sec_per_iter / m_batch;
        auto const med_ops = med_iter_per_sec * m_batch;

        std::cout << std::fixed << std::setprecision(2) << std::setw(18) << std::right
                  << med_ns_per_op << " ns/" << std::setw(2) << std::left << m_unit << " "
                  << std::setw(18) << std::right << med_ops << " " << std::setw(2) << m_unit
                  << "/s +-" << std::setw(5) << std::setprecision(1) << (mdaps_sec_per_iter * 100)
                  << "%  " << m_name << std::endl;
    }
};

namespace detail {

static void throwOverflow() {
    throw nanobench::overflow_error(
        "Cannot find a working number of iterations for reliable results. "
        "Maybe your code got optimized away?");
}

} // namespace detail

} // namespace ankerl

#endif
