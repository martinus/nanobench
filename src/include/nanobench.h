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
#define ANKERL_NANOBENCH_VERSION_MAJOR 1 // incompatible API changes
#define ANKERL_NANOBENCH_VERSION_MINOR 0 // backwards-compatible changes
#define ANKERL_NANOBENCH_VERSION_PATCH 0 // backwards-compatible bug fixes

#include <algorithm> // sort
#include <chrono>    // high_resolution_clock
#include <cmath>     // fabs
#include <iomanip>   // setw, setprecision
#include <iostream>  // cout
#include <vector>    // manage results

namespace ankerl {
namespace nanobench {

using Clock = std::chrono::high_resolution_clock;

// helper stuff that only intended to be used internally
namespace detail {

// remembers the unit that was last used. Once it changes, a new table header is automatically written for the next entry.
inline std::string& lastUnitUsed() {
    static std::string sUnit = {};
    return sUnit;
}

// Windows version of do not optimize away
#if defined(_MSC_VER)
// see https://docs.microsoft.com/en-us/cpp/preprocessor/optimize
#    pragma optimize("", off)
inline void doNotOptimizeAwaySink(void const*) {}
#    pragma optimize("", on

#else

template <typename T>
struct DoNotOptimizeAwayNeedsIndirect {
    using D = typename std::decay<T>::type;
    constexpr static bool value = !std::is_trivially_copyable<D>::value || sizeof(D) > sizeof(long) || std::is_pointer<D>::value;
};

template <typename T>
auto doNotOptimizeAwaySink(T const& val) -> typename std::enable_if<!DoNotOptimizeAwayNeedsIndirect<T>::value>::type {
    // see https://github.com/facebook/folly/blob/master/folly/Benchmark.h
    // Tells the compiler that we read val from memory and might read/write
    // from any memory location.
    asm volatile("" ::"m"(val) : "memory");
}

template <typename T>
auto doNotOptimizeAwaySink(T const& val) -> typename std::enable_if<DoNotOptimizeAwayNeedsIndirect<T>::value>::type {
    // the "r" forces compiler to make val available in a register, so it must have been loaded.
    // Only works when small enough (<= sizeof(long)), trivial, and no pointer
    asm volatile("" ::"r"(val));
}

#endif

// determines resolution of the given clock. This is done by measuring multiple times and returning the minimum time difference.
inline Clock::duration calcClockResolution(size_t numEvaluations) noexcept {
    auto bestDuration = Clock::duration::max();
    Clock::time_point tBegin;
    Clock::time_point tEnd;
    for (size_t i = 0; i < numEvaluations; ++i) {
        tBegin = Clock::now();
        do {
            tEnd = Clock::now();
        } while (tBegin == tEnd);
        bestDuration = (std::min)(bestDuration, tEnd - tBegin);
    }
    return bestDuration;
}

// Calculates clock resolution once, and remembers the result
inline Clock::duration clockResolution() noexcept {
    static Clock::duration sResolution = calcClockResolution(20);
    return sResolution;
}

// formatting utilities
namespace fmt {

// adds thousands separator to numbers
class NumSep : public std::numpunct<char> {
public:
    NumSep(char sep)
        : mSep(sep) {}

    char do_thousands_sep() const {
        return mSep;
    }

    std::string do_grouping() const {
        return "\003";
    }

private:
    char mSep;
};

// RAII to save & restore a stream's state
class StreamStateRestorer {
public:
    explicit StreamStateRestorer(std::ostream& s)
        : mStream(s)
        , mLocale(s.getloc())
        , mPrecision(s.precision())
        , mWidth(s.width())
        , mFill(s.fill())
        , mFmtFlags(s.flags()) {}

    ~StreamStateRestorer() {
        restore();
    }

    // sets back all stream info that we remembered at construction
    void restore() {
        mStream.imbue(mLocale);
        mStream.precision(mPrecision);
        mStream.width(mWidth);
        mStream.fill(mFill);
        mStream.flags(mFmtFlags);
    }

    // don't allow copying / moving
    StreamStateRestorer(StreamStateRestorer const&) = delete;

private:
    std::ostream& mStream;
    std::locale mLocale;
    std::streamsize const mPrecision;
    std::streamsize const mWidth;
    std::ostream::char_type const mFill;
    std::ostream::fmtflags const mFmtFlags;
};

// Number formatter
class Number {
public:
    Number(int width, int precision, double value)
        : mWidth(width)
        , mPrecision(precision)
        , mValue(value) {}

private:
    friend std::ostream& operator<<(std::ostream& os, Number const& n);

    std::ostream& write(std::ostream& os) const {
        StreamStateRestorer restorer(os);
        os.imbue(std::locale(os.getloc(), new NumSep(',')));
        os << std::setw(mWidth) << std::setprecision(mPrecision) << std::fixed << mValue;
        return os;
    }

    int mWidth;
    int mPrecision;
    double mValue;
};

inline std::ostream& operator<<(std::ostream& os, Number const& n) {
    return n.write(os);
}

// Formats any text as markdown inline code, escaping backticks.
class MarkDownCode {
public:
    MarkDownCode(std::string what)
        : mWhat(std::move(what)) {}

private:
    friend std::ostream& operator<<(std::ostream& os, MarkDownCode const& mdCode);

    std::ostream& write(std::ostream& os) const {
        os.put('`');
        for (char c : mWhat) {
            os.put(c);
            if ('`' == c) {
                os.put('`');
            }
        }
        os.put('`');
        return os;
    }

    std::string mWhat;
};

inline std::ostream& operator<<(std::ostream& os, MarkDownCode const& mdCode) {
    return mdCode.write(os);
}

} // namespace fmt

// mathy statistic stuff
namespace statistics {

// calculates median, and properly handles even number of elements too.
inline double calcMedian(std::vector<double> const& results) {
    auto mid = results.size() / 2;
    if (results.size() & 1) {
        return results[mid];
    }
    return (results[mid - 1] + results[mid]) / 2;
}

// calculates MdAPE which is the median of percentage error
// see https://www.spiderfinancial.com/support/documentation/numxl/reference-manual/forecasting-performance/mdape
inline double calcMedianAbsolutePercentageError(std::vector<double> const& results, double median) {
    std::vector<double> absolutePercentageErrors;
    for (auto r : results) {
        absolutePercentageErrors.push_back(std::fabs(r - median) / r);
    }
    std::sort(absolutePercentageErrors.begin(), absolutePercentageErrors.end());

    return calcMedian(absolutePercentageErrors);
}

} // namespace statistics
} // namespace detail

// Makes sure none of the given arguments are optimized away by the compiler.
template <typename... Args>
void doNotOptimizeAway(Args&&... args) {
    (void)std::initializer_list<int>{(detail::doNotOptimizeAwaySink(args), 0)...};
}

// Result returned after a benchmark has finished. Can be used as a baseline for relative().
class Result {
public:
    explicit Result(double ups) noexcept
        : mUnitPerSec(ups) {}

    Result() = default;

    // Operations per second
    double unitPerSec() const noexcept {
        return mUnitPerSec;
    }

    // Makes sure none of the given arguments are optimized away by the compiler.
    template <typename... Args>
    Result& doNotOptimizeAway(Args&&... args) {
        ::ankerl::nanobench::doNotOptimizeAway(std::forward<Args>(args)...);
        return *this;
    }

private:
    double mUnitPerSec = {-1};
};

// Configuration of a microbenchmark.
class Config {
public:
    Config() = default;

    Config(Config&&) = default;
    Config& operator=(Config&&) = default;
    Config(Config const&) = default;
    Config& operator=(Config const&) = default;

    // Set the batch size, e.g. number of processed bytes, or some other metric for the size of the processed data in each iteration.
    // Any argument is cast to double.
    template <typename T>
    Config& batch(T b) noexcept {
        mBatch = static_cast<double>(b);
        return *this;
    }

    // Set a baseline to compare it to. 100% it is exactly as fast as the baseline, >100% means it is faster than the baseline, <100%
    // means it is slower than the baseline.
    Config& relative(Result const& baseline) noexcept {
        mRelative = baseline;
        return *this;
    }

    // Operation unit. Defaults to "op", could be e.g. "byte" for string processing.
    // Use singular (byte, not bytes).
    Config& unit(std::string unit) {
        mUnit = std::move(unit);
        return *this;
    }

    // Number of epochs to evaluate. The reported result will be the median of evaluation of each epoch.
    Config& epochs(size_t numEpochs) noexcept {
        mNumEpochs = numEpochs;
        return *this;
    }

    // Desired evaluation time is a multiple of clock resolution. Default is to be 1000 times above this measurement precision.
    Config& clockResolutionMultiple(size_t multiple) noexcept {
        mClockResolutionMultiple = multiple;
        return *this;
    }

    // Sets the maximum time each epoch should take. Default is 100ms.
    Config& maxEpochTime(std::chrono::nanoseconds t) noexcept {
        mMaxEpochTime = t;
        return *this;
    }

    // Where to write the output to? Defaults to std::cout
    Config& out(std::ostream& out) noexcept {
        mOut = &out;
        return *this;
    }

    Config& warmup(size_t numWarmupIters) noexcept {
        mWarmup = numWarmupIters;
        return *this;
    }

    // Performs all evaluations.
    template <typename Op>
    Result run(std::string name, Op op) const {
#if 0
        detail::Measurements measurements(mClockResolutionMultiple, mMaxEpochTime, mNumEpochs);
        do {
            auto before = Clock::now();
            while (n > 0) {
                op();
                --n;
            }
            auto after = Clock::now();
        } while (measurements.add(before, after));
        return showResult(measurements.data(), measurements.error());
#endif
        auto targetRuntime = detail::clockResolution() * mClockResolutionMultiple;
        if (targetRuntime > mMaxEpochTime) {
            targetRuntime = mMaxEpochTime;
        }

        std::vector<double> secPerIter;
        secPerIter.reserve(mNumEpochs);

        size_t numIters = mWarmup;

        bool isWarmup = mWarmup != 0;
        while (secPerIter.size() != mNumEpochs) {
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
                    return showResult(name, secPerIter, "iterations overflow. Maybe your code got optimized away?");
                }
                numIters *= 10;
            } else {
                auto mult = numIters * static_cast<size_t>(targetRuntime.count());
                if (mult < numIters) {
                    return showResult(name, secPerIter, "iterations overflow. Maybe your code got optimized away?");
                }
                numIters = (numIters * targetRuntime) / elapsed;
                if (numIters == 0) {
                    numIters = 1;
                }
            }

            // if we are within 2/3 of the target runtime, add it.
            if (elapsed * 3 >= targetRuntime * 2 && !isWarmup) {
                auto result =
                    std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count() / static_cast<double>(numIters);

                secPerIter.push_back(result);
            }

            isWarmup = false;
        }
        return showResult(name, secPerIter, "");
    }

private:
    Result showResult(std::string const& name, std::vector<double>& secPerIter, std::string errorMessage) const {
        auto& os = *mOut;
        if (!errorMessage.empty()) {
            os << "|        - |                   - |                   - |       - | :boom: " << errorMessage << ' '
               << detail::fmt::MarkDownCode(name) << std::endl;
            return Result(-1);
        }
        std::vector<double> iterPerSec;
        for (auto t : secPerIter) {
            iterPerSec.push_back(1.0 / t);
        }

        std::sort(iterPerSec.begin(), iterPerSec.end());
        auto const medianIterPerSec = detail::statistics::calcMedian(iterPerSec);

        std::sort(secPerIter.begin(), secPerIter.end());
        auto const medianSecPerIter = detail::statistics::calcMedian(secPerIter);
        auto const mdapsSecPerIter = detail::statistics::calcMedianAbsolutePercentageError(secPerIter, medianSecPerIter);

        detail::fmt::StreamStateRestorer restorer(os);

        // show final result, the median
        auto const medianNsPerUnit = 1e9 * medianSecPerIter / mBatch;
        auto const medianUnitPerSec = medianIterPerSec * mBatch;

        if (detail::lastUnitUsed() != mUnit) {
            detail::lastUnitUsed() = mUnit;
            os << std::endl
               << "| relative |" << std::setw(20) << std::right << ("ns/" + mUnit) << " |" << std::setw(20) << std::right
               << (mUnit + "/s") << " |   MdAPE | benchmark" << std::endl
               << "|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------"
               << std::endl;
        }

        // we want output that looks like this:
        // |  1208.4% |               14.15 |       70,649,422.38 |    0.3% | `std::vector<std::string> emplace + release`

        // 1st column: relative
        os << '|';
        if (mRelative.unitPerSec() <= 0) {
            // relative not set or invalid, print blank column
            os << "          |";
        } else {
            os << detail::fmt::Number(8, 1, medianUnitPerSec / mRelative.unitPerSec() * 100) << "% |";
        }

        // 2nd column: ns/unit
        os << detail::fmt::Number(20, 2, medianNsPerUnit) << " |";

        // 3rd column: unit/s
        os << detail::fmt::Number(20, 2, medianUnitPerSec) << " |";

        // 4th column: MdAPE
        os << detail::fmt::Number(7, 1, mdapsSecPerIter * 100) << "% |";

        // 5th column: possible symbols, possibly errormessage, benchmark name
        if (mdapsSecPerIter >= 0.05) {
            // >=5%
            os << " :wavy_dash:";
        }
        os << ' ' << detail::fmt::MarkDownCode(name) << std::endl;

        return Result(medianUnitPerSec);
    }

    double mBatch = 1.0;
    std::string mUnit = "op";
    size_t mNumEpochs = 51;
    uint64_t mClockResolutionMultiple = UINT64_C(1000);
    std::chrono::nanoseconds mMaxEpochTime = std::chrono::milliseconds(100);
    Result mRelative = {};
    std::ostream* mOut = &std::cout;
    size_t mWarmup = 0;
};

// Small Fast Counting RNG, version 4
class Rng final {
public:
    using state_type = std::array<uint64_t, 4>;
    using result_type = uint64_t;

    static constexpr uint64_t(min)() {
        return 0;
    }
    static constexpr uint64_t(max)() {
        return (std::numeric_limits<uint64_t>::max)();
    }

    Rng()
        : Rng(UINT64_C(0xd3b45fd780a1b6a3)) {}

    // don't allow copying, it's dangerous
    Rng(Rng const&) = delete;

    explicit Rng(uint64_t seed) noexcept
        : mA(seed)
        , mB(seed)
        , mC(seed)
        , mCounter(1) {
        for (size_t i = 0; i < 12; ++i) {
            operator()();
        }
    }

    state_type state() const noexcept {
        return {mA, mB, mC, mCounter};
    }

    void state(state_type const& state) noexcept {
        mA = state[0];
        mB = state[1];
        mC = state[2];
        mCounter = state[3];
    }

    uint64_t operator()() noexcept {
        uint64_t const tmp = mA + mB + mCounter++;
        mA = mB ^ (mB >> 11);
        mB = mC + (mC << 3);
        mC = rotl(mC, 24) + tmp;
        return tmp;
    }

private:
    static constexpr uint64_t rotl(uint64_t const x, int k) noexcept {
        return (x << k) | (x >> (64 - k));
    }

    uint64_t mA;
    uint64_t mB;
    uint64_t mC;
    uint64_t mCounter;
};

// convenience helper to directly call Config().run(...)
template <typename Op>
inline Result run(std::string name, Op&& op) noexcept {
    return Config().run(std::move(name), std::forward<Op>(op));
}

inline void tableHeader() {
    // reset lastUnit, so it's printed next time
    detail::lastUnitUsed() = "";
}

} // namespace nanobench
} // namespace ankerl

#endif
