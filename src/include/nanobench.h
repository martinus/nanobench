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
#define ANKERL_NANOBENCH_VERSION_MAJOR 3 // incompatible API changes
#define ANKERL_NANOBENCH_VERSION_MINOR 0 // backwards-compatible changes
#define ANKERL_NANOBENCH_VERSION_PATCH 0 // backwards-compatible bug fixes

///////////////////////////////////////////////////////////////////////////////////////////////////
// public facing api - as minimal as possible
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <chrono>           // high_resolution_clock
#include <cstring>          // memcpy
#include <initializer_list> // for doNotOptimizeAway
#include <iosfwd>           // for std::ostream* custom output target in Config
#include <string>           // all names
#include <vector>           // holds all results

#define ANKERL_NANOBENCH(x) ANKERL_NANOBENCH_PRIVATE_##x()

#define ANKERL_NANOBENCH_PRIVATE_CXX() __cplusplus
#define ANKERL_NANOBENCH_PRIVATE_CXX98() 199711L
#define ANKERL_NANOBENCH_PRIVATE_CXX11() 201103L
#define ANKERL_NANOBENCH_PRIVATE_CXX14() 201402L
#define ANKERL_NANOBENCH_PRIVATE_CXX17() 201703L

#if ANKERL_NANOBENCH(CXX) >= ANKERL_NANOBENCH(CXX17)
#    define ANKERL_NANOBENCH_PRIVATE_NODISCARD() [[nodiscard]]
#else
#    define ANKERL_NANOBENCH_PRIVATE_NODISCARD()
#endif

#if defined(__clang__)
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_PADDED_PUSH() \
        _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wpadded\"")
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_PADDED_POP() _Pragma("clang diagnostic pop")
#else
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_PADDED_PUSH()
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_PADDED_POP()
#endif

#ifdef ANKERL_NANOBENCH_LOG_ENABLED
#    include <iostream>
#    define ANKERL_NANOBENCH_LOG(x) std::cout << __FUNCTION__ << "@" << __LINE__ << ": " << x << std::endl
#else
#    define ANKERL_NANOBENCH_LOG(x)
#endif

#if defined(__linux__)
#    define ANKERL_NANOBENCH_PRIVATE_PERF_COUNTERS() 1
#else
#    define ANKERL_NANOBENCH_PRIVATE_PERF_COUNTERS() 0
#endif

#if defined(__clang__)
#    define ANKERL_NANOBENCH_NO_SANITIZE(...) __attribute__((no_sanitize(__VA_ARGS__)))
#else
#    define ANKERL_NANOBENCH_NO_SANITIZE(...)
#endif

// declarations ///////////////////////////////////////////////////////////////////////////////////

namespace ankerl {
namespace nanobench {

using Clock = std::chrono::high_resolution_clock;
class Config;
class Measurement;
class Result;
class Rng;

// Contains mustache-like templates
namespace templates {

// CSV file from the benchmark results.
char const* csv() noexcept;

// HTML graphic using plotly.js
char const* htmlBoxplot() noexcept;

// JSON that contains all result data
char const* json() noexcept;

} // namespace templates

namespace detail {

template <typename T>
struct PerfCountSet;

class IterationLogic;
class PerformanceCounters;

#if ANKERL_NANOBENCH(PERF_COUNTERS)
class LinuxPerformanceCounters;
#endif

} // namespace detail
} // namespace nanobench
} // namespace ankerl

// definitions ////////////////////////////////////////////////////////////////////////////////////

namespace ankerl {
namespace nanobench {
namespace detail {

template <typename T>
struct PerfCountSet {
    T pageFaults{};
    T cpuCycles{};
    T contextSwitches{};
    T instructions{};
    T branchInstructions{};
    T branchMisses{};
};

} // namespace detail

// Holds measurement results of one epoch of a benchmark.
class Measurement {
public:
    Measurement(Clock::duration totalElapsed, uint64_t iters, double batch, detail::PerformanceCounters const& pc) noexcept;

    // sortable fastest to slowest
    ANKERL_NANOBENCH(NODISCARD) bool operator<(Measurement const& other) const noexcept;
    ANKERL_NANOBENCH(NODISCARD) Clock::duration const& elapsed() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) uint64_t numIters() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) std::chrono::duration<double> secPerUnit() const noexcept;

    ANKERL_NANOBENCH(NODISCARD) uint64_t pageFaults() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) uint64_t cpuCycles() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) uint64_t contextSwitches() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) uint64_t instructions() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) uint64_t branchInstructions() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) uint64_t branchMisses() const noexcept;

private:
    Clock::duration mTotalElapsed;
    uint64_t mNumIters;
    std::chrono::duration<double> mSecPerUnit;
    detail::PerfCountSet<uint64_t> mVal;
};

// Result returned after a benchmark has finished. Can be used as a baseline for relative().
ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
class Result {
public:
    Result(std::string benchmarkName, std::vector<Measurement> measurements, double batch) noexcept;
    Result() noexcept;

    ANKERL_NANOBENCH(NODISCARD) std::string const& name() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) std::vector<Measurement> const& sortedMeasurements() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) std::chrono::duration<double> median() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) double medianAbsolutePercentError() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) std::chrono::duration<double> minimum() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) std::chrono::duration<double> maximum() const noexcept;

    ANKERL_NANOBENCH(NODISCARD) double medianCpuCyclesPerUnit() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) bool hasMedianCpuCyclesPerUnit() const noexcept;

    ANKERL_NANOBENCH(NODISCARD) double medianInstructionsPerUnit() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) bool hasMedianInstructionsPerUnit() const noexcept;

    ANKERL_NANOBENCH(NODISCARD) double medianBranchesPerUnit() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) bool hasMedianBranchesPerUnit() const noexcept;

    ANKERL_NANOBENCH(NODISCARD) double medianBranchMissesPerUnit() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) bool hasMedianBranchMissesPerUnit() const noexcept;

    ANKERL_NANOBENCH(NODISCARD) Clock::duration total() const noexcept;

    ANKERL_NANOBENCH(NODISCARD) bool empty() const noexcept;

private:
    std::string mName{};
    std::vector<Measurement> mSortedMeasurements{};
    double mMedianAbsolutePercentError{};

    double mMedianCpuCyclesPerUnit{};
    double mMedianInstructionsPerUnit{};
    double mMedianBranchesPerUnit{};
    double mMedianBranchMissesPerUnit{};

    detail::PerfCountSet<bool> mHas{};
};
ANKERL_NANOBENCH(IGNORE_PADDED_POP)

// Sfc64, V4 - Small Fast Counting RNG, version 4
// Based on code from http://pracrand.sourceforge.net
class Rng final {
public:
    using result_type = uint64_t;

    static constexpr uint64_t(min)();
    static constexpr uint64_t(max)();

    Rng();

    // don't allow copying, it's dangerous
    Rng(Rng const&) = delete;
    Rng& operator=(Rng const&) = delete;

    // moving is ok
    Rng(Rng&&) noexcept = default;
    Rng& operator=(Rng&&) noexcept = default;
    ~Rng() noexcept = default;

    explicit Rng(uint64_t seed) noexcept;
    ANKERL_NANOBENCH(NODISCARD) Rng copy() const noexcept;
    void assign(Rng const& other) noexcept;

    // that one's inline so it is fast
    inline uint64_t operator()() noexcept;

    // random double in range [0, 1(
    inline double uniform01() noexcept;

private:
    static constexpr uint64_t rotl(uint64_t x, unsigned k) noexcept;

    uint64_t mA;
    uint64_t mB;
    uint64_t mC;
    uint64_t mCounter;
};

// Configuration of a microbenchmark.
ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
class Config {
public:
    Config();

    Config(Config&& other);
    Config& operator=(Config&& other);
    Config(Config const& other);
    Config& operator=(Config const& other);

    ~Config() noexcept;

    // Set the batch size, e.g. number of processed bytes, or some other metric for the size of the processed data in each iteration.
    // Best used in combination with `unit`. Any argument is cast to double.
    template <typename T>
    Config& batch(T b) noexcept;
    ANKERL_NANOBENCH(NODISCARD) double batch() const noexcept;

    // Marks the next run as the baseline. The following runs will be compared to this run. 100% will mean it is exactly as fast as the
    // baseline, >100% means it is faster than the baseline. It is calculated by `100% * runtime_baseline / runtime`. So e.g. 200%
    // means the current run is twice as fast as the baseline.
    Config& relative(bool isRelativeEnabled) noexcept;
    ANKERL_NANOBENCH(NODISCARD) bool relative() const noexcept;

    Config& performanceCounters(bool showPerformanceCounters) noexcept;
    ANKERL_NANOBENCH(NODISCARD) bool performanceCounters() const noexcept;

    // Operation unit. Defaults to "op", could be e.g. "byte" for string processing. This is used for the table header, e.g. to show
    // `ns/byte`. Use singular (byte, not bytes).
    Config& unit(std::string unit);
    ANKERL_NANOBENCH(NODISCARD) std::string const& unit() const noexcept;

    // Title of the benchmark, will be shown in the table header.
    Config& title(std::string benchmarkTitle);
    ANKERL_NANOBENCH(NODISCARD) std::string const& title() const noexcept;

    // Set the output stream where the resulting markdown table will be printed to. The default is `&std::cout`. You can disable all
    // output by setting `nullptr`.
    Config& output(std::ostream* outstream) noexcept;
    ANKERL_NANOBENCH(NODISCARD) std::ostream* output() const noexcept;

    // Number of epochs to evaluate. The reported result will be the median of evaluation of each epoch. Defaults to 11. The higher you
    // choose this, the more deterministic will the result be and outliers will be more easily removed. The default is already quite
    // high to be able to filter most outliers.
    //
    // For slow benchmarks you might want to reduce this number.
    Config& epochs(size_t numEpochs) noexcept;
    ANKERL_NANOBENCH(NODISCARD) size_t epochs() const noexcept;

    // Modern processors have a very accurate clock, being able to measure as low as 20 nanoseconds. This allows nanobech to be so
    // fast: we only run the benchmark sufficiently often so that the clock's accuracy is good enough. The default is to run one epoch
    // for 2000 times the clock resolution. So for 20ns resolution and 11 epochs, this gives a total runtime of `20ns * 8000 * 11 ~
    // 2ms` for a benchmark to get accurate results.
    Config& clockResolutionMultiple(size_t multiple) noexcept;
    ANKERL_NANOBENCH(NODISCARD) size_t clockResolutionMultiple() const noexcept;

    // As a safety precausion if the clock is not very accurate, we can set an upper limit for the maximum evaluation time per epoch.
    // Default is 100ms.
    Config& maxEpochTime(std::chrono::nanoseconds t) noexcept;
    ANKERL_NANOBENCH(NODISCARD) std::chrono::nanoseconds maxEpochTime() const noexcept;

    // Sets the minimum time each epoch should take. Default is zero, so clockResolutionMultiple() can do it's best guess. You can
    // increase this if you have the time and results are not accurate enough.
    Config& minEpochTime(std::chrono::nanoseconds t) noexcept;
    ANKERL_NANOBENCH(NODISCARD) std::chrono::nanoseconds minEpochTime() const noexcept;

    // Sets the minimum number of iterations each epoch should take. Default is 1. For high median average percentage error (MdAPE),
    // which happens when your benchmark is unstable, you might want to increase the minimum number to get more accurate reslts.
    Config& minEpochIterations(uint64_t numIters) noexcept;
    ANKERL_NANOBENCH(NODISCARD) uint64_t minEpochIterations() const noexcept;

    // Set a number of iterations that are initially performed without any measurements, to warmup caches / database / whatever.
    // Normally this is not needed, since we show the median result so initial outliers will be filtered away automatically.
    Config& warmup(uint64_t numWarmupIters) noexcept;
    ANKERL_NANOBENCH(NODISCARD) uint64_t warmup() const noexcept;

    // Gets all benchmark results
    ANKERL_NANOBENCH(NODISCARD) std::vector<Result> const& results() const noexcept;

    // Repeatedly calls op() based on the configuration, and performs measurements.
    template <typename Op>
    Config& run(std::string const& name, Op op);

    // Convenience: makes sure none of the given arguments are optimized away by the compiler.
    template <typename... Args>
    Config& doNotOptimizeAway(Args&&... args);

    // Parses the mustache-like template and renders the output into os.
    Config& render(char const* templateContent, std::ostream& os);

private:
    std::string mBenchmarkTitle = "benchmark";
    std::string mUnit = "op";
    double mBatch = 1.0;
    size_t mNumEpochs = 11;
    size_t mClockResolutionMultiple = static_cast<size_t>(1000);
    std::chrono::nanoseconds mMaxEpochTime = std::chrono::milliseconds(100);
    std::chrono::nanoseconds mMinEpochTime{};
    uint64_t mMinEpochIterations{1};
    uint64_t mWarmup = 0;
    std::vector<Result> mResults{};
    std::ostream* mOut = nullptr;
    bool mIsRelative = false;
    bool mShowPerformanceCounters = true;
};
ANKERL_NANOBENCH(IGNORE_PADDED_POP)

// Makes sure none of the given arguments are optimized away by the compiler.
template <typename... Args>
void doNotOptimizeAway(Args&&... args);

namespace detail {

#if defined(_MSC_VER)
void doNotOptimizeAwaySink(void const*);
#else
template <typename T>
void doNotOptimizeAway(T& value);
#endif

template <typename T>
void doNotOptimizeAway(T const& val);

// internally used, but visible because run() is templated
ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
class IterationLogic {
public:
    IterationLogic(Config const& config, std::string name) noexcept;

    ANKERL_NANOBENCH(NODISCARD) uint64_t numIters() const noexcept;
    void add(std::chrono::nanoseconds elapsed, PerformanceCounters const& pc) noexcept;
    ANKERL_NANOBENCH(NODISCARD) Result& result();

private:
    enum class State { warmup, upscaling_runtime, measuring, endless };

    ANKERL_NANOBENCH(NODISCARD) Result showResult(std::string const& errorMessage) const;
    ANKERL_NANOBENCH(NODISCARD) bool isCloseEnoughForMeasurements(std::chrono::nanoseconds elapsed) const noexcept;
    ANKERL_NANOBENCH(NODISCARD) uint64_t calcBestNumIters(std::chrono::nanoseconds elapsed, uint64_t iters) noexcept;
    void upscale(std::chrono::nanoseconds elapsed);

    uint64_t mNumIters = 1;
    Config const& mConfig;
    std::chrono::nanoseconds mTargetRuntimePerEpoch{};
    std::string mName;
    Result mResult{};
    std::vector<Measurement> mMeasurements{};
    Rng mRng{};
    std::chrono::nanoseconds mTotalElapsed{};
    uint64_t mTotalNumIters = 0;

    State mState = State::upscaling_runtime;
};
ANKERL_NANOBENCH(IGNORE_PADDED_POP)

ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
class PerformanceCounters {
public:
    PerformanceCounters(PerformanceCounters const&) = delete;
    PerformanceCounters& operator=(PerformanceCounters const&) = delete;

    PerformanceCounters();
    ~PerformanceCounters();

    void beginMeasure();
    void endMeasure();
    void updateResults(uint64_t numIters);

    ANKERL_NANOBENCH(NODISCARD) PerfCountSet<uint64_t> const& val() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) PerfCountSet<bool> const& has() const noexcept;

private:
#if ANKERL_NANOBENCH(PERF_COUNTERS)
    LinuxPerformanceCounters* mPc = nullptr;
#endif
    PerfCountSet<uint64_t> mVal;
    PerfCountSet<bool> mHas;
};
ANKERL_NANOBENCH(IGNORE_PADDED_POP)

// Gets the singleton
PerformanceCounters& performanceCounters();

} // namespace detail
} // namespace nanobench
} // namespace ankerl

// implementation /////////////////////////////////////////////////////////////////////////////////

namespace ankerl {
namespace nanobench {

// Small Fast Counting RNG, version 4
constexpr uint64_t(Rng::min)() {
    return 0;
}

constexpr uint64_t(Rng::max)() {
    return (std::numeric_limits<uint64_t>::max)();
}

// Mark this as no_sanitize, otherwise UBSAN will say we got an unsigned integer overflow. Which is not a undefined behavior, but often
// a bug. Not here though.
ANKERL_NANOBENCH_NO_SANITIZE("integer")
uint64_t Rng::operator()() noexcept {
    uint64_t tmp = mA + mB + mCounter++;
    mA = mB ^ (mB >> 11U);
    mB = mC + (mC << 3U);
    mC = rotl(mC, 24U) + tmp;
    return tmp;
}

// see http://prng.di.unimi.it/
double Rng::uniform01() noexcept {
    auto i = (UINT64_C(0x3ff) << 52U) | (operator()() >> 12U);
    // can't use union in c++ here for type puning, it's undefined behavior.
    // std::memcpy is optimized away anyways.
    double d;
    std::memcpy(&d, &i, sizeof(double));
    return d - 1.0;
}

constexpr uint64_t Rng::rotl(uint64_t x, unsigned k) noexcept {
    return (x << k) | (x >> (64U - k));
}

// Performs all evaluations.
template <typename Op>
ANKERL_NANOBENCH_NO_SANITIZE("integer")
Config& Config::run(std::string const& name, Op op) {
    // It is important that this method is kept short so the compiler can do better optimizations/ inlining of op()
    detail::IterationLogic iterationLogic(*this, name);
    auto& pc = detail::performanceCounters();

    while (auto n = iterationLogic.numIters()) {
        pc.beginMeasure();
        Clock::time_point before = Clock::now();
        while (n-- > 0) {
            op();
        }
        Clock::time_point after = Clock::now();
        pc.endMeasure();
        pc.updateResults(iterationLogic.numIters());
        iterationLogic.add(after - before, pc);
    }
    mResults.emplace_back(std::move(iterationLogic.result()));
    return *this;
}

// Set the batch size, e.g. number of processed bytes, or some other metric for the size of the processed data in each iteration.
// Any argument is cast to double.
template <typename T>
Config& Config::batch(T b) noexcept {
    mBatch = static_cast<double>(b);
    return *this;
}

// Convenience: makes sure none of the given arguments are optimized away by the compiler.
template <typename... Args>
Config& Config::doNotOptimizeAway(Args&&... args) {
    (void)std::initializer_list<int>{(detail::doNotOptimizeAway(std::forward<Args>(args)), 0)...};
    return *this;
}

// Makes sure none of the given arguments are optimized away by the compiler.
template <typename... Args>
void doNotOptimizeAway(Args&&... args) {
    (void)std::initializer_list<int>{(detail::doNotOptimizeAway(std::forward<Args>(args)), 0)...};
}

namespace detail {

#if defined(_MSC_VER)
template <typename T>
void doNotOptimizeAway(T const& val) {
    doNotOptimizeAwaySink(&val);
}

#else

template <typename T>
void doNotOptimizeAway(T const& val) {
    // NOLINTNEXTLINE(hicpp-no-assembler)
    asm volatile("" : : "r,m"(val) : "memory");
}

template <typename T>
void doNotOptimizeAway(T& value) {
#    if defined(__clang__)
    // NOLINTNEXTLINE(hicpp-no-assembler)
    asm volatile("" : "+r,m"(value) : : "memory");
#    else
    // NOLINTNEXTLINE(hicpp-no-assembler)
    asm volatile("" : "+m,r"(value) : : "memory");
#    endif
}
#endif

} // namespace detail
} // namespace nanobench
} // namespace ankerl

#ifdef ANKERL_NANOBENCH_IMPLEMENT

///////////////////////////////////////////////////////////////////////////////////////////////////
// implementation part
///////////////////////////////////////////////////////////////////////////////////////////////////

#    include <algorithm> // sort
#    include <atomic>    // compare_exchange_strong in loop overhead
#    include <cstdlib>   // getenv
#    include <cstring>   // strstr, strncmp
#    include <fstream>   // ifstream to parse proc files
#    include <iomanip>   // setw, setprecision
#    include <iostream>  // cout
#    include <random>    // random_device
#    include <sstream>   // to_s in Number
#    include <stdexcept> // throw for rendering templates
#    include <vector>    // manage results
#    if defined(__linux__)
#        include <unistd.h> //sysconf
#    endif
#    if ANKERL_NANOBENCH(PERF_COUNTERS)
#        include <map> // map

#        include <linux/perf_event.h>
#        include <sys/ioctl.h>
#        include <sys/syscall.h>
#        include <unistd.h>
#    endif

// declarations ///////////////////////////////////////////////////////////////////////////////////

namespace ankerl {
namespace nanobench {

// helper stuff that only intended to be used internally
namespace detail {

struct TableInfo;

// formatting utilities
namespace fmt {

class NumSep;
class StreamStateRestorer;
class Number;
class MarkDownColumn;
class MarkDownCode;

} // namespace fmt
} // namespace detail
} // namespace nanobench
} // namespace ankerl

// definitions ////////////////////////////////////////////////////////////////////////////////////

namespace ankerl {
namespace nanobench {
namespace templates {
char const* csv() noexcept {
    return R"DELIM({{title}}; "relative %"; "s/{{unit}}"; "min/{{unit}}"; "max/{{unit}}"; "error %"; "measurements"; "instructions/{{unit}}"; "branch/{{unit}}"; "branch misses/{{unit}}"
{{#benchmarks}}"{{name}}"; {{relative}}; {{median_sec_per_unit}}; {{min}}; {{max}}; {{md_ape}}; {{num_measurements}}; {{median_ins_per_unit}}; {{median_branches_per_unit}}; {{median_branchmisses_per_unit}}
{{/benchmarks}})DELIM";
}
//
char const* htmlBoxplot() noexcept {
    return R"DELIM(<html>

<head>
    <script src="https://cdn.plot.ly/plotly-latest.min.js"></script>
</head>

<body>
    <div id="myDiv" style="width:1024px; height:768px"></div>
    <script>
        var data = [
            {{#benchmarks}}{
                name: '{{name}}',
                y: [{{#results}}{{elapsed_ns}}e-9/{{iters}}{{^-last}}, {{/last}}{{/results}}],
            },
            {{/benchmarks}}
        ];
        var title = '{{title}}';

        data = data.map(a => Object.assign(a, { boxpoints: 'all', pointpos: 0, type: 'box' }));
        var layout = { title: { text: title }, showlegend: false, yaxis: { title: 'time per {{unit}}', rangemode: 'tozero', autorange: true } }; Plotly.newPlot('myDiv', data, layout, {responsive: true});
    </script>
</body>

</html>)DELIM";
}

char const* json() noexcept {
    return R"DELIM({
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
{{#results}}    { "sec_per_unit": {{sec_per_unit}}, "iters": {{iters}}, "elapsed_ns": {{elapsed_ns}}, "pagefaults": {{pagefaults}}, "cpucycles": {{cpucycles}}, "contextswitches": {{contextswitches}}, "instructions": {{instructions}}, "branchinstructions": {{branchinstructions}}, "branchmisses": {{branchmisses}}}{{^-last}}, {{/-last}}
{{/results}}   ]
  }{{^-last}},{{/-last}}
{{/benchmarks}} ]
}
)DELIM";
}

} // namespace templates

// helper stuff that only intended to be used internally
namespace detail {

char const* getEnv(char const* name);
bool isEndlessRunning(std::string const& name);

template <typename T>
T parseFile(std::string const& filename);

void gatherStabilityInformation(std::vector<std::string>& warnings, std::vector<std::string>& recommendations);
void printStabilityInformationOnce();

// remembers the last table settings used. When it changes, a new table header is automatically written for the new entry.
uint64_t& singletonLastTableSettingsHash() noexcept;
uint64_t calcTableSettingsHash(std::string const& unit, std::string const& title) noexcept;

// determines resolution of the given clock. This is done by measuring multiple times and returning the minimum time difference.
Clock::duration calcClockResolution(size_t numEvaluations) noexcept;

// Calculates clock resolution once, and remembers the result
inline Clock::duration clockResolution() noexcept;

// formatting utilities
namespace fmt {

// adds thousands separator to numbers
ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
class NumSep : public std::numpunct<char> {
public:
    explicit NumSep(char sep);
    char do_thousands_sep() const override;
    std::string do_grouping() const override;

private:
    char mSep;
};
ANKERL_NANOBENCH(IGNORE_PADDED_POP)

// RAII to save & restore a stream's state
ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
class StreamStateRestorer {
public:
    explicit StreamStateRestorer(std::ostream& s);
    ~StreamStateRestorer();

    // sets back all stream info that we remembered at construction
    void restore();

    // don't allow copying / moving
    StreamStateRestorer(StreamStateRestorer const&) = delete;
    StreamStateRestorer& operator=(StreamStateRestorer const&) = delete;
    StreamStateRestorer(StreamStateRestorer&&) = delete;
    StreamStateRestorer& operator=(StreamStateRestorer&&) = delete;

private:
    std::ostream& mStream;
    std::locale mLocale;
    std::streamsize const mPrecision;
    std::streamsize const mWidth;
    std::ostream::char_type const mFill;
    std::ostream::fmtflags const mFmtFlags;
};
ANKERL_NANOBENCH(IGNORE_PADDED_POP)

// Number formatter
class Number {
public:
    Number(int width, int precision, double value);
    Number(int width, int precision, int64_t value);
    std::string to_s() const;

private:
    friend std::ostream& operator<<(std::ostream& os, Number const& n);
    std::ostream& write(std::ostream& os) const;

    int mWidth;
    int mPrecision;
    double mValue;
};

std::ostream& operator<<(std::ostream& os, Number const& n);

class MarkDownColumn {
public:
    MarkDownColumn(int width, int precision, std::string const& title, std::string const& suffix, double value);
    std::string title() const;
    std::string separator() const;
    std::string invalid() const;
    std::string value() const;

private:
    int mWidth;
    int mPrecision;
    std::string mTitle;
    std::string mSuffix;
    double mValue;
};

// Formats any text as markdown code, escaping backticks.
class MarkDownCode {
public:
    explicit MarkDownCode(std::string const& what);

private:
    friend std::ostream& operator<<(std::ostream& os, MarkDownCode const& mdCode);
    std::ostream& write(std::ostream& os) const;

    std::string mWhat{};
};

std::ostream& operator<<(std::ostream& os, MarkDownCode const& mdCode);

} // namespace fmt
} // namespace detail
} // namespace nanobench
} // namespace ankerl

// implementation /////////////////////////////////////////////////////////////////////////////////

namespace ankerl {
namespace nanobench {
namespace detail {

PerformanceCounters& performanceCounters() {
#    if defined(__clang__)
#        pragma clang diagnostic push
#        pragma clang diagnostic ignored "-Wexit-time-destructors"
#    endif
    static PerformanceCounters pc;
#    if defined(__clang__)
#        pragma clang diagnostic pop
#    endif
    return pc;
}

// Windows version of do not optimize away
// see https://github.com/google/benchmark/blob/master/include/benchmark/benchmark.h#L307
// see https://github.com/facebook/folly/blob/master/folly/Benchmark.h#L280
// see https://docs.microsoft.com/en-us/cpp/preprocessor/optimize
#    if defined(_MSC_VER)
#        pragma optimize("", off)
void doNotOptimizeAwaySink(void const*) {}
#        pragma optimize("", on)
#    endif

template <typename T>
T parseFile(std::string const& filename) {
    std::ifstream fin(filename);
    T num{};
    fin >> num;
    return num;
}

char const* getEnv(char const* name) {
#    if defined(_MSC_VER)
#        pragma warning(push)
#        pragma warning(disable : 4996) // getenv': This function or variable may be unsafe.
#    endif
    return std::getenv(name);
#    if defined(_MSC_VER)
#        pragma warning(pop)
#    endif
}

bool isEndlessRunning(std::string const& name) {
    auto endless = getEnv("NANOBENCH_ENDLESS");
    return nullptr != endless && endless == name;
}

void gatherStabilityInformation(std::vector<std::string>& warnings, std::vector<std::string>& recommendations) {
    warnings.clear();
    recommendations.clear();

    bool recommendCheckFlags = false;
#    if !defined(NDEBUG)
    warnings.emplace_back("NDEBUG not defined, assert() macros are evaluated");
    recommendCheckFlags = true;
#    endif

#    if defined(DEBUG)
    warnings.emplace_back("DEBUG defined");
    recommendCheckFlags = true;
#    endif

    bool recommendPyPerf = false;
#    if defined(__linux__)
    auto nprocs = sysconf(_SC_NPROCESSORS_CONF);
    if (nprocs <= 0) {
        warnings.emplace_back("couldn't figure out number of processors - no governor, turbo check possible");
    } else {

        // check frequency scaling
        for (long id = 0; id < nprocs; ++id) {
            auto sysCpu = "/sys/devices/system/cpu/cpu" + std::to_string(id);
            auto minFreq = parseFile<int64_t>(sysCpu + "/cpufreq/scaling_min_freq");
            auto maxFreq = parseFile<int64_t>(sysCpu + "/cpufreq/scaling_max_freq");
            if (minFreq != maxFreq) {
                auto minMHz = static_cast<double>(minFreq) / 1000.0;
                auto maxMHz = static_cast<double>(maxFreq) / 1000.0;
                warnings.emplace_back("CPU frequency scaling enabled: CPU " + std::to_string(id) + " between " +
                                      detail::fmt::Number(1, 1, minMHz).to_s() + " and " + detail::fmt::Number(1, 1, maxMHz).to_s() +
                                      " MHz");
                recommendPyPerf = true;
                break;
            }
        }

        auto currentGovernor = parseFile<std::string>("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
        if ("performance" != currentGovernor) {
            warnings.emplace_back("CPU governor is '" + currentGovernor + "' but should be 'performance'");
            recommendPyPerf = true;
        }

        if (0 == parseFile<int>("/sys/devices/system/cpu/intel_pstate/no_turbo")) {
            warnings.emplace_back("Turbo is enabled, CPU frequency will fluctuate");
            recommendPyPerf = true;
        }
    }
#    endif

    if (recommendCheckFlags) {
        recommendations.emplace_back("Make sure you compile for Release");
    }
    if (recommendPyPerf) {
        recommendations.emplace_back("Use 'pyperf system tune' before benchmarking. See https://github.com/vstinner/pyperf");
    }
}

void printStabilityInformationOnce() {
    static bool shouldPrint = true;
    if (shouldPrint) {
        shouldPrint = false;
        std::vector<std::string> warnings;
        std::vector<std::string> recommendations;
        gatherStabilityInformation(warnings, recommendations);
        if (warnings.empty()) {
            return;
        }

        std::cerr << "Warning, results might be unstable:" << std::endl;
        for (auto const& w : warnings) {
            std::cerr << "* " << w << std::endl;
        }

        std::cerr << std::endl << "Recommendations" << std::endl;
        for (auto const& r : recommendations) {
            std::cerr << "* " << r << std::endl;
        }
    }
}

// remembers the last table settings used. When it changes, a new table header is automatically written for the new entry.
uint64_t& singletonLastTableSettingsHash() noexcept {
    static uint64_t sTableSettingHash = {};
    return sTableSettingHash;
}

ANKERL_NANOBENCH_NO_SANITIZE("integer")
inline uint64_t fnv1a(std::string const& str) noexcept {
    auto val = UINT64_C(14695981039346656037);
    for (auto c : str) {
        val = (val ^ static_cast<uint8_t>(c)) * UINT64_C(1099511628211);
    }
    return val;
}

ANKERL_NANOBENCH_NO_SANITIZE("integer")
inline void hash_combine(uint64_t* seed, uint64_t val) {
    *seed ^= val + UINT64_C(0x9e3779b9) + (*seed << 6U) + (*seed >> 2U);
}

inline uint64_t calcTableSettingsHash(Config const& cfg) noexcept {
    uint64_t h = 0;
    hash_combine(&h, fnv1a(cfg.unit()));
    hash_combine(&h, fnv1a(cfg.title()));
    return h;
}

// determines resolution of the given clock. This is done by measuring multiple times and returning the minimum time difference.
Clock::duration calcClockResolution(size_t numEvaluations) noexcept {
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
Clock::duration clockResolution() noexcept {
    static Clock::duration sResolution = calcClockResolution(20);
    return sResolution;
}

IterationLogic::IterationLogic(Config const& config, std::string name) noexcept
    : mConfig(config)
    , mName(std::move(name)) {
    printStabilityInformationOnce();

    // determine target runtime per epoch
    mTargetRuntimePerEpoch = detail::clockResolution() * mConfig.clockResolutionMultiple();
    if (mTargetRuntimePerEpoch > mConfig.maxEpochTime()) {
        mTargetRuntimePerEpoch = mConfig.maxEpochTime();
    }
    if (mTargetRuntimePerEpoch < mConfig.minEpochTime()) {
        mTargetRuntimePerEpoch = mConfig.minEpochTime();
    }

    // prepare array for measurement results
    mMeasurements.reserve(mConfig.epochs());

    if (isEndlessRunning(mName)) {
        std::cerr << "NANOBENCH_ENDLESS set: running '" << mName << "' endlessly" << std::endl;
        mNumIters = (std::numeric_limits<uint64_t>::max)();
        mState = State::endless;
    } else if (0 != mConfig.warmup()) {
        mNumIters = mConfig.warmup();
        mState = State::warmup;
    } else {
        mNumIters = mConfig.minEpochIterations();
        mState = State::upscaling_runtime;
    }
}

uint64_t IterationLogic::numIters() const noexcept {
    ANKERL_NANOBENCH_LOG(mName << ": mNumIters=" << mNumIters);
    return mNumIters;
}

bool IterationLogic::isCloseEnoughForMeasurements(std::chrono::nanoseconds elapsed) const noexcept {
    return elapsed * 3 >= mTargetRuntimePerEpoch * 2;
}

// directly calculates new iters based on elapsed&iters, and adds a 10% noise. Makes sure we don't underflow.
uint64_t IterationLogic::calcBestNumIters(std::chrono::nanoseconds elapsed, uint64_t iters) noexcept {
    auto doubleElapsed = std::chrono::duration_cast<std::chrono::duration<double>>(elapsed);
    auto doubleTargetRuntimePerEpoch = std::chrono::duration_cast<std::chrono::duration<double>>(mTargetRuntimePerEpoch);
    auto doubleNewIters = doubleTargetRuntimePerEpoch / doubleElapsed * static_cast<double>(iters);

    auto doubleMinEpochIters = static_cast<double>(mConfig.minEpochIterations());
    if (doubleNewIters < doubleMinEpochIters) {
        doubleNewIters = doubleMinEpochIters;
    }
    doubleNewIters *= 1.0 + 0.2 * mRng.uniform01();

    // +0.5 for correct rounding when casting
    // NOLINTNEXTLINE(bugprone-incorrect-roundings)
    return static_cast<uint64_t>(doubleNewIters + 0.5);
}

ANKERL_NANOBENCH_NO_SANITIZE("integer") void IterationLogic::upscale(std::chrono::nanoseconds elapsed) {
    if (elapsed * 10 < mTargetRuntimePerEpoch) {
        // we are far below the target runtime. Multiply iterations by 10 (with overflow check)
        if (mNumIters * 10 < mNumIters) {
            // overflow :-(
            mResult = showResult("iterations overflow. Maybe your code got optimized away?");
            mNumIters = 0;
            return;
        }
        mNumIters *= 10;
    } else {
        mNumIters = calcBestNumIters(elapsed, mNumIters);
    }
}

void IterationLogic::add(std::chrono::nanoseconds elapsed, PerformanceCounters const& pc) noexcept {
#    ifdef ANKERL_NANOBENCH_LOG_ENABLED
    auto oldIters = mNumIters;
#    endif

    switch (mState) {
    case State::warmup:
        if (isCloseEnoughForMeasurements(elapsed)) {
            // if elapsed is close enough, we can skip upscaling and go right to measurements
            // still, we don't add the result to the measurements.
            mState = State::measuring;
            mNumIters = calcBestNumIters(elapsed, mNumIters);
        } else {
            // not close enough: switch to upscaling
            mState = State::upscaling_runtime;
            upscale(elapsed);
        }
        break;

    case State::upscaling_runtime:
        if (isCloseEnoughForMeasurements(elapsed)) {
            // if we are close enough, add measurement and switch to always measuring
            mState = State::measuring;
            mTotalElapsed += elapsed;
            mTotalNumIters += mNumIters;
            mMeasurements.emplace_back(elapsed, mNumIters, mConfig.batch(), pc);
            mNumIters = calcBestNumIters(mTotalElapsed, mTotalNumIters);
        } else {
            upscale(elapsed);
        }
        break;

    case State::measuring:
        // just add measurements - no questions asked. Even when runtime is low. But we can't ignore
        // that fluctuation, or else we would bias the result
        mTotalElapsed += elapsed;
        mTotalNumIters += mNumIters;
        mMeasurements.emplace_back(elapsed, mNumIters, mConfig.batch(), pc);
        mNumIters = calcBestNumIters(mTotalElapsed, mTotalNumIters);
        break;

    case State::endless:
        mNumIters = (std::numeric_limits<uint64_t>::max)();
        break;
    }

    if (static_cast<uint64_t>(mMeasurements.size()) == mConfig.epochs()) {
        // we got all the results that we need, finish it
        mResult = showResult("");
        mNumIters = 0;
    }

    ANKERL_NANOBENCH_LOG(mName << ": " << detail::fmt::Number(20, 3, static_cast<double>(elapsed.count())) << " elapsed, "
                               << detail::fmt::Number(20, 3, static_cast<double>(mTargetRuntimePerEpoch.count()))
                               << " target. oldIters=" << oldIters << ", mNumIters=" << mNumIters
                               << ", mState=" << static_cast<int>(mState));
}

Result& IterationLogic::result() {
    return mResult;
}

Result IterationLogic::showResult(std::string const& errorMessage) const {
    ANKERL_NANOBENCH_LOG("mMeasurements.size()=" << mMeasurements.size());
    Result r;
    if (errorMessage.empty()) {
        r = Result(mName, mMeasurements, mConfig.batch());
    }

    if (mConfig.output() != nullptr) {
        // prepare column data ///////
        std::vector<fmt::MarkDownColumn> columns;

        if (mConfig.relative()) {
            double d = 100.0;
            if (!mConfig.results().empty()) {
                d = mConfig.results().front().median() / r.median() * 100;
            }
            columns.emplace_back(11, 1, "relative", "%", d);
        }

        columns.emplace_back(22, 2, "ns/" + mConfig.unit(), "", 1e9 * r.median().count());
        columns.emplace_back(22, 2, mConfig.unit() + "/s", "", 1.0 / r.median().count());
        columns.emplace_back(10, 1, "error %", "%", r.medianAbsolutePercentError() * 100);

        if (mConfig.performanceCounters()) {
            if (r.hasMedianInstructionsPerUnit()) {
                columns.emplace_back(18, 2, "ins/" + mConfig.unit(), "", r.medianInstructionsPerUnit());
            }
            if (r.hasMedianCpuCyclesPerUnit()) {
                columns.emplace_back(18, 2, "cyc/" + mConfig.unit(), "", r.medianCpuCyclesPerUnit());
            }
            if (r.hasMedianInstructionsPerUnit() && r.hasMedianCpuCyclesPerUnit()) {
                columns.emplace_back(9, 3, "IPC", "", r.medianInstructionsPerUnit() / r.medianCpuCyclesPerUnit());
            }
            if (r.hasMedianBranchesPerUnit()) {
                columns.emplace_back(17, 2, "branch/" + mConfig.unit(), "", r.medianBranchesPerUnit());
                if (r.hasMedianBranchMissesPerUnit()) {
                    double p = 0.0;
                    if (r.medianBranchesPerUnit() >= 1e-9) {
                        p = 100.0 * r.medianBranchMissesPerUnit() / r.medianBranchesPerUnit();
                    }
                    columns.emplace_back(10, 1, "miss %", "%", p);
                }
            }
        }

        columns.emplace_back(12, 2, "total sec", "", std::chrono::duration_cast<std::chrono::duration<double>>(r.total()).count());

        // write everything
        auto& os = *mConfig.output();

        auto h = calcTableSettingsHash(mConfig);
        if (h != singletonLastTableSettingsHash()) {
            singletonLastTableSettingsHash() = h;

            os << std::endl;
            for (auto const& col : columns) {
                os << col.title();
            }
            os << "| " << mConfig.title() << std::endl;

            for (auto const& col : columns) {
                os << col.separator();
            }
            os << "|:" << std::string(mConfig.title().size() + 1U, '-') << std::endl;
        }

        if (!errorMessage.empty()) {
            for (auto const& col : columns) {
                os << col.invalid();
            }
            os << "| :boom: " << fmt::MarkDownCode(mName) << " (" << errorMessage << ')' << std::endl;
        } else {
            for (auto const& col : columns) {
                os << col.value();
            }
            os << "| ";
            auto showUnstable = r.medianAbsolutePercentError() >= 0.05;
            if (showUnstable) {
                os << ":wavy_dash:";
            }
            os << mName;
            if (showUnstable) {
                auto avgIters = static_cast<double>(mTotalNumIters) / static_cast<double>(mConfig.epochs());
                // NOLINTNEXTLINE(bugprone-incorrect-roundings)
                auto suggestedIters = static_cast<uint64_t>(avgIters * 10 + 0.5);

                os << " (Unstable with ~" << detail::fmt::Number(1, 1, avgIters) << " iters. Increase `minEpochIterations` to e.g. "
                   << suggestedIters << ")";
            }
            os << std::endl;
        }
    }

    return r;
}

#    if ANKERL_NANOBENCH(PERF_COUNTERS)

ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
class LinuxPerformanceCounters {
public:
    struct Target {
        Target(uint64_t* targetValue_, bool correctMeasuringOverhead_, bool correctLoopOverhead_)
            : targetValue(targetValue_)
            , correctMeasuringOverhead(correctMeasuringOverhead_)
            , correctLoopOverhead(correctLoopOverhead_) {}

        uint64_t* targetValue{};
        bool correctMeasuringOverhead{};
        bool correctLoopOverhead{};
    };

    ~LinuxPerformanceCounters();

    // quick operation
    inline void start() {}

    inline void stop() {}

    bool monitor(perf_sw_ids swId, Target target);
    bool monitor(perf_hw_id hwId, Target target);

    bool hasError() const noexcept {
        return mHasError;
    }

    // Just reading data is faster than enable & disabling.
    // we subtract data ourselves.
    inline void beginMeasure() {
        if (mHasError) {
            return;
        }

        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        mHasError = -1 == ioctl(mFd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
        if (mHasError) {
            return;
        }

        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        mHasError = -1 == ioctl(mFd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    }

    inline void endMeasure() {
        if (mHasError) {
            return;
        }

        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        mHasError = (-1 == ioctl(mFd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP));
        if (mHasError) {
            return;
        }

        auto const numBytes = sizeof(uint64_t) * mCounters.size();
        auto ret = read(mFd, mCounters.data(), numBytes);
        mHasError = ret != static_cast<ssize_t>(numBytes);
    }

    void updateResults(uint64_t numIters);

    // rounded integer division
    template <typename T>
    static inline T divRounded(T a, T divisor) {
        return (a + divisor / 2) / divisor;
    }

    template <typename Op>
    ANKERL_NANOBENCH_NO_SANITIZE("integer")
    void calibrate(Op&& op) {
        // clear current calibration data,
        for (auto& v : mCalibratedOverhead) {
            v = UINT64_C(0);
        }

        // create new calibration data
        auto newCalibration = mCalibratedOverhead;
        for (auto& v : newCalibration) {
            v = (std::numeric_limits<uint64_t>::max)();
        }
        for (size_t iter = 0; iter < 100; ++iter) {
            beginMeasure();
            op();
            endMeasure();
            if (mHasError) {
                return;
            }

            for (size_t i = 0; i < newCalibration.size(); ++i) {
                auto diff = mCounters[i];
                if (newCalibration[i] > diff) {
                    newCalibration[i] = diff;
                }
            }
        }

        mCalibratedOverhead = std::move(newCalibration);

        {
            // calibrate loop overhead. For branches & instructions this makes sense, not so much for everything else like cycles.
            // with g++, atomic operation compiles exactly to one instruction. see https://godbolt.org/z/dEXYd1
            std::atomic<int> atomicVal(0);
            uint64_t const numIters = 100000U + (std::random_device{}() & 3);
            uint64_t n = numIters;
            int y = 123;

            auto fn = [&]() { atomicVal.compare_exchange_strong(y, 0); };

            beginMeasure();
            Clock::time_point before = Clock::now();
            while (n-- > 0) {
                fn();
            }
            Clock::time_point after = Clock::now();
            endMeasure();

            if ((after - before).count() == 0) {
                std::cerr << "could not calibrate loop overhead" << std::endl;
            }

            for (size_t i = 0; i < mCounters.size(); ++i) {
                auto sub = mCalibratedOverhead[i] + 1U * numIters;
                auto val = mCounters[i];
                if (val > sub) {
                    val -= sub;
                } else {
                    val = 0;
                }
                mLoopOverhead[i] = divRounded(val, numIters);
            }
        }
    }

private:
    bool monitor(uint32_t type, uint64_t eventid, Target target);

    std::map<uint64_t, Target> mIdToTarget{};

    // start with minimum size of 3 for read_format
    std::vector<uint64_t> mCounters{3};
    std::vector<uint64_t> mCalibratedOverhead{3};
    std::vector<uint64_t> mLoopOverhead{3};

    uint64_t mTimeEnabledNanos = 0;
    uint64_t mTimeRunningNanos = 0;
    int mFd = -1;
    bool mHasError = false;
};
ANKERL_NANOBENCH(IGNORE_PADDED_POP)

LinuxPerformanceCounters::~LinuxPerformanceCounters() {
    if (-1 != mFd) {
        close(mFd);
    }
}

bool LinuxPerformanceCounters::monitor(perf_sw_ids swId, LinuxPerformanceCounters::Target target) {
    return monitor(PERF_TYPE_SOFTWARE, swId, target);
}

bool LinuxPerformanceCounters::monitor(perf_hw_id hwId, LinuxPerformanceCounters::Target target) {
    return monitor(PERF_TYPE_HARDWARE, hwId, target);
}

// overflow is ok, it's checked
ANKERL_NANOBENCH_NO_SANITIZE("integer")
void LinuxPerformanceCounters::updateResults(uint64_t numIters) {
    // clear old data
    for (auto& id_value : mIdToTarget) {
        *id_value.second.targetValue = UINT64_C(0);
    }

    if (mHasError) {
        return;
    }

    mTimeEnabledNanos = mCounters[1] - mCalibratedOverhead[1];
    mTimeRunningNanos = mCounters[2] - mCalibratedOverhead[2];

    for (uint64_t i = 0; i < mCounters[0]; ++i) {
        auto idx = static_cast<size_t>(3 + i * 2 + 0);
        auto id = mCounters[idx + 1U];

        auto it = mIdToTarget.find(id);
        if (it != mIdToTarget.end()) {

            auto& tgt = it->second;
            *tgt.targetValue = mCounters[idx];
            if (tgt.correctMeasuringOverhead) {
                if (*tgt.targetValue >= mCalibratedOverhead[idx]) {
                    *tgt.targetValue -= mCalibratedOverhead[idx];
                } else {
                    *tgt.targetValue = 0U;
                }
            }
            if (tgt.correctLoopOverhead) {
                auto correctionVal = mLoopOverhead[idx] * numIters;
                if (*tgt.targetValue >= correctionVal) {
                    *tgt.targetValue -= correctionVal;
                } else {
                    *tgt.targetValue = 0U;
                }
            }
        }
    }
}

bool LinuxPerformanceCounters::monitor(uint32_t type, uint64_t eventid, Target target) {
    *target.targetValue = (std::numeric_limits<uint64_t>::max)();
    if (mHasError) {
        return false;
    }

    auto pea = perf_event_attr();
    std::memset(&pea, 0, sizeof(perf_event_attr));
    pea.type = type;
    pea.size = sizeof(perf_event_attr);
    pea.config = eventid;
    pea.disabled = 1; // start counter as disabled
    pea.exclude_kernel = 1;
    pea.exclude_hv = 1;

    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    pea.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;

    const int pid = 0;                    // the current process
    const int cpu = -1;                   // all CPUs
#        if defined(PERF_FLAG_FD_CLOEXEC) // since Linux 3.14
    const unsigned long flags = PERF_FLAG_FD_CLOEXEC;
#        else
    const unsigned long flags = 0;
#        endif

    auto fd = static_cast<int>(syscall(__NR_perf_event_open, &pea, pid, cpu, mFd, flags));
    if (-1 == fd) {
        return false;
    }
    if (-1 == mFd) {
        // first call: set to fd, and use this from now on
        mFd = fd;
    }
    uint64_t id = 0;
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    if (-1 == ioctl(fd, PERF_EVENT_IOC_ID, &id)) {
        // couldn't get id
        return false;
    }

    // insert into map, rely on the fact that map's references are constant.
    mIdToTarget.emplace(id, target);

    // prepare readformat with the correct size (after the insert)
    auto size = 3 + 2 * mIdToTarget.size();
    mCounters.resize(size);
    mCalibratedOverhead.resize(size);
    mLoopOverhead.resize(size);

    return true;
}

PerformanceCounters::PerformanceCounters()
    : mPc(new LinuxPerformanceCounters())
    , mVal()
    , mHas() {

    mHas.pageFaults = mPc->monitor(PERF_COUNT_SW_PAGE_FAULTS, LinuxPerformanceCounters::Target(&mVal.pageFaults, true, false));
    mHas.cpuCycles = mPc->monitor(PERF_COUNT_HW_REF_CPU_CYCLES, LinuxPerformanceCounters::Target(&mVal.cpuCycles, true, false));
    mHas.contextSwitches =
        mPc->monitor(PERF_COUNT_SW_CONTEXT_SWITCHES, LinuxPerformanceCounters::Target(&mVal.contextSwitches, true, false));
    mHas.instructions = mPc->monitor(PERF_COUNT_HW_INSTRUCTIONS, LinuxPerformanceCounters::Target(&mVal.instructions, true, true));
    mHas.branchInstructions =
        mPc->monitor(PERF_COUNT_HW_BRANCH_INSTRUCTIONS, LinuxPerformanceCounters::Target(&mVal.branchInstructions, true, false));
    mHas.branchMisses = mPc->monitor(PERF_COUNT_HW_BRANCH_MISSES, LinuxPerformanceCounters::Target(&mVal.branchMisses, true, false));
    // mHas.branchMisses = false;

    mPc->start();
    mPc->calibrate([] {
        auto before = ankerl::nanobench::Clock::now();
        auto after = ankerl::nanobench::Clock::now();
        (void)before;
        (void)after;
    });

    if (mPc->hasError()) {
        // something failed, don't monitor anything.
        mHas = PerfCountSet<bool>{};
    }
}

PerformanceCounters::~PerformanceCounters() {
    if (nullptr != mPc) {
        delete mPc;
    }
}

void PerformanceCounters::beginMeasure() {
    mPc->beginMeasure();
}

void PerformanceCounters::endMeasure() {
    mPc->endMeasure();
}

void PerformanceCounters::updateResults(uint64_t numIters) {
    mPc->updateResults(numIters);
}

#    else

PerformanceCounters::PerformanceCounters() = default;
PerformanceCounters::~PerformanceCounters() = default;
void PerformanceCounters::beginMeasure() {}
void PerformanceCounters::endMeasure() {}
void PerformanceCounters::updateResults(uint64_t) {}

#    endif

ANKERL_NANOBENCH(NODISCARD) PerfCountSet<uint64_t> const& PerformanceCounters::val() const noexcept {
    return mVal;
}
ANKERL_NANOBENCH(NODISCARD) PerfCountSet<bool> const& PerformanceCounters::has() const noexcept {
    return mHas;
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

Number::Number(int width, int precision, int64_t value)
    : mWidth(width)
    , mPrecision(precision)
    , mValue(static_cast<double>(value)) {}

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

std::string Number::to_s() const {
    std::stringstream ss;
    write(ss);
    return ss.str();
}

std::ostream& operator<<(std::ostream& os, Number const& n) {
    return n.write(os);
}

MarkDownColumn::MarkDownColumn(int width, int precision, std::string const& title, std::string const& suffix, double val)
    : mWidth(width)
    , mPrecision(precision)
    , mTitle(title)
    , mSuffix(suffix)
    , mValue(val) {}

std::string MarkDownColumn::title() const {
    std::stringstream ss;
    ss << '|' << std::setw(mWidth - 2) << std::right << mTitle << ' ';
    return ss.str();
}

std::string MarkDownColumn::separator() const {
    std::string sep(static_cast<size_t>(mWidth), '-');
    sep.front() = '|';
    sep.back() = ':';
    return sep;
}

std::string MarkDownColumn::invalid() const {
    std::string sep(static_cast<size_t>(mWidth), ' ');
    sep.front() = '|';
    sep[sep.size() - 2] = '-';
    return sep;
}

std::string MarkDownColumn::value() const {
    std::stringstream ss;
    auto width = mWidth - 2 - static_cast<int>(mSuffix.size());
    ss << '|' << Number(width, mPrecision, mValue) << mSuffix << ' ';
    return ss.str();
}

// Formats any text as markdown code, escaping backticks.
MarkDownCode::MarkDownCode(std::string const& what) {
    mWhat.reserve(what.size() + 2);
    mWhat.push_back('`');
    for (char c : what) {
        mWhat.push_back(c);
        if ('`' == c) {
            mWhat.push_back('`');
        }
    }
    mWhat.push_back('`');
}

std::ostream& MarkDownCode::write(std::ostream& os) const {
    return os << mWhat;
}

std::ostream& operator<<(std::ostream& os, MarkDownCode const& mdCode) {
    return mdCode.write(os);
}

namespace mustache {

ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
struct Node {
    enum class Type { tag, content, section, inverted_section };

    char const* begin;
    char const* end;
    std::vector<Node> children;
    Type type;

    template <size_t N>
    // NOLINTNEXTLINE(hicpp-avoid-c-arrays,modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays)
    bool operator==(char const (&str)[N]) const noexcept {
        return static_cast<size_t>(std::distance(begin, end) + 1) == N && 0 == strncmp(str, begin, N - 1);
    }
};
ANKERL_NANOBENCH(IGNORE_PADDED_POP)

static std::vector<Node> parseMustacheTemplate(char const** tpl) {
    std::vector<Node> nodes;

    while (true) {
        auto begin = std::strstr(*tpl, "{{");
        auto end = begin;
        if (begin != nullptr) {
            begin += 2;
            end = std::strstr(begin, "}}");
        }

        if (begin == nullptr || end == nullptr) {
            // nothing found, finish node
            nodes.emplace_back(Node{*tpl, *tpl + std::strlen(*tpl), std::vector<Node>{}, Node::Type::content});
            return nodes;
        }

        nodes.emplace_back(Node{*tpl, begin - 2, std::vector<Node>{}, Node::Type::content});

        // we found a tag
        *tpl = end + 2;
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

static bool generateFirstLast(Node const& n, size_t idx, size_t size, std::ostream& out) {
    bool matchFirst = n == "-first";
    bool matchLast = n == "-last";
    if (!matchFirst && !matchLast) {
        return false;
    }

    bool doWrite = false;
    if (n.type == Node::Type::section) {
        doWrite = (matchFirst && idx == 0) || (matchLast && idx == size - 1);
    } else if (n.type == Node::Type::inverted_section) {
        doWrite = (matchFirst && idx != 0) || (matchLast && idx != size - 1);
    }

    if (doWrite) {
        for (auto const& child : n.children) {
            if (child.type == Node::Type::content) {
                out.write(child.begin, std::distance(child.begin, child.end));
            }
        }
    }
    return true;
}

static void generateMeasurement(std::vector<Node> const& nodes, std::vector<ankerl::nanobench::Measurement> const& measurements,
                                size_t measurementIdx, std::ostream& out) {
    auto const& measurement = measurements[measurementIdx];
    for (auto const& n : nodes) {
        if (!generateFirstLast(n, measurementIdx, measurements.size(), out)) {
            switch (n.type) {
            case Node::Type::content:
                out.write(n.begin, std::distance(n.begin, n.end));
                break;

            case Node::Type::inverted_section:
                throw std::runtime_error("got a inverted section inside measurement");

            case Node::Type::section:
                throw std::runtime_error("got a section inside measurement");

            case Node::Type::tag:
                if (n == "sec_per_unit") {
                    out << measurement.secPerUnit().count();
                } else if (n == "iters") {
                    out << measurement.numIters();
                } else if (n == "elapsed_ns") {
                    out << measurement.elapsed().count();
                } else if (n == "pagefaults") {
                    out << measurement.pageFaults();
                } else if (n == "cpucycles") {
                    out << measurement.cpuCycles();
                } else if (n == "contextswitches") {
                    out << measurement.contextSwitches();
                } else if (n == "instructions") {
                    out << measurement.instructions();
                } else if (n == "branchinstructions") {
                    out << measurement.branchInstructions();
                } else if (n == "branchmisses") {
                    out << measurement.branchMisses();
                } else {
                    throw std::runtime_error("unknown tag '" + std::string(n.begin, n.end) + "'");
                }
                break;
            }
        }
    }
}

static void generateBenchmark(std::vector<Node> const& nodes, std::vector<ankerl::nanobench::Result> const& results, size_t resultIdx,
                              std::ostream& out) {
    auto const& result = results[resultIdx];
    for (auto const& n : nodes) {
        if (!generateFirstLast(n, resultIdx, results.size(), out)) {
            switch (n.type) {
            case Node::Type::content:
                out.write(n.begin, std::distance(n.begin, n.end));
                break;

            case Node::Type::section:
                if (n == "results") {
                    for (size_t m = 0; m < result.sortedMeasurements().size(); ++m) {
                        generateMeasurement(n.children, result.sortedMeasurements(), m, out);
                    }
                } else {
                    throw std::runtime_error("unknown list '" + std::string(n.begin, n.end) + "'");
                }
                break;

            case Node::Type::inverted_section:
                throw std::runtime_error("unknown list '" + std::string(n.begin, n.end) + "'");

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
                } else if (n == "median_ins_per_unit") {
                    out << result.medianInstructionsPerUnit();
                } else if (n == "median_branches_per_unit") {
                    out << result.medianBranchesPerUnit();
                } else if (n == "median_branchmisses_per_unit") {
                    out << result.medianBranchMissesPerUnit();
                } else {
                    throw std::runtime_error("unknown tag '" + std::string(n.begin, n.end) + "'");
                }
            }
        }
    }
}

static void generate(char const* mustacheTemplate, ankerl::nanobench::Config const& cfg, std::ostream& out) {
    // TODO(martinus) safe stream status
    out.precision(std::numeric_limits<double>::digits10);
    auto nodes = parseMustacheTemplate(&mustacheTemplate);
    for (auto const& n : nodes) {
        switch (n.type) {
        case Node::Type::content:
            out.write(n.begin, std::distance(n.begin, n.end));
            break;

        case Node::Type::inverted_section:
            throw std::runtime_error("unknown list '" + std::string(n.begin, n.end) + "'");

        case Node::Type::section:
            if (n == "benchmarks") {
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
            }
            break;
        }
    }
}

} // namespace mustache
} // namespace fmt
} // namespace detail

Measurement::Measurement(Clock::duration totalElapsed, uint64_t iters, double batch, detail::PerformanceCounters const& pc) noexcept
    : mTotalElapsed(totalElapsed)
    , mNumIters(iters)
    , mSecPerUnit(std::chrono::duration_cast<std::chrono::duration<double>>(totalElapsed) / (batch * static_cast<double>(iters)))
    , mVal(pc.val()) {

    // correcting branches: remove branch introduced by the while (...) loop for each iteration.
    if (mVal.branchInstructions > iters + 1U) {
        mVal.branchInstructions -= iters + 1U;
    } else {
        mVal.branchInstructions = 0;
    }

    // correcting branch misses: typically just one miss per branch
    if (mVal.branchMisses > mVal.branchInstructions) {
        // can't have branch misses when there were branches...
        mVal.branchMisses = mVal.branchInstructions;
    }
    if (mVal.branchMisses > 1) {
        // assuming at least one missed branch for the loop
        mVal.branchMisses -= 1;
    }
}

bool Measurement::operator<(Measurement const& other) const noexcept {
    return mSecPerUnit < other.mSecPerUnit;
}

Clock::duration const& Measurement::elapsed() const noexcept {
    return mTotalElapsed;
}

uint64_t Measurement::numIters() const noexcept {
    return mNumIters;
}

std::chrono::duration<double> Measurement::secPerUnit() const noexcept {
    return mSecPerUnit;
}

uint64_t Measurement::pageFaults() const noexcept {
    return mVal.pageFaults;
}
uint64_t Measurement::cpuCycles() const noexcept {
    return mVal.cpuCycles;
}
uint64_t Measurement::contextSwitches() const noexcept {
    return mVal.contextSwitches;
}
uint64_t Measurement::instructions() const noexcept {
    return mVal.instructions;
}
uint64_t Measurement::branchInstructions() const noexcept {
    return mVal.branchInstructions;
}
uint64_t Measurement::branchMisses() const noexcept {
    return mVal.branchMisses;
}

template <typename T>
inline double d(T t) noexcept {
    return static_cast<double>(t);
}

template <typename T>
class CalcMedian {
public:
    CalcMedian(T const& input)
        : mInput(input)
        , mData(mInput.size()) {}

    template <typename Op>
    double operator()(Op&& op) {
        for (size_t i = 0; i < mInput.size(); ++i) {
            mData[i] = op(mInput[i]);
        }
        std::sort(mData.begin(), mData.end());
        auto midpoint = mData.size() / 2;
        if (1U == (mData.size() & 1U)) {
            return mData[midpoint];
        } else {
            return (mData[midpoint - 1U] + mData[midpoint]) / 2U;
        }
    }

private:
    T const& mInput;
    std::vector<double> mData{};
};

// Result returned after a benchmark has finished. Can be used as a baseline for relative().
Result::Result(std::string benchmarkName, std::vector<Measurement> measurements, double batch) noexcept
    : mName(std::move(benchmarkName))
    , mSortedMeasurements(std::move(measurements))
    , mHas(detail::performanceCounters().has()) {

    std::sort(mSortedMeasurements.begin(), mSortedMeasurements.end());

    // calculates MdAPE which is the median of percentage error
    // see https://www.spiderfinancial.com/support/documentation/numxl/reference-manual/forecasting-performance/mdape
    auto const med = median();

    CalcMedian<std::vector<Measurement>> calcMedian(mSortedMeasurements);
    mMedianAbsolutePercentError = calcMedian([&](Measurement const& m) {
        auto percent = (m.secPerUnit() - med) / m.secPerUnit();
        if (percent < 0) {
            percent = -percent;
        }
        return percent;
    });

    if (mHas.cpuCycles) {
        mMedianCpuCyclesPerUnit = calcMedian([&](Measurement const& m) { return d(m.cpuCycles()) / (batch * d(m.numIters())); });
    }

    if (mHas.instructions) {
        mMedianInstructionsPerUnit = calcMedian([&](Measurement const& m) { return d(m.instructions()) / (batch * d(m.numIters())); });
    }

    if (mHas.branchInstructions) {
        mMedianBranchesPerUnit =
            calcMedian([&](Measurement const& m) { return d(m.branchInstructions()) / (batch * d(m.numIters())); });
    }

    if (mHas.branchMisses) {
        mMedianBranchMissesPerUnit = calcMedian([&](Measurement const& m) { return d(m.branchMisses()) / (batch * d(m.numIters())); });
    }
}

Result::Result() noexcept
    : mHas(detail::performanceCounters().has()) {}

std::string const& Result::name() const noexcept {
    return mName;
}

std::chrono::duration<double> Result::median() const noexcept {
    if (mSortedMeasurements.empty()) {
        return {};
    }

    auto mid = mSortedMeasurements.size() / 2U;
    if (1U == (mSortedMeasurements.size() & 1U)) {
        return mSortedMeasurements[mid].secPerUnit();
    }
    return (mSortedMeasurements[mid - 1U].secPerUnit() + mSortedMeasurements[mid].secPerUnit()) / 2U;
}

Clock::duration Result::total() const noexcept {
    Clock::duration tot{};
    for (auto const& m : mSortedMeasurements) {
        tot += m.elapsed();
    }
    return tot;
}

std::vector<Measurement> const& Result::sortedMeasurements() const noexcept {
    return mSortedMeasurements;
}

double Result::medianAbsolutePercentError() const noexcept {
    return mMedianAbsolutePercentError;
}

bool Result::empty() const noexcept {
    return mSortedMeasurements.empty();
}

std::chrono::duration<double> Result::minimum() const noexcept {
    return mSortedMeasurements.front().secPerUnit();
}

std::chrono::duration<double> Result::maximum() const noexcept {
    return mSortedMeasurements.back().secPerUnit();
}

double Result::medianCpuCyclesPerUnit() const noexcept {
    return mMedianCpuCyclesPerUnit;
}
bool Result::hasMedianCpuCyclesPerUnit() const noexcept {
    return mHas.cpuCycles;
}

double Result::medianInstructionsPerUnit() const noexcept {
    return mMedianInstructionsPerUnit;
}
bool Result::hasMedianInstructionsPerUnit() const noexcept {
    return mHas.instructions;
}

double Result::medianBranchesPerUnit() const noexcept {
    return mMedianBranchesPerUnit;
}
bool Result::hasMedianBranchesPerUnit() const noexcept {
    return mHas.branchInstructions;
}

double Result::medianBranchMissesPerUnit() const noexcept {
    return mMedianBranchMissesPerUnit;
}
bool Result::hasMedianBranchMissesPerUnit() const noexcept {
    return mHas.branchMisses;
}

// Configuration of a microbenchmark.
Config::Config()
    : mOut(&std::cout) {}

Config::Config(Config&&) = default;
Config& Config::operator=(Config&&) = default;
Config::Config(Config const&) = default;
Config& Config::operator=(Config const&) = default;
Config::~Config() noexcept = default;

double Config::batch() const noexcept {
    return mBatch;
}

// Set a baseline to compare it to. 100% it is exactly as fast as the baseline, >100% means it is faster than the baseline, <100%
// means it is slower than the baseline.
Config& Config::relative(bool isRelativeEnabled) noexcept {
    mIsRelative = isRelativeEnabled;
    return *this;
}
bool Config::relative() const noexcept {
    return mIsRelative;
}

Config& Config::performanceCounters(bool showPerformanceCounters) noexcept {
    mShowPerformanceCounters = showPerformanceCounters;
    return *this;
}
bool Config::performanceCounters() const noexcept {
    return mShowPerformanceCounters;
}

// Operation unit. Defaults to "op", could be e.g. "byte" for string processing.
// Use singular (byte, not bytes).
Config& Config::unit(std::string u) {
    mUnit = std::move(u);
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

// Sets the maximum time each epoch should take. Default is 100ms.
Config& Config::minEpochTime(std::chrono::nanoseconds t) noexcept {
    mMinEpochTime = t;
    return *this;
}
std::chrono::nanoseconds Config::minEpochTime() const noexcept {
    return mMinEpochTime;
}

Config& Config::minEpochIterations(uint64_t numIters) noexcept {
    mMinEpochIterations = (numIters == 0) ? 1 : numIters;
    return *this;
}
uint64_t Config::minEpochIterations() const noexcept {
    return mMinEpochIterations;
}

Config& Config::warmup(uint64_t numWarmupIters) noexcept {
    mWarmup = numWarmupIters;
    return *this;
}
uint64_t Config::warmup() const noexcept {
    return mWarmup;
}

Config& Config::output(std::ostream* outstream) noexcept {
    mOut = outstream;
    return *this;
}

ANKERL_NANOBENCH(NODISCARD) std::ostream* Config::output() const noexcept {
    return mOut;
}

std::vector<Result> const& Config::results() const noexcept {
    return mResults;
}

Config& Config::render(char const* templateContent, std::ostream& os) {
    detail::fmt::mustache::generate(templateContent, *this, os);
    return *this;
}

Rng::Rng()
    : Rng(UINT64_C(0xd3b45fd780a1b6a3)) {}

Rng::Rng(uint64_t seed) noexcept
    : mA(seed)
    , mB(seed)
    , mC(seed)
    , mCounter(1) {
    for (size_t i = 0; i < 12; ++i) {
        operator()();
    }
}

Rng Rng::copy() const noexcept {
    Rng r;
    r.mA = mA;
    r.mB = mB;
    r.mC = mC;
    r.mCounter = mCounter;
    return r;
}

void Rng::assign(Rng const& other) noexcept {
    mA = other.mA;
    mB = other.mB;
    mC = other.mC;
    mCounter = other.mCounter;
}

} // namespace nanobench
} // namespace ankerl

#endif // ANKERL_NANOBENCH_IMPLEMENT
#endif // ANKERL_NANOBENCH_H_INCLUDED
