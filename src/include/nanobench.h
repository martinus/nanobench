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
#include <cstdlib>   // getenv
#include <fstream>   // ifstream to parse proc files
#include <iomanip>   // setw, setprecision
#include <iostream>  // cout
#include <vector>    // manage results

#if __linux__
#    include <unistd.h> //sysconf
#endif

// macros /////////////////////////////////////////////////////////////////////////////////////////

// all non-argument macros should use this facility. See
// https://www.fluentcpp.com/2019/05/28/better-macros-better-flags/
#define ANKERL_NANOBENCH(x) ANKERL_NANOBENCH_PRIVATE_DEFINITION_##x()

// inline
#ifdef _WIN32
#    define ANKERL_NANOBENCH_PRIVATE_DEFINITION_NOINLINE() __declspec(noinline)
#else
#    define ANKERL_NANOBENCH_PRIVATE_DEFINITION_NOINLINE() __attribute__((noinline))
#endif

// declarations ///////////////////////////////////////////////////////////////////////////////////

namespace ankerl {
namespace nanobench {

using Clock = std::chrono::high_resolution_clock;

// helper stuff that only intended to be used internally
namespace detail {

struct TableInfo;
class Measurements;

// formatting utilities
namespace fmt {

class NumSep;
class StreamStateRestorer;
class Number;
class MarkDownCode;

} // namespace fmt
} // namespace detail

// Result returned after a benchmark has finished. Can be used as a baseline for relative().
class Result;
class Config;
class Rng;

} // namespace nanobench
} // namespace ankerl

// definitions ////////////////////////////////////////////////////////////////////////////////////

namespace ankerl {
namespace nanobench {

// Result returned after a benchmark has finished. Can be used as a baseline for relative().
class Result {
public:
    inline Result(std::string const& unit, std::vector<std::chrono::duration<double>> secPerUnit) noexcept;
    inline Result();

    inline std::string const& unit() const noexcept;
    inline std::chrono::duration<double> median() const noexcept;
    inline double medianAbsolutePercentError() const noexcept;
    inline std::chrono::duration<double> minimum() const noexcept;
    inline std::chrono::duration<double> maximum() const noexcept;

    // Convenience: makes sure none of the given arguments are optimized away by the compiler.
    template <typename... Args>
    Result& doNotOptimizeAway(Args&&... args);

private:
    // calculates median, and properly handles even number of elements too.
    template <typename T>
    static T calcMedian(std::vector<T> const& sortedResults);

    // calculates MdAPE which is the median of percentage error
    // see https://www.spiderfinancial.com/support/documentation/numxl/reference-manual/forecasting-performance/mdape
    inline static double calcMedianAbsolutePercentageError(std::vector<std::chrono::duration<double>> const& results,
                                                           std::chrono::duration<double> median);

    std::string mUnit = "";
    std::chrono::duration<double> mMedian{};
    double mMedianAbsolutePercentError{};
    std::chrono::duration<double> mMinimum{};
    std::chrono::duration<double> mMaximum{};
};

// helper stuff that only intended to be used internally
namespace detail {

template <typename T>
T parseFile(std::string filename);

inline void printStabilityInformationOnce();

struct TableInfo {
    std::string unit{};
    std::string title{};
};

// remembers the last table settings used. When it changes, a new table header is automatically written for the new entry.
inline TableInfo& singletonLastTableSetting();

// Windows version of do not optimize away
// see https://github.com/google/benchmark/blob/master/include/benchmark/benchmark.h#L307
// see https://github.com/facebook/folly/blob/master/folly/Benchmark.h#L280
// see https://docs.microsoft.com/en-us/cpp/preprocessor/optimize
#if defined(_MSC_VER)
inline void doNotOptimizeAwaySink(void const*);

template <typename T>
inline void doNotOptimizeAway(T const& val);

#else

template <typename T>
void doNotOptimizeAway(T const& val);

template <typename T>
void doNotOptimizeAway(T& value);

#endif

// determines resolution of the given clock. This is done by measuring multiple times and returning the minimum time difference.
inline Clock::duration calcClockResolution(size_t numEvaluations) noexcept;

// Calculates clock resolution once, and remembers the result
inline Clock::duration clockResolution() noexcept;

class Measurements {
public:
    inline Measurements(Config const& config, std::string const& name) noexcept;
    inline size_t numIters() const noexcept;
    inline void add(std::chrono::nanoseconds runtime) noexcept;

    inline Result const& result() const;

private:
    inline Result showResult(std::string const& name, std::vector<std::chrono::duration<double>>& secPerUnit,
                             std::string errorMessage) const;

    Config const& mConfig;
    std::chrono::nanoseconds mTargetRuntime{};
    std::vector<std::chrono::duration<double>> mSecPerUnit{};
    size_t mNumIters = 0;
    std::string mName{};
    Result mResult{};
    bool mIsWarmup = false;
};

// formatting utilities
namespace fmt {

// adds thousands separator to numbers
class NumSep : public std::numpunct<char> {
public:
    inline NumSep(char sep);
    inline char do_thousands_sep() const;
    inline std::string do_grouping() const;

private:
    char mSep;
};

// RAII to save & restore a stream's state
class StreamStateRestorer {
public:
    inline explicit StreamStateRestorer(std::ostream& s);
    inline ~StreamStateRestorer();

    // sets back all stream info that we remembered at construction
    inline void restore();

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
    inline Number(int width, int precision, double value);

private:
    friend std::ostream& operator<<(std::ostream& os, Number const& n);
    inline std::ostream& write(std::ostream& os) const;

    int mWidth;
    int mPrecision;
    double mValue;
};

inline std::ostream& operator<<(std::ostream& os, Number const& n);

// Formats any text as markdown inline code, escaping backticks.
class MarkDownCode {
public:
    inline MarkDownCode(std::string what);

private:
    friend std::ostream& operator<<(std::ostream& os, MarkDownCode const& mdCode);
    inline std::ostream& write(std::ostream& os) const;

    std::string mWhat;
};

inline std::ostream& operator<<(std::ostream& os, MarkDownCode const& mdCode);

} // namespace fmt
} // namespace detail

// Makes sure none of the given arguments are optimized away by the compiler.
template <typename... Args>
void doNotOptimizeAway(Args&&... args);

// Configuration of a microbenchmark.
class Config {
public:
    inline Config();

    inline Config(Config&&);
    inline Config& operator=(Config&&);
    inline Config(Config const&);
    inline Config& operator=(Config const&);

    // Set the batch size, e.g. number of processed bytes, or some other metric for the size of the processed data in each iteration.
    // Any argument is cast to double.
    template <typename T>
    Config& batch(T b) noexcept;
    inline double batch() const noexcept;

    // Set a baseline to compare it to. 100% it is exactly as fast as the baseline, >100% means it is faster than the baseline, <100%
    // means it is slower than the baseline.
    inline Config& relative(Result const& baseline) noexcept;
    inline Result const& relative() const noexcept;

    // Operation unit. Defaults to "op", could be e.g. "byte" for string processing.
    // Use singular (byte, not bytes).
    inline Config& unit(std::string unit);
    inline std::string const& unit() const noexcept;

    inline Config& title(std::string benchmarkTitle);
    inline std::string const& title() const noexcept;

    // Number of epochs to evaluate. The reported result will be the median of evaluation of each epoch.
    inline Config& epochs(size_t numEpochs) noexcept;
    inline size_t epochs() const noexcept;

    // Desired evaluation time is a multiple of clock resolution. Default is to be 1000 times above this measurement precision.
    inline Config& clockResolutionMultiple(size_t multiple) noexcept;
    inline size_t clockResolutionMultiple() const noexcept;

    // Sets the maximum time each epoch should take. Default is 100ms.
    inline Config& maxEpochTime(std::chrono::nanoseconds t) noexcept;
    inline std::chrono::nanoseconds maxEpochTime() const noexcept;

    // Where to write the output to? Defaults to std::cout
    inline Config& out(std::ostream& out) noexcept;
    inline std::ostream& out() const noexcept;

    inline Config& warmup(size_t numWarmupIters) noexcept;
    inline size_t warmup() const noexcept;

    // Performs all evaluations.
    template <typename Op>
    Result run(std::string name, Op op) const;

private:
    std::string mBenchmarkTitle = "benchmark";
    std::string mUnit = "op";
    double mBatch = 1.0;
    size_t mNumEpochs = 51;
    uint64_t mClockResolutionMultiple = UINT64_C(1000);
    std::chrono::nanoseconds mMaxEpochTime = std::chrono::milliseconds(100);
    Result mRelative{};
    std::ostream* mOut = &std::cout;
    size_t mWarmup = 0;
};

// Small Fast Counting RNG, version 4
class Rng final {
public:
    using state_type = std::array<uint64_t, 4>;
    using result_type = uint64_t;

    static constexpr uint64_t(min)();
    static constexpr uint64_t(max)();

    inline constexpr Rng();

    // don't allow copying, it's dangerous
    Rng(Rng const&) = delete;

    inline constexpr explicit Rng(uint64_t seed) noexcept;

    inline constexpr state_type state() const noexcept;
    inline constexpr void state(state_type const& state) noexcept;
    inline constexpr uint64_t operator()() noexcept;

private:
    inline static constexpr uint64_t rotl(uint64_t const x, int k) noexcept;

    uint64_t mA;
    uint64_t mB;
    uint64_t mC;
    uint64_t mCounter;
};

// convenience helper to directly call Config().run(...)
template <typename Op>
Result run(std::string name, Op op) noexcept;

inline void forceTableHeader();

} // namespace nanobench
} // namespace ankerl

// implementation /////////////////////////////////////////////////////////////////////////////////

namespace ankerl {
namespace nanobench {
namespace detail {

template <typename T>
T parseFile(std::string filename) {
    std::ifstream fin(filename);
    T num{};
    fin >> num;
    return num;
}

inline void printStabilityInformationOnce() {
    static bool shouldPrint = true;
    if (shouldPrint) {
        shouldPrint = false;
#if !defined(NDEBUG)
        std::cerr << "Warning: NDEBUG not defined, this is a debug build" << std::endl;
#endif

#if __linux__
        auto nprocs = sysconf(_SC_NPROCESSORS_CONF);
        if (nprocs <= 0) {
            std::cerr << "Warning: Can't figure out number of processors." << std::endl;
            return;
        }

        // check frequency scaling
        bool isFrequencyLocked = true;
        bool isGovernorPerformance = "performance" == parseFile<std::string>("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
        for (long id = 0; id < nprocs; ++id) {
            auto sysCpu = "/sys/devices/system/cpu/cpu" + std::to_string(id);
            auto minFreq = parseFile<int64_t>(sysCpu + "/cpufreq/scaling_min_freq");
            auto maxFreq = parseFile<int64_t>(sysCpu + "/cpufreq/scaling_max_freq");
            if (minFreq != maxFreq) {
                isFrequencyLocked = false;
            }
        }
        bool isTurbo = 0 == parseFile<int>("/sys/devices/system/cpu/intel_pstate/no_turbo");
        if (!isFrequencyLocked) {
            std::cerr << "Warning: CPU frequency scaling enabled, results will be invalid" << std::endl;
        }
        if (!isGovernorPerformance) {
            std::cerr << "Warning: CPU governor is not performance, results will be invalid" << std::endl;
        }
        if (isTurbo) {
            std::cerr << "Warning: Turbo is enabled" << std::endl;
        }

        if (!isFrequencyLocked || !isGovernorPerformance || isTurbo) {
            std::cerr << "Recommendation: use 'pyperf system tune' before benchmarking. See https://pypi.org/project/pyperf/"
                      << std::endl;
        }
#endif
    }
}

// remembers the last table settings used. When it changes, a new table header is automatically written for the new entry.
inline TableInfo& singletonLastTableSetting() {
    static TableInfo sTableInfo = {};
    return sTableInfo;
}

// Windows version of do not optimize away
// see https://github.com/google/benchmark/blob/master/include/benchmark/benchmark.h#L307
// see https://github.com/facebook/folly/blob/master/folly/Benchmark.h#L280
// see https://docs.microsoft.com/en-us/cpp/preprocessor/optimize
#if defined(_MSC_VER)
#    pragma optimize("", off)
inline void doNotOptimizeAwaySink(void const*) {}
#    pragma optimize("", on)

template <typename T>
inline void doNotOptimizeAway(T const& val) {
    doNotOptimizeAwaySink(&val);
}

#else

template <typename T>
void doNotOptimizeAway(T const& val) {
    asm volatile("" : : "r,m"(val) : "memory");
}

template <typename T>
void doNotOptimizeAway(T& value) {
#    if defined(__clang__)
    asm volatile("" : "+r,m"(value) : : "memory");
#    else
    asm volatile("" : "+m,r"(value) : : "memory");
#    endif
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

Measurements::Measurements(Config const& config, std::string const& name) noexcept
    : mConfig(config)
    , mName(name) {
    printStabilityInformationOnce();

    mTargetRuntime = detail::clockResolution() * mConfig.clockResolutionMultiple();
    if (mTargetRuntime > mConfig.maxEpochTime()) {
        mTargetRuntime = mConfig.maxEpochTime();
    }

    mSecPerUnit.reserve(mConfig.epochs());
    mNumIters = mConfig.warmup();

    // check environment variable NANOBENCH_ENDLESS
    auto endless = std::getenv("NANOBENCH_ENDLESS");
    if (endless && endless == name) {
        std::cout << "NANOBENCH_ENDLESS set: running '" << name << "' endlessly" << std::endl;
        mNumIters = (std::numeric_limits<size_t>::max)();
    }

    mIsWarmup = mConfig.warmup() != 0;
    if (mNumIters == 0) {
        mNumIters = 1;
    }
}

size_t Measurements::numIters() const noexcept {
    return mNumIters;
}

void Measurements::add(std::chrono::nanoseconds elapsed) noexcept {
    // adapt n
    if (elapsed * 10 < mTargetRuntime) {
        if (mNumIters * 10 < mNumIters) {
            mResult = showResult(mName, mSecPerUnit,
                                 "iterations overflow at " + std::to_string(mNumIters) + ". Maybe your code got optimized away?");
            mNumIters = 0;
            return;
        }
        mNumIters *= 10;
    } else {
        auto mult = mNumIters * static_cast<size_t>(mTargetRuntime.count());
        if (mult < mNumIters) {
            mResult = showResult(mName, mSecPerUnit,
                                 "iterations overflow at " + std::to_string(mNumIters) + ". Maybe your code got optimized away?");
            mNumIters = 0;
            return;
        }
        mNumIters = (mNumIters * mTargetRuntime) / elapsed;
        if (mNumIters == 0) {
            mNumIters = 1;
        }
    }

    // if we are within 2/3 of the target runtime, add it.
    if (elapsed * 3 >= mTargetRuntime * 2 && !mIsWarmup) {
        mSecPerUnit.push_back(std::chrono::duration_cast<std::chrono::duration<double>>(elapsed) /
                              (mConfig.batch() * static_cast<double>(mNumIters)));
        if (mSecPerUnit.size() == mConfig.epochs()) {
            mResult = showResult(mName, mSecPerUnit, "");
            mNumIters = 0;
            return;
        }
    }

    mIsWarmup = false;
}

Result const& Measurements::result() const {
    return mResult;
}

Result Measurements::showResult(std::string const& name, std::vector<std::chrono::duration<double>>& secPerUnit,
                                std::string errorMessage) const {
    auto& os = mConfig.out();
    if (!errorMessage.empty()) {
        os << "|        - |                   - |                   - |       - | :boom: " << errorMessage << ' '
           << detail::fmt::MarkDownCode(name) << std::endl;
        return Result();
    }

    Result result(mConfig.unit(), secPerUnit);

    detail::fmt::StreamStateRestorer restorer(os);
    auto& lastTableSetting = detail::singletonLastTableSetting();
    if (lastTableSetting.title != mConfig.title() || lastTableSetting.unit != mConfig.unit()) {
        lastTableSetting.title = mConfig.title();
        lastTableSetting.unit = mConfig.unit();

        os << std::endl
           << "| relative |" << std::setw(20) << std::right << ("ns/" + mConfig.unit()) << " |" << std::setw(20) << std::right
           << (mConfig.unit() + "/s") << " |   MdAPE | " << mConfig.title() << std::endl
           << "|---------:|--------------------:|--------------------:|--------:|:----------------------------------------------"
           << std::endl;
    }

    // we want output that looks like this:
    // |  1208.4% |               14.15 |       70,649,422.38 |    0.3% | `std::vector<std::string> emplace + release`

    // 1st column: relative
    os << '|';
    if (mConfig.relative().median() <= std::chrono::duration<double>::zero()) {
        // relative not set or invalid, print blank column
        os << "          |";
    } else {
        os << detail::fmt::Number(8, 1, mConfig.relative().median() / result.median() * 100) << "% |";
    }

    // 2nd column: ns/unit
    os << detail::fmt::Number(20, 2, 1e9 * result.median().count()) << " |";

    // 3rd column: unit/s
    os << detail::fmt::Number(20, 2, 1 / result.median().count()) << " |";

    // 4th column: MdAPE
    os << detail::fmt::Number(7, 1, result.medianAbsolutePercentError() * 100) << "% |";

    // 5th column: possible symbols, possibly errormessage, benchmark name
    if (result.medianAbsolutePercentError() >= 0.05) {
        // >=5%
        os << " :wavy_dash:";
    }
    os << ' ' << detail::fmt::MarkDownCode(name) << std::endl;

    return result;
}

// formatting utilities
namespace fmt {

// adds thousands separator to numbers
NumSep::NumSep(char sep)
    : mSep(sep) {}

char NumSep::do_thousands_sep() const {
    return mSep;
}

std::string NumSep::do_grouping() const {
    return "\003";
}

// RAII to save & restore a stream's state
StreamStateRestorer::StreamStateRestorer(std::ostream& s)
    : mStream(s)
    , mLocale(s.getloc())
    , mPrecision(s.precision())
    , mWidth(s.width())
    , mFill(s.fill())
    , mFmtFlags(s.flags()) {}

StreamStateRestorer::~StreamStateRestorer() {
    restore();
}

// sets back all stream info that we remembered at construction
void StreamStateRestorer::restore() {
    mStream.imbue(mLocale);
    mStream.precision(mPrecision);
    mStream.width(mWidth);
    mStream.fill(mFill);
    mStream.flags(mFmtFlags);
}

Number::Number(int width, int precision, double value)
    : mWidth(width)
    , mPrecision(precision)
    , mValue(value) {}

std::ostream& Number::write(std::ostream& os) const {
    StreamStateRestorer restorer(os);
    os.imbue(std::locale(os.getloc(), new NumSep(',')));
    os << std::setw(mWidth) << std::setprecision(mPrecision) << std::fixed << mValue;
    return os;
}

inline std::ostream& operator<<(std::ostream& os, Number const& n) {
    return n.write(os);
}

// Formats any text as markdown inline code, escaping backticks.
MarkDownCode::MarkDownCode(std::string what)
    : mWhat(std::move(what)) {}

std::ostream& MarkDownCode::write(std::ostream& os) const {
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

inline std::ostream& operator<<(std::ostream& os, MarkDownCode const& mdCode) {
    return mdCode.write(os);
}

} // namespace fmt
} // namespace detail

// Makes sure none of the given arguments are optimized away by the compiler.
template <typename... Args>
void doNotOptimizeAway(Args&&... args) {
    (void)std::initializer_list<int>{(detail::doNotOptimizeAway(std::forward<Args>(args)), 0)...};
}

// Result returned after a benchmark has finished. Can be used as a baseline for relative().
Result::Result(std::string const& unit, std::vector<std::chrono::duration<double>> secPerUnit) noexcept
    : mUnit(unit) {
    std::sort(secPerUnit.begin(), secPerUnit.end());
    mMinimum = secPerUnit.front();
    mMaximum = secPerUnit.back();
    mMedian = calcMedian(secPerUnit);
    mMedianAbsolutePercentError = calcMedianAbsolutePercentageError(secPerUnit, mMedian);
}

Result::Result() = default;

std::string const& Result::unit() const noexcept {
    return mUnit;
}

std::chrono::duration<double> Result::median() const noexcept {
    return mMedian;
}

double Result::medianAbsolutePercentError() const noexcept {
    return mMedianAbsolutePercentError;
}

std::chrono::duration<double> Result::minimum() const noexcept {
    return mMinimum;
}

std::chrono::duration<double> Result::maximum() const noexcept {
    return mMaximum;
}

// Convenience: makes sure none of the given arguments are optimized away by the compiler.
template <typename... Args>
Result& Result::doNotOptimizeAway(Args&&... args) {
    ::ankerl::nanobench::doNotOptimizeAway(std::forward<Args>(args)...);
    return *this;
}

// calculates median, and properly handles even number of elements too.
template <typename T>
T Result::calcMedian(std::vector<T> const& sortedResults) {
    auto mid = sortedResults.size() / 2;
    if (sortedResults.size() & 1) {
        return sortedResults[mid];
    }
    return (sortedResults[mid - 1] + sortedResults[mid]) / 2;
}

// calculates MdAPE which is the median of percentage error
// see https://www.spiderfinancial.com/support/documentation/numxl/reference-manual/forecasting-performance/mdape
double Result::calcMedianAbsolutePercentageError(std::vector<std::chrono::duration<double>> const& results,
                                                 std::chrono::duration<double> median) {
    std::vector<double> absolutePercentageErrors;
    for (auto r : results) {
        auto percent = (r - median) / r;
        if (percent < 0) {
            percent = -percent;
        }
        absolutePercentageErrors.push_back(percent);
    }
    std::sort(absolutePercentageErrors.begin(), absolutePercentageErrors.end());
    return calcMedian(absolutePercentageErrors);
}

// Configuration of a microbenchmark.
Config::Config() = default;

Config::Config(Config&&) = default;
Config& Config::operator=(Config&&) = default;
Config::Config(Config const&) = default;
Config& Config::operator=(Config const&) = default;

// Set the batch size, e.g. number of processed bytes, or some other metric for the size of the processed data in each iteration.
// Any argument is cast to double.
template <typename T>
Config& Config::batch(T b) noexcept {
    mBatch = static_cast<double>(b);
    return *this;
}
double Config::batch() const noexcept {
    return mBatch;
}

// Set a baseline to compare it to. 100% it is exactly as fast as the baseline, >100% means it is faster than the baseline, <100%
// means it is slower than the baseline.
Config& Config::relative(Result const& baseline) noexcept {
    mRelative = baseline;
    return *this;
}
Result const& Config::relative() const noexcept {
    return mRelative;
}

// Operation unit. Defaults to "op", could be e.g. "byte" for string processing.
// Use singular (byte, not bytes).
Config& Config::unit(std::string unit) {
    mUnit = std::move(unit);
    return *this;
}
std::string const& Config::unit() const noexcept {
    return mUnit;
}

Config& Config::title(std::string benchmarkTitle) {
    mBenchmarkTitle = std::move(benchmarkTitle);
    return *this;
}
std::string const& Config::title() const noexcept {
    return mBenchmarkTitle;
}

// Number of epochs to evaluate. The reported result will be the median of evaluation of each epoch.
Config& Config::epochs(size_t numEpochs) noexcept {
    mNumEpochs = numEpochs;
    return *this;
}
size_t Config::epochs() const noexcept {
    return mNumEpochs;
}

// Desired evaluation time is a multiple of clock resolution. Default is to be 1000 times above this measurement precision.
Config& Config::clockResolutionMultiple(size_t multiple) noexcept {
    mClockResolutionMultiple = multiple;
    return *this;
}
size_t Config::clockResolutionMultiple() const noexcept {
    return mClockResolutionMultiple;
}

// Sets the maximum time each epoch should take. Default is 100ms.
Config& Config::maxEpochTime(std::chrono::nanoseconds t) noexcept {
    mMaxEpochTime = t;
    return *this;
}
std::chrono::nanoseconds Config::maxEpochTime() const noexcept {
    return mMaxEpochTime;
}

// Where to write the output to? Defaults to std::cout
Config& Config::out(std::ostream& out) noexcept {
    mOut = &out;
    return *this;
}
std::ostream& Config::out() const noexcept {
    return *mOut;
}

Config& Config::warmup(size_t numWarmupIters) noexcept {
    mWarmup = numWarmupIters;
    return *this;
}
size_t Config::warmup() const noexcept {
    return mWarmup;
}

// Performs all evaluations.
template <typename Op>
Result Config::run(std::string name, Op op) const {
    // It is important that this method is kept short so the compiler can do better optimizations/ inlining of op()
    detail::Measurements measurements(*this, name);

    while (auto n = measurements.numIters()) {
        Clock::time_point before = Clock::now();
        while (n-- > 0) {
            op();
        }
        Clock::time_point after = Clock::now();
        measurements.add(after - before);
    }
    return measurements.result();
}

// Small Fast Counting RNG, version 4
constexpr uint64_t(Rng::min)() {
    return 0;
}

constexpr uint64_t(Rng::max)() {
    return (std::numeric_limits<uint64_t>::max)();
}

constexpr Rng::Rng()
    : Rng(UINT64_C(0xd3b45fd780a1b6a3)) {}

constexpr Rng::Rng(uint64_t seed) noexcept
    : mA(seed)
    , mB(seed)
    , mC(seed)
    , mCounter(1) {
    for (size_t i = 0; i < 12; ++i) {
        operator()();
    }
}

constexpr Rng::state_type Rng::state() const noexcept {
    return {mA, mB, mC, mCounter};
}

constexpr void Rng::state(state_type const& state) noexcept {
    mA = state[0];
    mB = state[1];
    mC = state[2];
    mCounter = state[3];
}

constexpr uint64_t Rng::operator()() noexcept {
    uint64_t tmp = mA + mB + mCounter++;
    mA = mB ^ (mB >> 11);
    mB = mC + (mC << 3);
    mC = rotl(mC, 24) + tmp;
    return tmp;
}

constexpr uint64_t Rng::rotl(uint64_t const x, int k) noexcept {
    return (x << k) | (x >> (64 - k));
}

// convenience helper to directly call Config().run(...)
template <typename Op>
Result run(std::string name, Op op) noexcept {
    return Config().run(std::move(name), op);
}

inline void forceTableHeader() {
    // reset info, so next time header will be printed
    detail::singletonLastTableSetting() = detail::TableInfo{};
}

} // namespace nanobench
} // namespace ankerl

#endif
