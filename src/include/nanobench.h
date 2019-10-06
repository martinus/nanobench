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
#define ROBIN_HOOD_VERSION_MAJOR 0 // for incompatible API changes
#define ROBIN_HOOD_VERSION_MINOR 0 // for adding functionality in a backwards-compatible manner
#define ROBIN_HOOD_VERSION_PATCH 1 // for backwards-compatible bug fixes

#include <algorithm>
#include <chrono>
#include <functional>
#include <initializer_list>
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
inline void noopSink(void const*) {}
#    pragma optimize("", on

#else

template <typename T>
struct NoopNeedsIndirect {
    using D = typename std::decay<T>::type;
    constexpr static bool value = !std::is_trivially_copyable<D>::value ||
                                  sizeof(D) > sizeof(long) || std::is_pointer<D>::value;
};

template <typename T>
auto noopSink(T const& val) -> typename std::enable_if<!NoopNeedsIndirect<T>::value>::type {
    // see https://github.com/facebook/folly/blob/master/folly/Benchmark.h
    // Tells the compiler that we read val from memory and might read/write
    // from any memory location.
    asm volatile("" ::"m"(val) : "memory");
}

template <typename T>
auto noopSink(T const& val) -> typename std::enable_if<NoopNeedsIndirect<T>::value>::type {
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

} // namespace detail

template <typename... Args>
void noop(Args&&... args) {
    (void)std::initializer_list<int>{(detail::noopSink(args), 0)...};
}

class nanobench {
    using Clock = std::chrono::high_resolution_clock;

    std::string m_name{};
    size_t m_batch{1};
    size_t m_iters{1};

public:
    template <typename Op, typename = typename std::enable_if<
                               std::is_constructible<std::function<void()>, Op>::value>::type>
    nanobench(Op op) {
        run(op);
    }

    explicit nanobench(std::string name)
        : m_name(std::move(name)) {}

    nanobench& batch(size_t batch) noexcept {
        m_batch = batch;
        return *this;
    }

    nanobench& iters(size_t iters) noexcept {
        m_iters = iters;
        return *this;
    }

    template <typename Op>
    nanobench const& run(Op op) const {
        size_t const numEvals = 101;

        auto const targetRuntime = detail::clockResolution<Clock>() * 1000;

        std::vector<double> results;
        results.reserve(numEvals);

        size_t numIters = 1;

        while (results.size() != numEvals) {
            auto n = numIters;
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
                    std::cout << "overflow in *10!" << std::endl;
                    return *this;
                }
                numIters *= 10;
            } else {
                auto mult = numIters * static_cast<size_t>(targetRuntime.count());
                if (mult < numIters) {
                    std::cout << "overflow in mult!" << std::endl;
                    return *this;
                }
                numIters = (numIters * targetRuntime) / elapsed;
            }

            // if we are within 50% of the target time, add this result
            auto diff = targetRuntime.count() - elapsed.count();
            if (diff < 0) {
                diff = -diff;
            }
            if (diff * 100 / targetRuntime.count() < 20) {
                auto result =
                    std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count() /
                    static_cast<double>(numIters);

                results.push_back(result);
            }
        }

        // show final result, the median
        std::sort(results.begin(), results.end());
        auto r = results[results.size() / 2];

        std::cout << (r * 1e9) << " ns";
        if (!m_name.empty()) {
            std::cout << " for " << m_name;
        }
        std::cout << std::endl;

        return *this;
    }

    template <typename... Args>
    nanobench const& noop(Args&&... args) const noexcept {
        ::ankerl::noop(std::forward<Args>(args)...);
        return *this;
    }
};

} // namespace ankerl

#endif
