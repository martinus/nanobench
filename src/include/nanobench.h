//  __   _ _______ __   _  _____  ______  _______ __   _ _______ _     _
//  | \  | |_____| | \  | |     | |_____] |______ | \  | |       |_____|
//  |  \_| |     | |  \_| |_____| |_____] |______ |  \_| |_____  |     |
//
// Microbenchmark framework for C++11/14/17/20
// https://github.com/martinus/nanobench
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2019-2023 Martin Leitner-Ankerl <martin.ankerl@gmail.com>
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
#define ANKERL_NANOBENCH_VERSION_MAJOR 4 // incompatible API changes
#define ANKERL_NANOBENCH_VERSION_MINOR 5 // backwards-compatible changes
#define ANKERL_NANOBENCH_VERSION_PATCH 0 // backwards-compatible bug fixes

///////////////////////////////////////////////////////////////////////////////////////////////////
// public facing api - as minimal as possible
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <chrono>        // high_resolution_clock
#include <cmath>         // log & exp for the paired A/B statistics
#include <cstring>       // memcpy
#include <iosfwd>        // for std::ostream* custom output target in Config
#include <string>        // all names
#include <unordered_map> // holds context information of results
#include <vector>        // holds all results

#define ANKERL_NANOBENCH(x) ANKERL_NANOBENCH_PRIVATE_##x()

// MSVC reports __cplusplus as 199711L whatever /std: says, unless /Zc:__cplusplus is passed - and it
// is not passed by default. _MSVC_LANG carries the real value. Getting this wrong does not fail to
// compile, it silently takes the pre-C++17 branch on MSVC, which is how [[nodiscard]] was missing
// there.
#if defined(_MSVC_LANG)
#    define ANKERL_NANOBENCH_PRIVATE_CXX() _MSVC_LANG
#else
#    define ANKERL_NANOBENCH_PRIVATE_CXX() __cplusplus
#endif
#define ANKERL_NANOBENCH_PRIVATE_CXX98() 199711L
#define ANKERL_NANOBENCH_PRIVATE_CXX11() 201103L
#define ANKERL_NANOBENCH_PRIVATE_CXX14() 201402L
#define ANKERL_NANOBENCH_PRIVATE_CXX17() 201703L

#if ANKERL_NANOBENCH(CXX) >= ANKERL_NANOBENCH(CXX17)
#    define ANKERL_NANOBENCH_PRIVATE_NODISCARD() [[nodiscard]]
#    define ANKERL_NANOBENCH_PRIVATE_HAS_STRING_VIEW() 1
#else
#    define ANKERL_NANOBENCH_PRIVATE_NODISCARD()
#    define ANKERL_NANOBENCH_PRIVATE_HAS_STRING_VIEW() 0
#endif

#if ANKERL_NANOBENCH(HAS_STRING_VIEW)
#    include <string_view> // the name() and run() overloads
#endif

#if defined(__clang__)
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_PADDED_PUSH() \
        _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wpadded\"")
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_PADDED_POP() _Pragma("clang diagnostic pop")
#else
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_PADDED_PUSH()
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_PADDED_POP()
#endif

#if defined(__GNUC__)
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_EFFCPP_PUSH() _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Weffc++\"")
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_EFFCPP_POP() _Pragma("GCC diagnostic pop")
#else
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_EFFCPP_PUSH()
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_EFFCPP_POP()
#endif

#if defined(ANKERL_NANOBENCH_LOG_ENABLED)
#    include <iostream>
#    define ANKERL_NANOBENCH_LOG(x)                                                 \
        do {                                                                        \
            std::cout << __FUNCTION__ << "@" << __LINE__ << ": " << x << std::endl; \
        } while (0)
#else
#    define ANKERL_NANOBENCH_LOG(x) \
        do {                        \
        } while (0)
#endif

#define ANKERL_NANOBENCH_PRIVATE_PERF_COUNTERS() 0
#if defined(__linux__) && !defined(ANKERL_NANOBENCH_DISABLE_PERF_COUNTERS)
#    include <linux/version.h>
#    if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
// PERF_COUNT_HW_REF_CPU_CYCLES only available since kernel 3.3
// PERF_FLAG_FD_CLOEXEC since kernel 3.14
#        undef ANKERL_NANOBENCH_PRIVATE_PERF_COUNTERS
#        define ANKERL_NANOBENCH_PRIVATE_PERF_COUNTERS() 1
#    endif
#endif

#if defined(__clang__)
#    define ANKERL_NANOBENCH_NO_SANITIZE(...) __attribute__((no_sanitize(__VA_ARGS__)))
#else
#    define ANKERL_NANOBENCH_NO_SANITIZE(...)
#endif

#if defined(_MSC_VER)
#    define ANKERL_NANOBENCH_PRIVATE_NOINLINE() __declspec(noinline)
#else
#    define ANKERL_NANOBENCH_PRIVATE_NOINLINE() __attribute__((noinline))
#endif

// Whether doNotOptimizeAway can use GCC-style inline assembly, which is both faster and a stronger barrier than the
// function call it falls back to. Testing for _MSC_VER is not enough to rule it out: clang-cl and clang in MSVC
// compatibility mode define _MSC_VER but not __GNUC__, even though they do understand the assembly.
// See https://github.com/martinus/nanobench/issues/111
#if defined(__clang__) || defined(__GNUC__)
#    define ANKERL_NANOBENCH_PRIVATE_ASM_DONT_OPTIMIZE_AWAY() 1
#else
#    define ANKERL_NANOBENCH_PRIVATE_ASM_DONT_OPTIMIZE_AWAY() 0
#endif

// workaround missing "is_trivially_copyable" in g++ < 5.0
// See https://stackoverflow.com/a/31798726/48181
#if defined(__GNUC__) && __GNUC__ < 5
#    define ANKERL_NANOBENCH_IS_TRIVIALLY_COPYABLE(...) __has_trivial_copy(__VA_ARGS__)
#else
#    define ANKERL_NANOBENCH_IS_TRIVIALLY_COPYABLE(...) std::is_trivially_copyable<__VA_ARGS__>::value
#endif

// noexcept may be missing for std::string.
// See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=58265
#define ANKERL_NANOBENCH_PRIVATE_NOEXCEPT_STRING_MOVE() std::is_nothrow_move_assignable<std::string>::value

// nanobench throws only for programming errors in a render template - a tag that does not exist, a
// section in the wrong place. With exceptions turned off there is nothing sensible left to do but
// stop, so the argument is never evaluated and the message building disappears entirely.
//
// std::abort() rather than a per-compiler intrinsic on purpose: the branch that would pick one is
// the branch no CI leg can reach, and an untested call into CRT internals is exactly the kind of
// thing that turns out not to compile the first time someone actually needs it.
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#    define ANKERL_NANOBENCH_THROW(...) throw __VA_ARGS__
#else
#    define ANKERL_NANOBENCH_THROW(...) std::abort()
#endif

// declarations ///////////////////////////////////////////////////////////////////////////////////

namespace ankerl {
namespace nanobench {

using Clock = std::conditional<std::chrono::high_resolution_clock::is_steady, std::chrono::high_resolution_clock,
                               std::chrono::steady_clock>::type;
class Bench;
struct Config;
class Result;
class Rng;
class BigO;
class CompareResult;

namespace detail {
template <typename SetupOp>
class SetupRunner;

// One of compare()'s alternatives, with its type forgotten so that they can live in a vector.
//
// The alternatives all have different types, so holding them needs either type erasure or a template
// recursion that walks the pack at every step. This is the erasure, and it costs nothing that
// matters: `run` points at an instantiation that knows the concrete type, so the measuring loop is
// inside a function where the operation still inlines exactly as it would otherwise. What is added
// is one indirect call per *epoch*, against an epoch of a millisecond.
//
// std::function would not do, because it would put that indirection in the inner loop instead.
struct ErasedOp {
    void (*run)(void* op, uint64_t numIters); // NOLINT(misc-non-private-member-variables-in-classes)
    void* op;                                 // NOLINT(misc-non-private-member-variables-in-classes)
};

template <typename Op>
void runErasedOp(void* op, uint64_t numIters) {
    auto& concrete = *static_cast<Op*>(op);
    for (uint64_t i = 0; i < numIters; ++i) {
        concrete();
    }
}

template <typename Op>
ErasedOp eraseOp(Op&& op) {
    using Bare = typename std::remove_reference<Op>::type;
    // The operation is only ever called, never written to, so casting a const one back to non-const
    // to fit it through void* is safe - a const lambda's operator() is const or it could not be
    // called at all.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return ErasedOp{&runErasedOp<Bare>, const_cast<void*>(static_cast<void const*>(&op))};
}
} // namespace detail

/**
 * @brief Renders output from a mustache-like template and benchmark results.
 *
 * The templating facility here is heavily inspired by [mustache - logic-less templates](https://mustache.github.io/).
 * It adds a few more features that are necessary to get all of the captured data out of nanobench. Please read the
 * excellent [mustache manual](https://mustache.github.io/mustache.5.html) to see what this is all about.
 *
 * nanobench output has two nested layers, *result* and *measurement*.  Here is a hierarchy of the allowed tags:
 *
 * * `{{#result}}` Marks the begin of the result layer. Whatever comes after this will be instantiated as often as
 *   a benchmark result is available. Within it, you can use these tags:
 *
 *    * `{{title}}` See Bench::title.
 *
 *    * `{{name}}` Benchmark name, usually directly provided with Bench::run, but can also be set with Bench::name.
 *
 *    * `{{unit}}` Unit, e.g. `byte`. Defaults to `op`, see Bench::unit.
 *
 *    * `{{batch}}` Batch size, see Bench::batch.
 *
 *    * `{{complexityN}}` Value used for asymptotic complexity calculation. See Bench::complexityN.
 *
 *    * `{{epochs}}` Number of epochs, see Bench::epochs.
 *
 *    * `{{clockResolution}}` Accuracy of the clock, i.e. what's the smallest time possible to measure with the clock.
 *      For modern systems, this can be around 20 ns. This value is automatically determined by nanobench at the first
 *      benchmark that is run, and used as a static variable throughout the application's runtime.
 *
 *    * `{{clockResolutionMultiple}}` Configuration multiplier for `clockResolution`. See Bench::clockResolutionMultiple.
 *      This is the target runtime for each measurement (epoch). That means the more accurate your clock is, the faster
 *      will be the benchmark. Basing the measurement's runtime on the clock resolution is the main reason why nanobench is so fast.
 *
 *    * `{{maxEpochTime}}` Configuration for a maximum time each measurement (epoch) is allowed to take. Note that at least
 *      a single iteration will be performed, even when that takes longer than maxEpochTime. See Bench::maxEpochTime.
 *
 *    * `{{minEpochTime}}` Minimum epoch time, defaults to 1ms. See Bench::minEpochTime.
 *
 *    * `{{minEpochIterations}}` See Bench::minEpochIterations.
 *
 *    * `{{epochIterations}}` See Bench::epochIterations.
 *
 *    * `{{warmup}}` Number of iterations used before measuring starts. See Bench::warmup.
 *
 *    * `{{relative}}` True or false, depending on the setting you have used. See Bench::relative.
 *
 *    * `{{context(variableName)}}` See Bench::context.
 *
 *    Apart from these tags, it is also possible to use some mathematical operations on the measurement data. The operations
 *    are of the form `{{command(name)}}`.  Currently `name` can be one of `elapsed`, `iterations`. If performance counters
 *    are available (currently only on current Linux systems), you also have `pagefaults`, `cpucycles`,
 *    `contextswitches`, `instructions`, `branchinstructions`, and `branchmisses`. All the measures (except `iterations`) are
 *    provided for a single iteration (so `elapsed` is the time a single iteration took). The following tags are available:
 *
 *    `elapsed` is in **seconds**, which is rarely the unit a report wants. Since the template language has no arithmetic
 *    to scale it afterwards, the time measure also comes in `elapsedms`, `elapsedus` and `elapsedns` — the same value in
 *    milliseconds, microseconds and nanoseconds. So `{{minimum(elapsedms)}}` is the fastest iteration in milliseconds.
 *    Note this is unrelated to Bench::timeUnit(), which only sets the unit of the `ns/op` column in the table.
 *    `{{medianAbsolutePercentError(...)}}` is a relative error, so it is the same number whichever of them you ask for.
 *
 *    * `{{median(<name>)}}` Calculate median of a measurement data set, e.g. `{{median(elapsed)}}`.
 *
 *    * `{{average(<name>)}}` Average (mean) calculation.
 *
 *    * `{{medianAbsolutePercentError(<name>)}}` Calculates MdAPE, the Median Absolute Percentage Error. The MdAPE is an excellent
 *      metric for the variation of measurements. It is more robust to outliers than the
 *      [Mean absolute percentage error (M-APE)](https://en.wikipedia.org/wiki/Mean_absolute_percentage_error).
 *      @f[
 *       \mathrm{MdAPE}(e) = \mathrm{med}\{| \frac{e_i - \mathrm{med}\{e\}}{e_i}| \}
 *      @f]
 *      E.g. for *elapsed*: First, @f$ \mathrm{med}\{e\} @f$ calculates the median by sorting and then taking the middle element
 *      of all *elapsed* measurements. This is used to calculate the absolute percentage
 *      error to this median for each measurement, as in  @f$ | \frac{e_i - \mathrm{med}\{e\}}{e_i}| @f$. All these results
 *      are sorted, and the middle value is chosen as the median absolute percent error.
 *
 *      This measurement is a bit hard to interpret, but it is very robust against outliers. E.g. a value of 5% means that half of the
 *      measurements deviate less than 5% from the median, and the other deviate more than 5% from the median.
 *
 *    * `{{sum(<name>)}}` Sum of all the measurements. E.g. `{{sum(iterations)}}` will give you the total number of iterations
*        measured in this benchmark.
 *
 *    * `{{minimum(<name>)}}` Minimum of all measurements.
 *
 *    * `{{maximum(<name>)}}` Maximum of all measurements.
 *
 *    * `{{sumProduct(<first>, <second>)}}` Calculates the sum of the products of corresponding measures:
 *      @f[
 *          \mathrm{sumProduct}(a,b) = \sum_{i=1}^{n}a_i\cdot b_i
 *      @f]
 *      E.g. to calculate total runtime of the benchmark, you multiply iterations with elapsed time for each measurement, and
 *      sum these results up:
 *      `{{sumProduct(iterations, elapsed)}}`.
 *
 *    * `{{#measurement}}` To access individual measurement results, open the begin tag for measurements.
 *
 *       * `{{elapsed}}` Average elapsed wall clock time per iteration, in seconds.
 *
 *       * `{{iterations}}` Number of iterations in the measurement. The number of iterations will fluctuate due
 *         to some applied randomness, to enhance accuracy.
 *
 *       * `{{pagefaults}}` Average number of pagefaults per iteration.
 *
 *       * `{{cpucycles}}` Average number of CPU cycles processed per iteration.
 *
 *       * `{{contextswitches}}` Average number of context switches per iteration.
 *
 *       * `{{instructions}}` Average number of retired instructions per iteration.
 *
 *       * `{{branchinstructions}}` Average number of branches executed per iteration.
 *
 *       * `{{branchmisses}}` Average number of branches that were missed per iteration.
 *
 *    * `{{/measurement}}` Ends the measurement tag.
 *
 * * `{{/result}}` Marks the end of the result layer. This is the end marker for the template part that will be instantiated
 *   for each benchmark result.
 *
 *
 *  For the layer tags *result* and *measurement* you additionally can use these special markers:
 *
 *  * ``{{#-first}}`` - Begin marker of a template that will be instantiated *only for the first* entry in the layer. Use is only
 *    allowed between the begin and end marker of the layer. So between ``{{#result}}`` and ``{{/result}}``, or between
 *    ``{{#measurement}}`` and ``{{/measurement}}``. Finish the template with ``{{/-first}}``.
 *
 *  * ``{{^-first}}`` - Begin marker of a template that will be instantiated *for each except the first* entry in the layer. This,
 *    this is basically the inversion of ``{{#-first}}``. Use is only allowed between the begin and end marker of the layer.
 *    So between ``{{#result}}`` and ``{{/result}}``, or between ``{{#measurement}}`` and ``{{/measurement}}``.
 *
 *  * ``{{/-first}}`` - End marker for either ``{{#-first}}`` or ``{{^-first}}``.
 *
 *  * ``{{#-last}}`` - Begin marker of a template that will be instantiated *only for the last* entry in the layer. Use is only
 *    allowed between the begin and end marker of the layer. So between ``{{#result}}`` and ``{{/result}}``, or between
 *    ``{{#measurement}}`` and ``{{/measurement}}``. Finish the template with ``{{/-last}}``.
 *
 *  * ``{{^-last}}`` - Begin marker of a template that will be instantiated *for each except the last* entry in the layer. This,
 *    this is basically the inversion of ``{{#-last}}``. Use is only allowed between the begin and end marker of the layer.
 *    So between ``{{#result}}`` and ``{{/result}}``, or between ``{{#measurement}}`` and ``{{/measurement}}``.
 *
 *  * ``{{/-last}}`` - End marker for either ``{{#-last}}`` or ``{{^-last}}``.
 *
   @verbatim embed:rst

   For an overview of all the possible data you can get out of nanobench, please see the tutorial at :ref:`tutorial-template-json`.

   The templates that ship with nanobench are:

   * :cpp:func:`templates::csv() <ankerl::nanobench::templates::csv()>`
   * :cpp:func:`templates::json() <ankerl::nanobench::templates::json()>`
   * :cpp:func:`templates::htmlBoxplot() <ankerl::nanobench::templates::htmlBoxplot()>`
   * :cpp:func:`templates::pyperf() <ankerl::nanobench::templates::pyperf()>`

   @endverbatim
 *
 * @param mustacheTemplate The template.
 * @param bench Benchmark, containing all the results.
 * @param out Output for the generated output.
 */
void render(char const* mustacheTemplate, Bench const& bench, std::ostream& out);
void render(std::string const& mustacheTemplate, Bench const& bench, std::ostream& out);

/**
 * Same as render(char const* mustacheTemplate, Bench const& bench, std::ostream& out), but for when
 * you only have results available.
 *
 * @param mustacheTemplate The template.
 * @param results All the results to be used for rendering.
 * @param out Output for the generated output.
 */
void render(char const* mustacheTemplate, std::vector<Result> const& results, std::ostream& out);
void render(std::string const& mustacheTemplate, std::vector<Result> const& results, std::ostream& out);

// Contains mustache-like templates
namespace templates {

/*!
  @brief CSV data for the benchmark results.

  Generates a comma-separated values dataset. First line is the header, each following line is a summary of each benchmark run.

  @verbatim embed:rst
  See the tutorial at :ref:`tutorial-template-csv` for an example.
  @endverbatim
 */
char const* csv() noexcept;

/*!
  @brief HTML output that uses plotly to generate an interactive boxplot chart. See the tutorial for an example output.

  The output uses only the elapsed wall clock time, and displays each epoch as a single dot.
  @verbatim embed:rst
  See the tutorial at :ref:`tutorial-template-html` for an example.
  @endverbatim

  @see also ankerl::nanobench::render()
 */
char const* htmlBoxplot() noexcept;

/*!
 @brief Output in pyperf compatible JSON format, which can be used for more analyzation.
 @verbatim embed:rst
 See the tutorial at :ref:`tutorial-template-pyperf` for an example how to further analyze the output.
 @endverbatim
 */
char const* pyperf() noexcept;

/*!
  @brief Template to generate JSON data.

  The generated JSON data contains *all* data that has been generated. All times are as double values, in seconds. The output can get
  quite large.
  @verbatim embed:rst
  See the tutorial at :ref:`tutorial-template-json` for an example.
  @endverbatim
 */
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

ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
struct Config {
    // actual benchmark config
    std::string mBenchmarkTitle = "benchmark";                               // NOLINT(misc-non-private-member-variables-in-classes)
    std::string mBenchmarkName = "noname";                                   // NOLINT(misc-non-private-member-variables-in-classes)
    std::string mUnit = "op";                                                // NOLINT(misc-non-private-member-variables-in-classes)
    double mBatch = 1.0;                                                     // NOLINT(misc-non-private-member-variables-in-classes)
    double mComplexityN = -1.0;                                              // NOLINT(misc-non-private-member-variables-in-classes)
    size_t mNumEpochs = 11;                                                  // NOLINT(misc-non-private-member-variables-in-classes)
    size_t mClockResolutionMultiple = static_cast<size_t>(1000);             // NOLINT(misc-non-private-member-variables-in-classes)
    std::chrono::nanoseconds mMaxEpochTime = std::chrono::milliseconds(100); // NOLINT(misc-non-private-member-variables-in-classes)
    std::chrono::nanoseconds mMinEpochTime = std::chrono::milliseconds(1);   // NOLINT(misc-non-private-member-variables-in-classes)
    uint64_t mMinEpochIterations{1};                                         // NOLINT(misc-non-private-member-variables-in-classes)
    // If not 0, run *exactly* these number of iterations per epoch.
    uint64_t mEpochIterations{0};                                          // NOLINT(misc-non-private-member-variables-in-classes)
    uint64_t mWarmup = 0;                                                  // NOLINT(misc-non-private-member-variables-in-classes)
    std::ostream* mOut = nullptr;                                          // NOLINT(misc-non-private-member-variables-in-classes)
    std::chrono::duration<double> mTimeUnit = std::chrono::nanoseconds{1}; // NOLINT(misc-non-private-member-variables-in-classes)
    std::string mTimeUnitName = "ns";                                      // NOLINT(misc-non-private-member-variables-in-classes)
    bool mShowPerformanceCounters = true;                                  // NOLINT(misc-non-private-member-variables-in-classes)
    bool mIsRelative = false;                                              // NOLINT(misc-non-private-member-variables-in-classes)
    std::unordered_map<std::string, std::string> mContext{};               // NOLINT(misc-non-private-member-variables-in-classes)
    // One bit per Column, set when that column is hidden - so the default of 0 shows everything and
    // stays the table nanobench has always printed.
    uint32_t mHiddenColumns{};                  // NOLINT(misc-non-private-member-variables-in-classes)
    std::vector<std::string> mContextColumns{}; // NOLINT(misc-non-private-member-variables-in-classes)

    Config();
    ~Config();
    Config& operator=(Config const& other);
    Config& operator=(Config&& other) noexcept(ANKERL_NANOBENCH(NOEXCEPT_STRING_MOVE));
    Config(Config const& other);
    Config(Config&& other) noexcept;
};
ANKERL_NANOBENCH(IGNORE_PADDED_POP)

// Result returned after a benchmark has finished. Can be used as a baseline for relative().
ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
class Result {
public:
    enum class Measure : size_t {
        elapsed,
        iterations,
        pagefaults,
        cpucycles,
        contextswitches,
        instructions,
        branchinstructions,
        branchmisses,

        /// Number of measures, and what fromString() returns for a name it does not know. Passing it
        /// to the accessors below is not an error: it reads as a measure that was never recorded.
        _size
    };

    explicit Result(Config benchmarkConfig);

    ~Result();
    Result& operator=(Result const& other);
    Result& operator=(Result&& other) noexcept(ANKERL_NANOBENCH(NOEXCEPT_STRING_MOVE));
    Result(Result const& other);
    Result(Result&& other) noexcept;

    // adds new measurement results
    // all values are scaled by iters (except iters...)
    void add(Clock::duration totalElapsed, uint64_t iters, detail::PerformanceCounters const& pc);

    ANKERL_NANOBENCH(NODISCARD) Config const& config() const noexcept;

    ANKERL_NANOBENCH(NODISCARD) double median(Measure m) const;
    ANKERL_NANOBENCH(NODISCARD) double medianAbsolutePercentError(Measure m) const;
    ANKERL_NANOBENCH(NODISCARD) double average(Measure m) const;
    ANKERL_NANOBENCH(NODISCARD) double sum(Measure m) const noexcept;
    ANKERL_NANOBENCH(NODISCARD) double sumProduct(Measure m1, Measure m2) const noexcept;
    ANKERL_NANOBENCH(NODISCARD) double minimum(Measure m) const noexcept;
    ANKERL_NANOBENCH(NODISCARD) double maximum(Measure m) const noexcept;
    ANKERL_NANOBENCH(NODISCARD) std::string const& context(char const* variableName) const;
    ANKERL_NANOBENCH(NODISCARD) std::string const& context(std::string const& variableName) const;

    ANKERL_NANOBENCH(NODISCARD) bool has(Measure m) const noexcept;
    ANKERL_NANOBENCH(NODISCARD) double get(size_t idx, Measure m) const;
    ANKERL_NANOBENCH(NODISCARD) bool empty() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) size_t size() const noexcept;

    // Finds string, if not found, returns _size.
    static Measure fromString(std::string const& str);

private:
    Config mConfig{};
    std::vector<std::vector<double>> mNameToMeasurements{};
};
ANKERL_NANOBENCH(IGNORE_PADDED_POP)

/**
 * @brief The outcome of a paired comparison, see Bench::compare().
 *
   @verbatim embed:rst
   :cpp:func:`relative() <ankerl::nanobench::Bench::relative()>` compares the median of one benchmark
   run entirely before another, which measures the machine's drift as much as the code: nanobench's
   own test suite has a case where two *identical* workloads came out 38% apart on a CI runner, while
   each reported an ``err%`` of 0.5. Whatever ``err%`` is measuring there, it is not the uncertainty
   of the comparison.

   A paired comparison measures the alternatives against each other within the same slice of time, so
   anything that affects all of them - a frequency ramp, a noisy neighbour, thermal throttling -
   cancels out of the ratios. What is reported is therefore an uncertainty about *the ratio*, which
   is the number the caller actually wanted.

   Entry 0 is the baseline that everything else is measured against.
   @endverbatim
 */
ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
class CompareResult {
public:
    /// One alternative: its measurements, and how it compares to the baseline.
    ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
    struct Entry {
        Entry(std::string entryName, Result entryResult, double entryRelative, double entryRelativeLow, double entryRelativeHigh,
              size_t entryTiedRounds);

        std::string name;    // NOLINT(misc-non-private-member-variables-in-classes)
        Result result;       // NOLINT(misc-non-private-member-variables-in-classes)
        double relative;     // NOLINT(misc-non-private-member-variables-in-classes)
        double relativeLow;  // NOLINT(misc-non-private-member-variables-in-classes)
        double relativeHigh; // NOLINT(misc-non-private-member-variables-in-classes)
        size_t tiedRounds;   // NOLINT(misc-non-private-member-variables-in-classes)
    };
    ANKERL_NANOBENCH(IGNORE_PADDED_POP)

    CompareResult(std::vector<Entry> entries, size_t numRounds);

    /// Number of alternatives compared, including the baseline.
    ANKERL_NANOBENCH(NODISCARD) size_t size() const noexcept;

    /// Entry 0 is the baseline.
    ANKERL_NANOBENCH(NODISCARD) Entry const& operator[](size_t idx) const;

    /// Number of paired rounds each alternative was measured over.
    ANKERL_NANOBENCH(NODISCARD) size_t rounds() const noexcept;

    /**
     * @brief How many comparisons the intervals were corrected for: one per alternative besides the
     *        baseline.
     *
       @verbatim embed:rst
       With ten alternatives there are nine chances to be wrong at 5% each, so the intervals are
       widened to keep the *whole table* at 95% rather than each row separately. See
       :ref:`ab-comparison` for what that costs - less than it sounds, because the binomial tail is
       steep.
       @endverbatim
     */
    ANKERL_NANOBENCH(NODISCARD) size_t comparisons() const noexcept;

    /**
     * @brief True when this alternative's interval excludes the baseline, i.e. the measurement told
     *        them apart. Always false for the baseline itself.
     */
    ANKERL_NANOBENCH(NODISCARD) bool isSignificant(size_t idx) const;

    /// Index of the alternative with the lowest median time.
    ANKERL_NANOBENCH(NODISCARD) size_t fastest() const;

private:
    std::vector<Entry> mEntries{};
    size_t mRounds{};
};
ANKERL_NANOBENCH(IGNORE_PADDED_POP)

/// Writes the comparison as a markdown table, in the same shape as an ordinary benchmark table, with
/// a summary line under it.
std::ostream& operator<<(std::ostream& os, CompareResult const& compareResult);

/**
 * An extremely fast random generator. Currently, this implements *RomuDuoJr*, developed by Mark Overton. Source:
 * http://www.romu-random.org/
 *
 * RomuDuoJr is extremely fast and provides reasonable good randomness. Not enough for large jobs, but definitely
 * good enough for a benchmarking framework.
 *
 *  * Estimated capacity: @f$ 2^{51} @f$ bytes
 *  * Register pressure: 4
 *  * State size: 128 bits
 *
 * This random generator is a drop-in replacement for the generators supplied by ``<random>``. It is not
 * cryptographically secure. It's intended purpose is to be very fast so that benchmarks that make use
 * of randomness are not distorted too much by the random generator.
 *
 * Rng also provides a few non-standard helpers, optimized for speed.
 */
class Rng final {
public:
    /**
     * @brief This RNG provides 64bit randomness.
     */
    using result_type = uint64_t;

    static constexpr uint64_t(min)();
    static constexpr uint64_t(max)();

    /**
     * As a safety precaution, we don't allow copying. Copying a PRNG would mean you would have two random generators that produce the
     * same sequence, which is generally not what one wants. Instead create a new rng with the default constructor Rng(), which is
     * automatically seeded from `std::random_device`. If you really need a copy, use `copy()`.
     */
    Rng(Rng const&) = delete;

    /**
     * Same as Rng(Rng const&), we don't allow assignment. If you need a new Rng create one with the default constructor Rng().
     */
    Rng& operator=(Rng const&) = delete;

    // moving is ok
    Rng(Rng&&) noexcept = default;
    Rng& operator=(Rng&&) noexcept = default;
    ~Rng() noexcept = default;

    /**
     * @brief Creates a new Random generator with random seed.
     *
     * Instead of a default seed (as the random generators from the STD), this properly seeds the random generator from
     * `std::random_device`. It guarantees correct seeding. Note that seeding can be relatively slow, depending on the source of
     * randomness used. So it is best to create a Rng once and use it for all your randomness purposes.
     */
    Rng();

    /*!
      Creates a new Rng that is seeded with a specific seed. Each Rng created from the same seed will produce the same randomness
      sequence. This can be useful for deterministic behavior.

      @verbatim embed:rst
      .. note::

         The random algorithm might change between nanobench releases. Whenever a faster and/or better random
         generator becomes available, I will switch the implementation.
      @endverbatim

      As per the Romu paper, this seeds the Rng with splitMix64 algorithm and performs 10 initial rounds for further mixing up of the
      internal state.

      @param seed  The 64bit seed. All values are allowed, even 0.
     */
    explicit Rng(uint64_t seed) noexcept;
    Rng(uint64_t x, uint64_t y) noexcept;
    explicit Rng(std::vector<uint64_t> const& data);

    /**
     * Creates a copy of the Rng, thus the copy provides exactly the same random sequence as the original.
     */
    ANKERL_NANOBENCH(NODISCARD) Rng copy() const noexcept;

    /**
     * @brief Produces a 64bit random value. This should be very fast, thus it is marked as inline. In my benchmark, this is ~46 times
     * faster than `std::default_random_engine` for producing 64bit random values. It seems that the fastest std contender is
     * `std::mt19937_64`. Still, this RNG is 2-3 times as fast.
     *
     * @return uint64_t The next 64 bit random value.
     */
    inline uint64_t operator()() noexcept;

    // This is slightly biased. See

    /**
     * Generates a random number between 0 and range (excluding range).
     *
     * The algorithm only produces 32bit numbers, and is slightly biased. The effect is quite small unless your range is close to the
     * maximum value of an integer. It is possible to correct the bias with rejection sampling (see
     * [here](https://lemire.me/blog/2016/06/30/fast-random-shuffling/), but this is most likely irrelevant in practices for the
     * purposes of this Rng.
     *
     * See Daniel Lemire's blog post [A fast alternative to the modulo
     * reduction](https://lemire.me/blog/2016/06/27/a-fast-alternative-to-the-modulo-reduction/)
     *
     * @param range Upper exclusive range. E.g a value of 3 will generate random numbers 0, 1, 2.
     * @return uint32_t Generated random values in range [0, range(.
     */
    inline uint32_t bounded(uint32_t range) noexcept;

    // random double in range [0, 1(
    // see http://prng.di.unimi.it/

    /**
     * Provides a random uniform double value between 0 and 1. This uses the method described in [Generating uniform doubles in the
     * unit interval](http://prng.di.unimi.it/), and is extremely fast.
     *
     * @return double Uniformly distributed double value in range [0,1(, excluding 1.
     */
    inline double uniform01() noexcept;

    /**
     * Shuffles all entries in the given container. Although this has a slight bias due to the implementation of bounded(), this is
     * preferable to `std::shuffle` because it is over 5 times faster. See Daniel Lemire's blog post [Fast random
     * shuffling](https://lemire.me/blog/2016/06/30/fast-random-shuffling/).
     *
     * @param container The whole container will be shuffled.
     */
    template <typename Container>
    void shuffle(Container& container) noexcept;

    /**
     * Extracts the full state of the generator, e.g. for serialization. For this RNG this is just 2 values, but to stay API compatible
     * with future implementations that potentially use more state, we use a vector.
     *
     * @return Vector containing the full state:
     */
    ANKERL_NANOBENCH(NODISCARD) std::vector<uint64_t> state() const;

private:
    static constexpr uint64_t rotl(uint64_t x, unsigned k) noexcept;

    uint64_t mX;
    uint64_t mY;
};

/**
 * @brief A column of the markdown table, for Bench::hideColumn() and Bench::showColumn().
 *
 * The names in brackets are the headers the column is printed with; `op` follows Bench::unit(), and the
 * time unit follows Bench::timeUnit().
 *
 * @see ankerl::nanobench::Bench::hideColumn()
 */
enum class Column : size_t {
    relative,      ///< `relative` - only shown when Bench::relative() is set.
    complexityN,   ///< `complexityN` - only shown when Bench::complexityN() is set.
    timePerUnit,   ///< `ns/op` - time for one unit.
    unitPerSecond, ///< `op/s` - units per second.
    error,         ///< `err%` - median absolute percentage error over the epochs.
    instructions,  ///< `ins/op` - retired instructions, Linux only.
    cycles,        ///< `cyc/op` - CPU cycles, Linux only.
    ipc,           ///< `IPC` - instructions per cycle, Linux only.
    branches,      ///< `bra/op` - retired branch instructions, Linux only.
    branchMisses,  ///< `miss%` - percentage of branches mispredicted, Linux only.
    total,         ///< `total` - wall clock time spent measuring this row.
    _size          ///< Not a column; the number of them.
};

/**
 * @brief Main entry point to nanobench's benchmarking facility.
 *
 * It holds configuration and results from one or more benchmark runs. Usually it is used in a single line, where the object is
 * constructed, configured, and then a benchmark is run. E.g. like this:
 *
 *     ankerl::nanobench::Bench().unit("byte").batch(1000).run("random fluctuations", [&] {
 *         // here be the benchmark code
 *     });
 *
 * In that example Bench() constructs the benchmark, it is then configured with unit() and batch(), and after configuration a
 * benchmark is executed with run(). Once run() has finished, it prints the result to `std::cout`. It would also store the results
 * in the Bench instance, but in this case the object is immediately destroyed so it's not available any more.
 */
ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
class Bench {
public:
    /**
     * @brief Creates a new benchmark for configuration and running of benchmarks.
     */
    Bench();

    Bench(Bench&& other) noexcept;
    Bench& operator=(Bench&& other) noexcept(ANKERL_NANOBENCH(NOEXCEPT_STRING_MOVE));
    Bench(Bench const& other);
    Bench& operator=(Bench const& other);
    ~Bench() noexcept;

    /*!
      @brief Repeatedly calls `op()` based on the configuration, and performs measurements.

      This call is marked with `noinline` to prevent the compiler to optimize beyond different benchmarks. This can have quite a big
      effect on benchmark accuracy.

      @verbatim embed:rst
      .. note::

        Each call to your lambda must have a side effect that the compiler can't possibly optimize it away. E.g. add a result to an
        externally defined number (like `x` in the above example), and finally call `doNotOptimizeAway` on the variables the compiler
        must not remove. You can also use :cpp:func:`ankerl::nanobench::doNotOptimizeAway` directly in the lambda, but be aware that
        this has a small overhead.

      @endverbatim

      @tparam Op The code to benchmark.
     */
    template <typename Op>
    ANKERL_NANOBENCH(NOINLINE)
    Bench& run(char const* benchmarkName, Op&& op);

    template <typename Op>
    ANKERL_NANOBENCH(NOINLINE)
    Bench& run(std::string const& benchmarkName, Op&& op);

#if ANKERL_NANOBENCH(HAS_STRING_VIEW)
    /// Same as run(char const* benchmarkName, Op op), for a std::string_view name. Only available
    /// when compiling as C++17 or newer.
    template <typename Op>
    ANKERL_NANOBENCH(NOINLINE)
    Bench& run(std::string_view benchmarkName, Op&& op);
#endif

    /**
     * @brief Same as run(char const* benchmarkName, Op op), but instead uses the previously set name.
     * @tparam Op The code to benchmark.
     */
    template <typename Op>
    ANKERL_NANOBENCH(NOINLINE)
    Bench& run(Op&& op);

    /**
     * @brief Compares alternatives against each other, paired and interleaved.
     *
     * Takes `name, op` pairs - two or more of them. The first is the baseline everything else is
     * measured against, exactly as with relative(), but measured against it *at the same time*
     * rather than one after the other. Drift the machine introduces - a frequency ramp, a noisy
     * neighbour, thermal throttling - then hits every alternative alike and cancels out of the
     * ratios.
     *
     * @code
     * ankerl::nanobench::Bench().epochs(52).compare(
     *     "std::mt19937", [&] { ... },     // the baseline
     *     "sfc4",         [&] { ... },
     *     "romu",         [&] { ... });
     * @endcode
     *
     * Prints an ordinary nanobench table with two extra columns - the ratio to the baseline and a
     * confidence interval for it - and a summary line underneath.
     *
     @verbatim embed:rst
     A worked example with output, and the reasoning behind each choice below, is in the tutorial at
     :ref:`ab-comparison`.

     What it does, in order:

     #. Calibrates an iteration count once, up front, and uses the **same** count for every
        alternative for the whole run. An epoch's fixed overhead is divided by that count, so two
        different counts would amortize it differently and bias the ratio - 1.2% on 200us epochs, and
        no amount of pairing removes it.
     #. Runs one epoch of each per round, interleaved, so drift is common to the round.
     #. Orders each block of N rounds as a randomly chosen cyclic Latin square, so every alternative
        occupies every position exactly once per block and their mean positions are equal - which is
        what cancels a drift that is linear over the block. For two alternatives this is exactly
        ABBA/BAAB.
     #. Reduces each round to ``ln(t_baseline) - ln(t_alternative)``, dropping rounds where either
        side measured zero.
     #. Reports the median of those as the ratio, with a sign-test interval around it, widened to
        keep the whole table at 95% rather than each row separately.

     .. note::

        Use more rounds than the default 11. An epoch is about a millisecond, so ``epochs(52)`` costs
        a tenth of a second per alternative and buys an interval narrow enough to act on. The count is
        rounded up to a whole number of blocks, and raised if it is too small to support an interval
        at all.

     .. warning::

        Interleaving means each alternative runs with the others' cache and branch predictor state.
        That is usually the more honest measurement for "which should I ship", but it is a different
        measurement from running one alone - use :cpp:func:`run() <ankerl::nanobench::Bench::run()>`
        for that.

     @endverbatim
     *
     * @tparam Args Alternating names and operations.
     * @param args `name, op` pairs; at least two.
     * @return The ratios and their intervals; also written to output() unless that is nullptr.
     */
    template <typename... Args>
    ANKERL_NANOBENCH(NOINLINE)
    CompareResult compare(Args&&... args);

    /**
     * @brief Title of the benchmark, will be shown in the table header. Changing the title will start a new markdown table.
     *
     * @param benchmarkTitle The title of the benchmark.
     */
    Bench& title(char const* benchmarkTitle);
    Bench& title(std::string const& benchmarkTitle);

    /**
     * @brief Gets the title of the benchmark
     */
    ANKERL_NANOBENCH(NODISCARD) std::string const& title() const noexcept;

    /// Name of the benchmark, will be shown in the table row.
    Bench& name(char const* benchmarkName);
    Bench& name(std::string const& benchmarkName);
#if ANKERL_NANOBENCH(HAS_STRING_VIEW)
    /// Only available when compiling as C++17 or newer. @see name()
    Bench& name(std::string_view benchmarkName);
#endif
    ANKERL_NANOBENCH(NODISCARD) std::string const& name() const noexcept;

    /**
     * @brief Set context information.
     *
     * The information can be accessed using custom render templates via `{{context(variableName)}}`.
     * Trying to render a variable that hasn't been set before raises an exception.
     * Not included in (default) markdown table.
     *
     * @see clearContext, render
     *
     * @param variableName The name of the context variable.
     * @param variableValue The value of the context variable.
     */
    Bench& context(char const* variableName, char const* variableValue);
    Bench& context(std::string const& variableName, std::string const& variableValue);

    /**
     * @brief Reset context information.
     *
     * This may improve efficiency when using many context entries,
     * or improve robustness by removing spurious context entries.
     *
     * @see context
     */
    Bench& clearContext();

    /**
     * @brief Sets the batch size.
     *
     * E.g. number of processed byte, or some other metric for the size of the processed data in each iteration. If you benchmark
     * hashing of a 1000 byte long string and want byte/sec as a result, you can specify 1000 as the batch size.
     *
     * @tparam T Any input type is internally cast to `double`.
     * @param b batch size
     */
    template <typename T>
    Bench& batch(T b) noexcept;
    ANKERL_NANOBENCH(NODISCARD) double batch() const noexcept;

    /**
     * @brief Sets the operation unit.
     *
     * Defaults to "op". Could be e.g. "byte" for string processing. This is used for the table header, e.g. to show `ns/byte`. Use
     * singular (*byte*, not *bytes*). A change clears the currently collected results.
     *
     * @param unit The unit name.
     */
    Bench& unit(char const* unit);
    Bench& unit(std::string const& unit);
    ANKERL_NANOBENCH(NODISCARD) std::string const& unit() const noexcept;

    /**
     * @brief Sets the time unit to be used for the default output.
     *
     * Nanobench defaults to using ns (nanoseconds) as output in the markdown. For some benchmarks this is too coarse, so it is
     * possible to configure this. E.g. use `timeUnit(1ms, "ms")` to show `ms/op` instead of `ns/op`.
     *
     * @param tu Time unit to display the results in, default is 1ns.
     * @param tuName Name for the time unit, default is "ns"
     */
    Bench& timeUnit(std::chrono::duration<double> const& tu, std::string const& tuName);
    ANKERL_NANOBENCH(NODISCARD) std::string const& timeUnitName() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) std::chrono::duration<double> const& timeUnit() const noexcept;

    /**
     * @brief Set the output stream where the resulting markdown table will be printed to.
     *
     * The default is `&std::cout`. You can disable all output by setting `nullptr`.
     *
     * @param outstream Pointer to output stream, can be `nullptr`.
     */
    Bench& output(std::ostream* outstream) noexcept;
    ANKERL_NANOBENCH(NODISCARD) std::ostream* output() const noexcept;

    /**
     * Modern processors have a very accurate clock, being able to measure as low as 20 nanoseconds. This is the main trick nanobech to
     * be so fast: we find out how accurate the clock is, then run the benchmark only so often that the clock's accuracy is good enough
     * for accurate measurements.
     *
     * The default is to run one epoch for 1000 times the clock resolution. So for 20ns resolution and 11 epochs, this gives a total
     * runtime of
     *
     * @f[
     * 20ns * 1000 * 11 \approx 0.2ms
     * @f]
     *
     * To be precise, nanobench adds a 0-20% random noise to each evaluation. This is to prevent any aliasing effects, and further
     * improves accuracy.
     *
     * Total runtime will be higher though: Some initial time is needed to find out the target number of iterations for each epoch, and
     * there is some overhead involved to start & stop timers and calculate resulting statistics and writing the output.
     *
     * @param multiple Target number of times of clock resolution. Usually 1000 is a good compromise between runtime and accuracy.
     */
    Bench& clockResolutionMultiple(size_t multiple) noexcept;
    ANKERL_NANOBENCH(NODISCARD) size_t clockResolutionMultiple() const noexcept;

    /**
     * @brief Controls number of epochs, the number of measurements to perform.
     *
     * The reported result will be the median of evaluation of each epoch. The higher you choose this, the more
     * deterministic the result be and outliers will be more easily removed. Also the `err%` will be more accurate the higher this
     * number is. Note that the `err%` will not necessarily decrease when number of epochs is increased. But it will be a more accurate
     * representation of the benchmarked code's runtime stability.
     *
     * Choose the value wisely. In practice, 11 has been shown to be a reasonable choice between runtime performance and accuracy.
     * This setting goes hand in hand with minEpochIterations() (or minEpochTime()). If you are more interested in *median* runtime,
     * you might want to increase epochs(). If you are more interested in *mean* runtime, you might want to increase
     * minEpochIterations() instead.
     *
     * @param numEpochs Number of epochs.
     */
    Bench& epochs(size_t numEpochs) noexcept;
    ANKERL_NANOBENCH(NODISCARD) size_t epochs() const noexcept;

    /**
     * @brief Upper limit for the runtime of each epoch.
     *
     * As a safety precaution if the clock is not very accurate, we can set an upper limit for the maximum evaluation time per
     * epoch. Default is 100ms. At least a single evaluation of the benchmark is performed.
     *
     * @see minEpochTime, minEpochIterations
     *
     * @param t Maximum target runtime for a single epoch.
     */
    Bench& maxEpochTime(std::chrono::nanoseconds t) noexcept;
    ANKERL_NANOBENCH(NODISCARD) std::chrono::nanoseconds maxEpochTime() const noexcept;

    /**
     * @brief Minimum time each epoch should take.
     *
     * Default is 1ms, so we are mostly relying on clockResolutionMultiple(). In most cases this is exactly what you want. If you see
     * that the evaluation is unreliable with a high `err%`, you can increase either minEpochTime() or minEpochIterations().
     *
     * @see maxEpochTime, minEpochIterations
     *
     * @param t Minimum time each epoch should take.
     */
    Bench& minEpochTime(std::chrono::nanoseconds t) noexcept;
    ANKERL_NANOBENCH(NODISCARD) std::chrono::nanoseconds minEpochTime() const noexcept;

    /**
     * @brief Sets the minimum number of iterations each epoch should take.
     *
     * Default is 1, and we rely on clockResolutionMultiple(). If the `err%` is high and you want a more smooth result, you might want
     * to increase the minimum number of iterations, or increase the minEpochTime().
     *
     * @see minEpochTime, maxEpochTime, minEpochIterations
     *
     * @param numIters Minimum number of iterations per epoch.
     */
    Bench& minEpochIterations(uint64_t numIters) noexcept;
    ANKERL_NANOBENCH(NODISCARD) uint64_t minEpochIterations() const noexcept;

    /**
     * Sets exactly the number of iterations for each epoch. Ignores all other epoch limits. This forces nanobench to use exactly
     * the given number of iterations for each epoch, not more and not less. Default is 0 (disabled).
     *
     * @param numIters Exact number of iterations to use. Set to 0 to disable.
     */
    Bench& epochIterations(uint64_t numIters) noexcept;
    ANKERL_NANOBENCH(NODISCARD) uint64_t epochIterations() const noexcept;

    /**
     * @brief Sets a number of iterations that are initially performed without any measurements.
     *
     * Some benchmarks need a few evaluations to warm up caches / database / whatever access. Normally this should not be needed, since
     * we show the median result so initial outliers will be filtered away automatically. If the warmup effect is large though, you
     * might want to set it. Default is 0.
     *
     * @param numWarmupIters Number of warmup iterations.
     */
    Bench& warmup(uint64_t numWarmupIters) noexcept;
    ANKERL_NANOBENCH(NODISCARD) uint64_t warmup() const noexcept;

    /**
     * @brief Marks the next run as the baseline.
     *
     * Call `relative(true)` to mark the run as the baseline. Successive runs will be compared to this run. Just like all the
     * other columns, the comparison is per unit, so it stays meaningful when the runs use a different Bench::batch:
     *
     * @f[
     * 100\% * \frac{baseline / baselineBatch}{runtime / batch}
     * @f]
     *
     *  * 100% means it is exactly as fast as the baseline
     *  * >100% means it is faster than the baseline. E.g. 200% means the current run is twice as fast as the baseline.
     *  * <100% means it is slower than the baseline. E.g. 50% means it is twice as slow as the baseline.
     *
     * See the tutorial section "Comparing Results" for example usage.
     *
     * @param isRelativeEnabled True to enable processing
     */
    Bench& relative(bool isRelativeEnabled) noexcept;
    ANKERL_NANOBENCH(NODISCARD) bool relative() const noexcept;

    /**
     * @brief Enables/disables performance counters.
     *
     * On Linux nanobench has a powerful feature to use performance counters. This enables counting of retired instructions, count
     * number of branches, missed branches, etc. On default this is enabled, but you can disable it if you don't need that feature.
     *
     * @param showPerformanceCounters True to enable, false to disable.
     */
    Bench& performanceCounters(bool showPerformanceCounters) noexcept;
    ANKERL_NANOBENCH(NODISCARD) bool performanceCounters() const noexcept;

    /**
     * @brief Removes a column from the table.
     *
     * The full table is over 150 characters wide, which wraps in most terminals. Hide what you are not
     * reading:
     *
     *     bench.hideColumn(Column::instructions)
     *          .hideColumn(Column::cycles)
     *          .hideColumn(Column::ipc);
     *
     * Hiding a column only changes what is printed - the measurement still happens, and results() and
     * the render templates are unaffected. To stop *measuring* the performance counters as well, use
     * performanceCounters(false), which hides all five of their columns at once.
     *
     * @param column The column to hide.
     */
    Bench& hideColumn(Column column) noexcept;

    /// Shows a column hidden with hideColumn() again. @see hideColumn()
    Bench& showColumn(Column column) noexcept;

    /// True when @p column is not hidden. A column can still be absent from the table for other
    /// reasons - `relative` without relative(), or the counter columns off Linux. @see hideColumn()
    ANKERL_NANOBENCH(NODISCARD) bool isColumnVisible(Column column) const noexcept;

    /**
     * @brief Adds a column showing the value of a context variable.
     *
     * Context variables set with context() are otherwise only reachable from a render template, which
     * makes parameterised benchmarks awkward to read on the console. This puts one in the table:
     *
     *     bench.context("threads", std::to_string(n))
     *          .contextColumn("threads")
     *          .run(...);
     *
     * The column is added once per name, in the order given, before the benchmark name. A row whose
     * context does not have the variable is left blank.
     *
     * @param variableName Name of the context variable to show. @see context()
     */
    Bench& contextColumn(std::string const& variableName);

    /// Removes all columns added with contextColumn(). @see contextColumn()
    Bench& clearContextColumns() noexcept;

    /**
     * @brief Retrieves all benchmark results collected by the bench object so far.
     *
     * Each call to run() generates a Result that is stored within the Bench instance. This is mostly for advanced users who want to
     * see all the nitty gritty details.
     *
     * @return All results collected so far.
     */
    ANKERL_NANOBENCH(NODISCARD) std::vector<Result> const& results() const noexcept;

    /*!
      @verbatim embed:rst

      Convenience shortcut to :cpp:func:`ankerl::nanobench::doNotOptimizeAway`.

      @endverbatim
     */
    template <typename Arg>
    Bench& doNotOptimizeAway(Arg&& arg);

    /*!
      @verbatim embed:rst

      Sets N for asymptotic complexity calculation, so it becomes possible to calculate `Big O
      <https://en.wikipedia.org/wiki/Big_O_notation>`_ from multiple benchmark evaluations.

      Use :cpp:func:`ankerl::nanobench::Bench::complexityBigO` when the evaluation has finished. See the tutorial
      :ref:`asymptotic-complexity` for details.

      @endverbatim

      @tparam T Any type is cast to `double`.
      @param n Length of N for the next benchmark run, so it is possible to calculate `bigO`.
     */
    template <typename T>
    Bench& complexityN(T n) noexcept;
    ANKERL_NANOBENCH(NODISCARD) double complexityN() const noexcept;

    /*!
      Calculates [Big O](https://en.wikipedia.org/wiki/Big_O_notation>) of the results with all preconfigured complexity functions.
      Currently these complexity functions are fitted into the benchmark results:

       @f$ \mathcal{O}(1) @f$,
       @f$ \mathcal{O}(n) @f$,
       @f$ \mathcal{O}(\log{}n) @f$,
       @f$ \mathcal{O}(n\log{}n) @f$,
       @f$ \mathcal{O}(n^2) @f$,
       @f$ \mathcal{O}(n^3) @f$.

      If we e.g. evaluate the complexity of `std::sort`, this is the result of `std::cout << bench.complexityBigO()`:

      ```
      |   coefficient |   err% | complexity
      |--------------:|-------:|------------
      |   5.08935e-09 |   2.6% | O(n log n)
      |   6.10608e-08 |   8.0% | O(n)
      |   1.29307e-11 |  47.2% | O(n^2)
      |   2.48677e-15 |  69.6% | O(n^3)
      |   9.88133e-06 | 132.3% | O(log n)
      |   5.98793e-05 | 162.5% | O(1)
      ```

      So in this case @f$ \mathcal{O}(n\log{}n) @f$ provides the best approximation.

      @verbatim embed:rst
      See the tutorial :ref:`asymptotic-complexity` for details.
      @endverbatim
      @return Evaluation results, which can be printed or otherwise inspected.
     */
    std::vector<BigO> complexityBigO() const;

    /**
     * @brief Calculates bigO for a custom function.
     *
     * E.g. to calculate the mean squared error for @f$ \mathcal{O}(\log{}\log{}n) @f$, which is not part of the default set of
     * complexityBigO(), you can do this:
     *
     * ```
     * auto logLogN = bench.complexityBigO("O(log log n)", [](double n) {
     *     return std::log2(std::log2(n));
     * });
     * ```
     *
     * The resulting mean squared error can be printed with `std::cout << logLogN`. E.g. it prints something like this:
     *
     * ```text
     * 2.46985e-05 * O(log log n), rms=1.48121
     * ```
     *
     * @tparam Op Type of mapping operation.
     * @param name Name for the function, e.g. "O(log log n)"
     * @param op Op's operator() maps a `double` with the desired complexity function, e.g. `log2(log2(n))`.
     * @return BigO Error calculation, which is streamable to std::cout.
     */
    template <typename Op>
    BigO complexityBigO(char const* name, Op op) const;

    template <typename Op>
    BigO complexityBigO(std::string const& name, Op op) const;

    /*!
      @verbatim embed:rst

      Convenience shortcut to :cpp:func:`ankerl::nanobench::render`.

      @endverbatim
     */
    Bench& render(char const* templateContent, std::ostream& os);
    Bench& render(std::string const& templateContent, std::ostream& os);

    Bench& config(Config const& benchmarkConfig);
    ANKERL_NANOBENCH(NODISCARD) Config const& config() const noexcept;

    /*!
      @brief Runs `setupOp()` once before each epoch, without measuring it.

      Use this to restore whatever state your benchmark consumes, when that restoration would otherwise
      pollute the measurement:

      @code
      bench.setup([&] { data = pristine; })
           .run("consume data", [&] { consume(data); });
      @endcode

      @verbatim embed:rst
      .. important::

         The setup runs **once per epoch, not once per iteration**. An epoch calls ``op()`` many times
         (see :cpp:func:`epochIterations() <ankerl::nanobench::Bench::epochIterations()>`), and the setup
         does not run again in between. So this helps when your operation can be repeated as-is and only
         the *starting* state has to be established - and it does **not** help when every single call
         mutates the data such that the next call would measure something different.

         For that second case, set :cpp:func:`epochIterations(1) <ankerl::nanobench::Bench::epochIterations()>`
         so an epoch is a single call, which makes setup effectively per-iteration - at the cost of much
         noisier results, since one call is then timed against the clock's resolution. The alternative,
         and usually the better measurement, is to time the setup separately and subtract it.

         Timers are deliberately not started and stopped around each iteration: for anything fast that
         costs more than the thing being measured, and the performance counters would have to be
         restarted too.

      .. note::

         The returned object keeps a reference to this ``Bench``, so don't let it outlive it - call
         ``run()`` on the same expression, as above.

      @endverbatim

      @tparam SetupOp The untimed code to run before each epoch.
      @param setupOp The setup to run.
     */
    template <typename SetupOp>
    detail::SetupRunner<SetupOp> setup(SetupOp setupOp);

private:
    // Collects the `name, op` pack into two flat vectors, once. Everything after this point is
    // ordinary index-based code in the implementation block rather than more template recursion.
    static void compareCollect(std::vector<std::string>& /*names*/, std::vector<detail::ErasedOp>& /*ops*/) {}

    template <typename Name, typename Op, typename... Rest>
    static void compareCollect(std::vector<std::string>& names, std::vector<detail::ErasedOp>& ops, Name&& name, Op&& op,
                               Rest&&... rest) {
        names.emplace_back(std::forward<Name>(name));
        ops.push_back(detail::eraseOp(op));
        compareCollect(names, ops, std::forward<Rest>(rest)...);
    }

    // The measuring half, which no longer needs to know the operations' types.
    ANKERL_NANOBENCH(NODISCARD)
    CompareResult compareImpl(std::vector<std::string> const& names, std::vector<detail::ErasedOp> const& ops);

    // Number of iterations that makes one epoch of `op` last about as long as a normal epoch would.
    // compare() keeps this fixed for the whole experiment: an iteration count that drifted between
    // rounds would be a second thing changing while the comparison is being made.
    ANKERL_NANOBENCH(NODISCARD)
    uint64_t compareCalibrate(detail::ErasedOp const& op) const;

    // Runs `numIters` iterations of `op` as one epoch, appending the measurement to `result`.
    static void compareEpoch(detail::ErasedOp const& op, uint64_t numIters, Result& result);

    template <typename SetupOp, typename Op>
    Bench& runImpl(SetupOp& setupOp, Op&& op);

    template <typename SetupOp>
    friend class detail::SetupRunner;

    Config mConfig{};
    std::vector<Result> mResults{};
};
ANKERL_NANOBENCH(IGNORE_PADDED_POP)

/**
 * @brief Makes sure none of the given arguments are optimized away by the compiler.
 *
 * @tparam Arg Type of the argument that shouldn't be optimized away.
 * @param arg The input that we mark as being used, even though we don't do anything with it.
 */
template <typename Arg>
void doNotOptimizeAway(Arg&& arg);

namespace detail {

#if !ANKERL_NANOBENCH(ASM_DONT_OPTIMIZE_AWAY)
void doNotOptimizeAwaySink(void const*);

template <typename T>
void doNotOptimizeAway(T const& val);

#else

// These assembly magic is directly from what Google Benchmark is doing. I have previously used what facebook's folly was doing, but
// this seemed to have compilation problems in some cases. Google Benchmark seemed to be the most well tested anyways.
// see https://github.com/google/benchmark/blob/v1.7.1/include/benchmark/benchmark.h#L443-L446
template <typename T>
void doNotOptimizeAway(T const& val) {
    // NOLINTNEXTLINE(hicpp-no-assembler)
    asm volatile("" : : "r,m"(val) : "memory");
}

template <typename T>
void doNotOptimizeAway(T& val) {
#    if defined(__clang__)
    // NOLINTNEXTLINE(hicpp-no-assembler)
    asm volatile("" : "+r,m"(val) : : "memory");
#    else
    // NOLINTNEXTLINE(hicpp-no-assembler)
    asm volatile("" : "+m,r"(val) : : "memory");
#    endif
}
#endif

// internally used, but visible because run() is templated.
// Not movable/copy-able, so we simply use a pointer instead of unique_ptr. This saves us from
// having to include <memory>, and the template instantiation overhead of unique_ptr which is unfortunately quite significant.
ANKERL_NANOBENCH(IGNORE_EFFCPP_PUSH)
class IterationLogic {
public:
    explicit IterationLogic(Bench const& bench);
    IterationLogic(IterationLogic&&) = delete;
    IterationLogic& operator=(IterationLogic&&) = delete;
    IterationLogic(IterationLogic const&) = delete;
    IterationLogic& operator=(IterationLogic const&) = delete;
    ~IterationLogic();

    ANKERL_NANOBENCH(NODISCARD) uint64_t numIters() const noexcept;
    void add(std::chrono::nanoseconds elapsed, PerformanceCounters const& pc) noexcept;
    void moveResultTo(std::vector<Result>& results) noexcept;

private:
    struct Impl;
    Impl* mPimpl;
};
ANKERL_NANOBENCH(IGNORE_EFFCPP_POP)

ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
class PerformanceCounters {
public:
    PerformanceCounters(PerformanceCounters const&) = delete;
    PerformanceCounters(PerformanceCounters&&) = delete;
    PerformanceCounters& operator=(PerformanceCounters const&) = delete;
    PerformanceCounters& operator=(PerformanceCounters&&) = delete;

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
    PerfCountSet<uint64_t> mVal{};
    PerfCountSet<bool> mHas{};
};
ANKERL_NANOBENCH(IGNORE_PADDED_POP)

// Gets the singleton
PerformanceCounters& performanceCounters();

// How long one epoch should take: the clock's resolution multiplied up until the measurement sits
// well above the clock's own noise, then clamped into [minEpochTime, maxEpochTime]. The minimum is
// applied last, so it wins when the two bounds conflict.
//
// Both run() and compare() size their epochs with this. It is a function rather than a line in each
// of them because it is a rule, and a rule with two copies is a rule with two behaviours.
Clock::duration targetRuntimePerEpoch(Bench const& bench);

// Whether hideColumn() left this column visible. Takes the Config rather than the Bench because the
// table writers only have the Config - every Result carries a copy of the one it was measured under.
ANKERL_NANOBENCH(NODISCARD) bool isColumnVisible(Config const& config, Column column) noexcept;

// The frequency-scaling and pyperf warnings, and the note that the kernel refused the performance
// counters. Declared up here because compare() is a template defined above the implementation block
// and has to be able to call them - a program whose only nanobench call is compare() needs the
// warning at least as much as one that calls run().
void printStabilityInformationOnce(std::ostream* outStream);
void printPerformanceCounterHintOnce(std::ostream* outStream, bool wantsPerformanceCounters);

// The last table settings written to this stream. When they change, a new header is written.
uint64_t& streamHeaderHash(std::ostream& os);

// The arithmetic that turns what the kernel hands back into the numbers of the ins/op, cyc/op, IPC,
// bra/op and miss% columns. It lives out here, rather than inside the Linux-only counter class,
// because it is the part that can be silently wrong: a counter that is quietly 5% low looks exactly
// like a correct one. Out here it is compiled and testable on every platform, while
// perf_event_open() - which cannot be exercised in a container at all - stays where it is.
//
// All of these saturate at 0 rather than wrapping: a correction larger than the measurement means
// the measurement was noise, not that the value is enormous.

// Rounded integer division, for spreading a measured overhead over the iterations it covers.
uint64_t divRounded(uint64_t a, uint64_t divisor) noexcept;

// Index of the i-th event's value in a PERF_FORMAT_GROUP | PERF_FORMAT_ID read: three header words
// (nr, time_enabled, time_running), then a value and an id per event.
size_t perfValueIndex(uint64_t i) noexcept;

// Extrapolates a counter that the kernel could only keep active for part of the measurement, the way
// `perf stat` does. Returns the value unchanged when it was active throughout.
uint64_t scaleMultiplexed(uint64_t value, uint64_t timeEnabled, uint64_t timeRunning) noexcept;

// Subtracts the calibrated overhead of measuring, plus the per-iteration overhead of the loop doing
// the measuring, from a raw counter value. Pass 0 for a correction that does not apply.
uint64_t correctOverhead(uint64_t value, uint64_t measuringOverhead, uint64_t loopOverheadPerIteration, uint64_t numIters) noexcept;

// The loop's own per-iteration cost, from a calibration run of `numIters` operations and one of
// twice as many: whatever the second run did not cost twice over is the loop rather than the work.
uint64_t loopOverheadPerIteration(uint64_t single, uint64_t doubled, uint64_t measuringOverhead, uint64_t numIters) noexcept;

// Removes the branch that the measuring loop itself takes once per iteration, plus the one that ends
// it, from a raw branch count.
uint64_t correctBranchInstructions(uint64_t rawBranchInstructions, uint64_t numIters) noexcept;

// Paired statistics, for Bench::compare(). Pure functions over the per-round measurements, so they are
// testable without running a benchmark at all - which matters, because the alternative is judging a
// confidence interval by looking at it.

// ln(a[i]) - ln(b[i]) for each round. Working in log space turns a speedup, which is multiplicative,
// into a difference, which is what every statistic below assumes. Rounds where either side measured
// 0 - a clock too coarse for the operation - carry no ratio and are dropped.
std::vector<double> pairedLogRatios(std::vector<double> const& a, std::vector<double> const& b);

// The same, over two Results' per-round elapsed times. compare() runs one epoch of every alternative
// per round, so their measurements line up round for round and can be paired directly - not only
// against the baseline, but between any two of them.
std::vector<double> pairedLogRatios(Result const& a, Result const& b);

// Median of a copy of the values. 0.0 when there are none.
double medianOf(std::vector<double> values);

// The pair of 0-based order statistics that bracket the median with at least `confidence`
// probability: the largest k with P(Bin(n, 1/2) < k) <= (1 - confidence) / 2 gives the interval
// [x_(k), x_(n+1-k)]. Returns {1, 0} - an empty range - when no such interval exists, which is the
// case for fewer than six observations at 95%.
std::pair<size_t, size_t> medianIntervalIndices(size_t n, double confidence);

// Rounds whose two sides measured the same time to the last tick the clock could report, i.e. a log
// ratio of exactly 0. Not evidence of equality - evidence that the clock ran out of resolution.
size_t countTiedRounds(std::vector<double> const& logRatios);

// Confidence each individual interval is built at, so that all `numComparisons` of them together
// hold at 95%. Bonferroni: simple, assumption-free, and conservative here because the comparisons
// share a baseline and are therefore correlated. One function, because the table and the summary
// line underneath it must not be able to report intervals at two different confidences.
double bonferroniConfidence(size_t numComparisons) noexcept;

// Distribution-free confidence interval for the median, from those order statistics.
//
// The sign test, and it is chosen for what it does *not* assume. It needs the observations to be
// independent and nothing else - no shape, no symmetry, no finite variance, no asymptotics - and it
// is exact at every n rather than approximately right for large ones. The alternatives all want
// more: a t-interval wants normality, a bootstrap wants its own asymptotics and converges slowly
// for a median in particular, and Wilcoxon wants the differences to be symmetric about their
// median. That last one is exactly what paired timings do not give you, because an operation can be
// arbitrarily slower but never faster than its floor, so the differences are skewed whenever one
// side has the heavier tail. Measured on such data: sign 96.7% coverage of a nominal 95%, Wilcoxon
// 91.5%.
//
// The price is width - roughly 10% wider than a bootstrap - and slight over-coverage from the
// discreteness. Both err towards saying "no difference resolved", which is the right direction for
// a tool whose output ends up in a pull request.
std::pair<double, double> medianInterval(std::vector<double> values, double confidence);

// Branch misses cannot exceed the branches they were taken from, and the loop is assumed to mispredict
// its own exit once - so at least one miss is always attributed to it.
double correctBranchMisses(uint64_t rawBranchMisses, double correctedBranchInstructions) noexcept;

} // namespace detail

class BigO {
public:
    using RangeMeasure = std::vector<std::pair<double, double>>;

    template <typename Op>
    static RangeMeasure mapRangeMeasure(RangeMeasure data, Op op) {
        for (auto& rangeMeasure : data) {
            rangeMeasure.first = op(rangeMeasure.first);
        }
        return data;
    }

    static RangeMeasure collectRangeMeasure(std::vector<Result> const& results);

    template <typename Op>
    BigO(char const* bigOName, RangeMeasure const& rangeMeasure, Op rangeToN)
        : BigO(bigOName, mapRangeMeasure(rangeMeasure, rangeToN)) {}

    template <typename Op>
    BigO(std::string bigOName, RangeMeasure const& rangeMeasure, Op rangeToN)
        : BigO(std::move(bigOName), mapRangeMeasure(rangeMeasure, rangeToN)) {}

    BigO(char const* bigOName, RangeMeasure const& scaledRangeMeasure);
    BigO(std::string bigOName, RangeMeasure const& scaledRangeMeasure);
    ANKERL_NANOBENCH(NODISCARD) std::string const& name() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) double constant() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) double normalizedRootMeanSquare() const noexcept;
    ANKERL_NANOBENCH(NODISCARD) bool operator<(BigO const& other) const noexcept;

private:
    std::string mName{};
    double mConstant{};
    double mNormalizedRootMeanSquare{};
};
std::ostream& operator<<(std::ostream& os, BigO const& bigO);
std::ostream& operator<<(std::ostream& os, std::vector<ankerl::nanobench::BigO> const& bigOs);

} // namespace nanobench
} // namespace ankerl

// implementation /////////////////////////////////////////////////////////////////////////////////

namespace ankerl {
namespace nanobench {

constexpr uint64_t(Rng::min)() {
    return 0;
}

constexpr uint64_t(Rng::max)() {
    return (std::numeric_limits<uint64_t>::max)();
}

ANKERL_NANOBENCH_NO_SANITIZE("integer", "undefined")
uint64_t Rng::operator()() noexcept {
    auto x = mX;

    mX = UINT64_C(15241094284759029579) * mY;
    mY = rotl(mY - x, 27);

    return x;
}

ANKERL_NANOBENCH_NO_SANITIZE("integer", "undefined")
uint32_t Rng::bounded(uint32_t range) noexcept {
    uint64_t const r32 = static_cast<uint32_t>(operator()());
    auto multiresult = r32 * range;
    return static_cast<uint32_t>(multiresult >> 32U);
}

double Rng::uniform01() noexcept {
    auto i = (UINT64_C(0x3ff) << 52U) | (operator()() >> 12U);
    // can't use union in c++ here for type puning, it's undefined behavior.
    // std::memcpy is optimized anyways.
    double d{};
    std::memcpy(&d, &i, sizeof(double));
    return d - 1.0;
}

template <typename Container>
void Rng::shuffle(Container& container) noexcept {
    auto i = container.size();
    while (i > 1U) {
        using std::swap;
        auto n = operator()();
        // using decltype(i) instead of size_t to be compatible to containers with 32bit index (see #80)
        auto b1 = static_cast<decltype(i)>((static_cast<uint32_t>(n) * static_cast<uint64_t>(i)) >> 32U);
        swap(container[--i], container[b1]);

        auto b2 = static_cast<decltype(i)>(((n >> 32U) * static_cast<uint64_t>(i)) >> 32U);
        swap(container[--i], container[b2]);
    }
}

ANKERL_NANOBENCH_NO_SANITIZE("integer", "undefined")
constexpr uint64_t Rng::rotl(uint64_t x, unsigned k) noexcept {
    return (x << k) | (x >> (64U - k));
}

namespace detail {

// A setup lambda that captures little - or nothing - is smaller than the alignment of the Bench
// reference next to it, so this pads. That is unavoidable here and harmless, but a consumer building
// with -Weverything sees it as an error in nanobench's own header.
ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
template <typename SetupOp>
class SetupRunner {
public:
    explicit SetupRunner(SetupOp setupOp, Bench& bench)
        : mSetupOp(std::move(setupOp))
        , mBench(bench) {}

    template <typename Op>
    ANKERL_NANOBENCH_NO_SANITIZE("integer")
    Bench& run(Op&& op) {
        return mBench.runImpl(mSetupOp, std::forward<Op>(op));
    }

    // Bench::run() takes a name, so setup().run() has to as well - otherwise adding a setup to an
    // existing benchmark means rewriting its call site to use name() separately.
    template <typename Op>
    Bench& run(char const* benchmarkName, Op&& op) {
        mBench.name(benchmarkName);
        return run(std::forward<Op>(op));
    }

    template <typename Op>
    Bench& run(std::string const& benchmarkName, Op&& op) {
        mBench.name(benchmarkName);
        return run(std::forward<Op>(op));
    }

private:
    SetupOp mSetupOp;
    Bench& mBench;
};
ANKERL_NANOBENCH(IGNORE_PADDED_POP)
} // namespace detail

template <typename Op>
ANKERL_NANOBENCH_NO_SANITIZE("integer")
Bench& Bench::run(Op&& op) {
    auto setupOp = [] {};
    return runImpl(setupOp, std::forward<Op>(op));
}

template <typename SetupOp, typename Op>
ANKERL_NANOBENCH_NO_SANITIZE("integer")
Bench& Bench::runImpl(SetupOp& setupOp, Op&& op) {
    // It is important that this method is kept short so the compiler can do better optimizations/ inlining of op()
    detail::IterationLogic iterationLogic(*this);
    auto& pc = detail::performanceCounters();

    while (auto n = iterationLogic.numIters()) {
        setupOp();

        pc.beginMeasure();
        Clock::time_point const before = Clock::now();
        while (n-- > 0) {
            op();
        }
        Clock::time_point const after = Clock::now();
        pc.endMeasure();
        pc.updateResults(iterationLogic.numIters());
        iterationLogic.add(after - before, pc);
    }
    iterationLogic.moveResultTo(mResults);
    return *this;
}

template <typename... Args>
CompareResult Bench::compare(Args&&... args) {
    static_assert(sizeof...(args) % 2 == 0, "compare() takes `name, op` pairs");
    static_assert(sizeof...(args) >= 4, "compare() needs at least two alternatives to compare");

    std::vector<std::string> names;
    std::vector<detail::ErasedOp> ops;
    names.reserve(sizeof...(args) / 2);
    ops.reserve(sizeof...(args) / 2);
    compareCollect(names, ops, std::forward<Args>(args)...);
    return compareImpl(names, ops);
}

template <typename SetupOp>
detail::SetupRunner<SetupOp> Bench::setup(SetupOp setupOp) {
    return detail::SetupRunner<SetupOp>(std::move(setupOp), *this);
}

// Performs all evaluations.
template <typename Op>
Bench& Bench::run(char const* benchmarkName, Op&& op) {
    name(benchmarkName);
    return run(std::forward<Op>(op));
}

template <typename Op>
Bench& Bench::run(std::string const& benchmarkName, Op&& op) {
    name(benchmarkName);
    return run(std::forward<Op>(op));
}

#if ANKERL_NANOBENCH(HAS_STRING_VIEW)
template <typename Op>
Bench& Bench::run(std::string_view benchmarkName, Op&& op) {
    name(benchmarkName);
    return run(std::forward<Op>(op));
}
#endif

template <typename Op>
BigO Bench::complexityBigO(char const* benchmarkName, Op op) const {
    return BigO(benchmarkName, BigO::collectRangeMeasure(mResults), op);
}

template <typename Op>
BigO Bench::complexityBigO(std::string const& benchmarkName, Op op) const {
    return BigO(benchmarkName, BigO::collectRangeMeasure(mResults), op);
}

// Set the batch size, e.g. number of processed bytes, or some other metric for the size of the processed data in each iteration.
// Any argument is cast to double.
template <typename T>
Bench& Bench::batch(T b) noexcept {
    mConfig.mBatch = static_cast<double>(b);
    return *this;
}

// Sets the computation complexity of the next run. Any argument is cast to double.
template <typename T>
Bench& Bench::complexityN(T n) noexcept {
    mConfig.mComplexityN = static_cast<double>(n);
    return *this;
}

// Convenience: makes sure none of the given arguments are optimized away by the compiler.
template <typename Arg>
Bench& Bench::doNotOptimizeAway(Arg&& arg) {
    detail::doNotOptimizeAway(std::forward<Arg>(arg));
    return *this;
}

// Makes sure none of the given arguments are optimized away by the compiler.
template <typename Arg>
void doNotOptimizeAway(Arg&& arg) {
    detail::doNotOptimizeAway(std::forward<Arg>(arg));
}

namespace detail {

#if !ANKERL_NANOBENCH(ASM_DONT_OPTIMIZE_AWAY)
template <typename T>
void doNotOptimizeAway(T const& val) {
    doNotOptimizeAwaySink(&val);
}

#endif

} // namespace detail
} // namespace nanobench
} // namespace ankerl

#if defined(ANKERL_NANOBENCH_IMPLEMENT)

///////////////////////////////////////////////////////////////////////////////////////////////////
// implementation part - only visible in .cpp
///////////////////////////////////////////////////////////////////////////////////////////////////

#    include <algorithm> // sort, reverse
#    include <atomic>    // compare_exchange_strong in loop overhead
#    include <cstdlib>   // getenv
#    include <cstring>   // strstr, strncmp
#    include <fstream>   // ifstream to parse proc files
#    include <iomanip>   // setw, setprecision
#    include <iostream>  // cout
#    include <numeric>   // accumulate
#    include <random>    // random_device
#    include <sstream>   // to_s in Number
#    include <stdexcept> // throw for rendering templates
#    if defined(__linux__)
#        include <unistd.h> //sysconf
#    endif
#    if ANKERL_NANOBENCH(PERF_COUNTERS)
#        include <map> // map

#        include <linux/perf_event.h>
#        include <sys/ioctl.h>
#        include <sys/syscall.h>
#    endif

// declarations ///////////////////////////////////////////////////////////////////////////////////

namespace ankerl {
namespace nanobench {

// helper stuff that is only intended to be used internally
namespace detail {

struct TableInfo;

// formatting utilities
namespace fmt {

// groups the integer digits of an already formatted number in threes
std::string addThousandsSeparators(std::string str, char sep);

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

uint64_t splitMix64(uint64_t& state) noexcept;

namespace detail {

// helpers to get double values
template <typename T>
inline double d(T t) noexcept {
    return static_cast<double>(t);
}
inline double d(Clock::duration duration) noexcept {
    return std::chrono::duration_cast<std::chrono::duration<double>>(duration).count();
}

// Rounds to the nearest uint64_t. Casting a double that doesn't fit is undefined behavior, so the value is clamped
// into a safe range first. The comparisons are written so that a NaN takes the same path as a negative value.
inline uint64_t u64(double val) noexcept {
    auto const maxVal = d((std::numeric_limits<uint64_t>::max)() / 2U);
    if (!(val > 0.0)) {
        return 0;
    }
    // +0.5 for correct rounding when casting
    // NOLINTNEXTLINE(bugprone-incorrect-roundings)
    return static_cast<uint64_t>((val < maxVal ? val : maxVal) + 0.5);
}

// Calculates clock resolution once, and remembers the result
inline Clock::duration clockResolution() noexcept;

} // namespace detail

namespace templates {

char const* csv() noexcept {
    return R"DELIM("title";"name";"unit";"batch";"elapsed";"error %";"instructions";"branches";"branch misses";"total"
{{#result}}"{{title}}";"{{name}}";"{{unit}}";{{batch}};{{median(elapsed)}};{{medianAbsolutePercentError(elapsed)}};{{median(instructions)}};{{median(branchinstructions)}};{{median(branchmisses)}};{{sumProduct(iterations, elapsed)}}
{{/result}})DELIM";
}

char const* htmlBoxplot() noexcept {
    return R"DELIM(<html>

<head>
    <script src="https://cdn.plot.ly/plotly-latest.min.js"></script>
</head>

<body>
    <div id="myDiv"></div>
    <script>
        var data = [
            {{#result}}{
                name: '{{name}}',
                y: [{{#measurement}}{{elapsed}}{{^-last}}, {{/last}}{{/measurement}}],
            },
            {{/result}}
        ];
        var title = '{{title}}';

        data = data.map(a => Object.assign(a, { boxpoints: 'all', pointpos: 0, type: 'box' }));
        var layout = { title: { text: title }, showlegend: false, yaxis: { title: 'time per unit', rangemode: 'tozero', autorange: true } }; Plotly.newPlot('myDiv', data, layout, {responsive: true});
    </script>
</body>

</html>)DELIM";
}

char const* pyperf() noexcept {
    return R"DELIM({
    "benchmarks": [
        {
            "runs": [
                {
                    "values": [
{{#measurement}}                        {{elapsed}}{{^-last}},
{{/last}}{{/measurement}}
                    ]
                }
            ]
        }
    ],
    "metadata": {
        "loops": {{sum(iterations)}},
        "inner_loops": {{batch}},
        "name": "{{title}}",
        "unit": "second"
    },
    "version": "1.0"
})DELIM";
}

char const* json() noexcept {
    return R"DELIM({
    "results": [
{{#result}}        {
            "title": "{{title}}",
            "name": "{{name}}",
            "unit": "{{unit}}",
            "batch": {{batch}},
            "complexityN": {{complexityN}},
            "epochs": {{epochs}},
            "clockResolution": {{clockResolution}},
            "clockResolutionMultiple": {{clockResolutionMultiple}},
            "maxEpochTime": {{maxEpochTime}},
            "minEpochTime": {{minEpochTime}},
            "minEpochIterations": {{minEpochIterations}},
            "epochIterations": {{epochIterations}},
            "warmup": {{warmup}},
            "relative": {{relative}},
            "median(elapsed)": {{median(elapsed)}},
            "medianAbsolutePercentError(elapsed)": {{medianAbsolutePercentError(elapsed)}},
            "median(instructions)": {{median(instructions)}},
            "medianAbsolutePercentError(instructions)": {{medianAbsolutePercentError(instructions)}},
            "median(cpucycles)": {{median(cpucycles)}},
            "median(contextswitches)": {{median(contextswitches)}},
            "median(pagefaults)": {{median(pagefaults)}},
            "median(branchinstructions)": {{median(branchinstructions)}},
            "median(branchmisses)": {{median(branchmisses)}},
            "totalTime": {{sumProduct(iterations, elapsed)}},
            "measurements": [
{{#measurement}}                {
                    "iterations": {{iterations}},
                    "elapsed": {{elapsed}},
                    "pagefaults": {{pagefaults}},
                    "cpucycles": {{cpucycles}},
                    "contextswitches": {{contextswitches}},
                    "instructions": {{instructions}},
                    "branchinstructions": {{branchinstructions}},
                    "branchmisses": {{branchmisses}}
                }{{^-last}},{{/-last}}
{{/measurement}}            ]
        }{{^-last}},{{/-last}}
{{/result}}    ]
})DELIM";
}

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
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        return static_cast<size_t>(std::distance(begin, end) + 1) == N && 0 == strncmp(str, begin, N - 1);
    }
};
ANKERL_NANOBENCH(IGNORE_PADDED_POP)

// NOLINTNEXTLINE(misc-no-recursion)
static std::vector<Node> parseMustacheTemplate(char const** tpl) {
    std::vector<Node> nodes;

    while (true) {
        auto const* begin = std::strstr(*tpl, "{{");
        auto const* end = begin;
        if (begin != nullptr) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            begin += 2;
            end = std::strstr(begin, "}}");
        }

        if (begin == nullptr || end == nullptr) {
            // nothing found, finish node
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            nodes.emplace_back(Node{*tpl, *tpl + std::strlen(*tpl), std::vector<Node>{}, Node::Type::content});
            return nodes;
        }

        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        nodes.emplace_back(Node{*tpl, begin - 2, std::vector<Node>{}, Node::Type::content});

        // we found a tag
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        *tpl = end + 2;
        switch (*begin) {
        case '/':
            // finished! bail out
            return nodes;

        case '#':
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            nodes.emplace_back(Node{begin + 1, end, parseMustacheTemplate(tpl), Node::Type::section});
            break;

        case '^':
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            nodes.emplace_back(Node{begin + 1, end, parseMustacheTemplate(tpl), Node::Type::inverted_section});
            break;

        default:
            nodes.emplace_back(Node{begin, end, std::vector<Node>{}, Node::Type::tag});
            break;
        }
    }
}

static bool generateFirstLast(Node const& n, size_t idx, size_t size, std::ostream& out) {
    ANKERL_NANOBENCH_LOG("n.type=" << static_cast<int>(n.type));
    bool const matchFirst = n == "-first";
    bool const matchLast = n == "-last";
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

static bool matchCmdArgs(std::string const& str, std::vector<std::string>& matchResult) {
    matchResult.clear();
    auto idxOpen = str.find('(');
    auto idxClose = str.find(')', idxOpen);
    if (idxClose == std::string::npos) {
        return false;
    }

    matchResult.emplace_back(str.substr(0, idxOpen));

    // split by comma
    matchResult.emplace_back();
    for (size_t i = idxOpen + 1; i != idxClose; ++i) {
        if (str[i] == ' ' || str[i] == '\t') {
            // skip whitespace
            continue;
        }
        if (str[i] == ',') {
            // got a comma => new string
            matchResult.emplace_back();
            continue;
        }
        // no whitespace no comma, append
        matchResult.back() += str[i];
    }
    return true;
}

static bool generateConfigTag(Node const& n, Config const& config, std::ostream& out) {
    using detail::d;

    if (n == "title") {
        out << config.mBenchmarkTitle;
        return true;
    }
    if (n == "name") {
        out << config.mBenchmarkName;
        return true;
    }
    if (n == "unit") {
        out << config.mUnit;
        return true;
    }
    if (n == "batch") {
        out << config.mBatch;
        return true;
    }
    if (n == "complexityN") {
        out << config.mComplexityN;
        return true;
    }
    if (n == "epochs") {
        out << config.mNumEpochs;
        return true;
    }
    if (n == "clockResolution") {
        out << d(detail::clockResolution());
        return true;
    }
    if (n == "clockResolutionMultiple") {
        out << config.mClockResolutionMultiple;
        return true;
    }
    if (n == "maxEpochTime") {
        out << d(config.mMaxEpochTime);
        return true;
    }
    if (n == "minEpochTime") {
        out << d(config.mMinEpochTime);
        return true;
    }
    if (n == "minEpochIterations") {
        out << config.mMinEpochIterations;
        return true;
    }
    if (n == "epochIterations") {
        out << config.mEpochIterations;
        return true;
    }
    if (n == "warmup") {
        out << config.mWarmup;
        return true;
    }
    if (n == "relative") {
        out << config.mIsRelative;
        return true;
    }
    return false;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
// `elapsed` is seconds, which is rarely the unit anyone wants in a report, and the template language
// has no arithmetic to fix that afterwards (issue #107). So a time measure may carry a unit suffix -
// `elapsedms`, `elapsedus`, `elapsedns` - and this resolves it to the measure plus the factor to
// multiply by. Plain `elapsed` keeps its meaning, so existing templates render byte for byte as
// before.
static Result::Measure measureFromString(std::string const& str, double& scale) {
    scale = 1.0;
    auto m = Result::fromString(str);
    if (Result::Measure::_size != m) {
        return m;
    }

    if (str == "elapsedms") {
        scale = 1e3;
        return Result::Measure::elapsed;
    }
    if (str == "elapsedus") {
        scale = 1e6;
        return Result::Measure::elapsed;
    }
    if (str == "elapsedns") {
        scale = 1e9;
        return Result::Measure::elapsed;
    }
    return Result::Measure::_size;
}

// The one-argument measure commands - {{median(elapsed)}} and friends. Split out of
// generateResultTag so that adding the unit-scaled measures did not push its cognitive complexity
// past what clang-tidy accepts.
static bool generateMeasureTag(std::string const& command, Result const& r, Result::Measure m, double scale, std::ostream& out) {
    if (command == "median") {
        out << r.median(m) * scale;
        return true;
    }
    if (command == "average") {
        out << r.average(m) * scale;
        return true;
    }
    if (command == "medianAbsolutePercentError") {
        // a relative error is the same number whatever the unit, so this one is not scaled
        out << r.medianAbsolutePercentError(m);
        return true;
    }
    if (command == "sum") {
        out << r.sum(m) * scale;
        return true;
    }
    if (command == "minimum") {
        out << r.minimum(m) * scale;
        return true;
    }
    if (command == "maximum") {
        out << r.maximum(m) * scale;
        return true;
    }
    return false;
}

static std::ostream& generateResultTag(Node const& n, Result const& r, std::ostream& out) {
    if (generateConfigTag(n, r.config(), out)) {
        return out;
    }
    // match e.g. "median(elapsed)"
    // g++ 4.8 doesn't implement std::regex :(
    // static std::regex const regOpArg1("^([a-zA-Z]+)\\(([a-zA-Z]*)\\)$");
    // std::cmatch matchResult;
    // if (std::regex_match(n.begin, n.end, matchResult, regOpArg1)) {
    std::vector<std::string> matchResult;
    if (matchCmdArgs(std::string(n.begin, n.end), matchResult)) {
        if (matchResult.size() == 2) {
            if (matchResult[0] == "context") {
                return out << r.context(matchResult[1]);
            }

            double scale = 1.0;
            auto m = measureFromString(matchResult[1], scale);
            if (m == Result::Measure::_size) {
                return out << 0.0;
            }
            if (generateMeasureTag(matchResult[0], r, m, scale, out)) {
                return out;
            }
        } else if (matchResult.size() == 3) {
            double scale1 = 1.0;
            double scale2 = 1.0;
            auto m1 = measureFromString(matchResult[1], scale1);
            auto m2 = measureFromString(matchResult[2], scale2);
            if (m1 == Result::Measure::_size || m2 == Result::Measure::_size) {
                return out << 0.0;
            }

            if (matchResult[0] == "sumProduct") {
                return out << r.sumProduct(m1, m2) * scale1 * scale2;
            }
        }
    }

    // match e.g. "sumProduct(elapsed, iterations)"
    // static std::regex const regOpArg2("^([a-zA-Z]+)\\(([a-zA-Z]*)\\s*,\\s+([a-zA-Z]*)\\)$");

    // nothing matches :(
    ANKERL_NANOBENCH_THROW(std::runtime_error("command '" + std::string(n.begin, n.end) + "' not understood"));
}

static void generateResultMeasurement(std::vector<Node> const& nodes, size_t idx, Result const& r, std::ostream& out) {
    for (auto const& n : nodes) {
        if (!generateFirstLast(n, idx, r.size(), out)) {
            ANKERL_NANOBENCH_LOG("n.type=" << static_cast<int>(n.type));
            switch (n.type) {
            case Node::Type::content:
                out.write(n.begin, std::distance(n.begin, n.end));
                break;

            case Node::Type::inverted_section:
                ANKERL_NANOBENCH_THROW(std::runtime_error("got a inverted section inside measurement"));

            case Node::Type::section:
                ANKERL_NANOBENCH_THROW(std::runtime_error("got a section inside measurement"));

            case Node::Type::tag: {
                double scale = 1.0;
                auto m = measureFromString(std::string(n.begin, n.end), scale);
                if (m == Result::Measure::_size || !r.has(m)) {
                    out << 0.0;
                } else {
                    out << r.get(idx, m) * scale;
                }
                break;
            }
            }
        }
    }
}

static void generateResult(std::vector<Node> const& nodes, size_t idx, std::vector<Result> const& results, std::ostream& out) {
    auto const& r = results[idx];
    for (auto const& n : nodes) {
        if (!generateFirstLast(n, idx, results.size(), out)) {
            ANKERL_NANOBENCH_LOG("n.type=" << static_cast<int>(n.type));
            switch (n.type) {
            case Node::Type::content:
                out.write(n.begin, std::distance(n.begin, n.end));
                break;

            case Node::Type::inverted_section:
                ANKERL_NANOBENCH_THROW(std::runtime_error("got a inverted section inside result"));

            case Node::Type::section:
                if (n == "measurement") {
                    for (size_t i = 0; i < r.size(); ++i) {
                        generateResultMeasurement(n.children, i, r, out);
                    }
                } else {
                    ANKERL_NANOBENCH_THROW(std::runtime_error("got a section inside result"));
                }
                break;

            case Node::Type::tag:
                generateResultTag(n, r, out);
                break;
            }
        }
    }
}

} // namespace templates

// helper stuff that only intended to be used internally
namespace detail {

char const* getEnv(char const* name);
bool isEndlessRunning(std::string const& name);
bool isWarningsEnabled();

template <typename T>
T parseFile(std::string const& filename, bool* fail);

void gatherStabilityInformation(std::vector<std::string>& warnings, std::vector<std::string>& recommendations);

// determines resolution of the given clock. This is done by measuring multiple times and returning the minimum time difference.
Clock::duration calcClockResolution(size_t numEvaluations) noexcept;

// formatting utilities
namespace fmt {

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
    ANKERL_NANOBENCH(NODISCARD) std::string to_s() const;

private:
    friend std::ostream& operator<<(std::ostream& os, Number const& n);
    std::ostream& write(std::ostream& os) const;

    int mWidth;
    int mPrecision;
    double mValue;
};

// helper replacement for std::to_string of signed/unsigned numbers so we are locale independent
std::string to_s(uint64_t n);

std::ostream& operator<<(std::ostream& os, Number const& n);

ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
class MarkDownColumn {
public:
    MarkDownColumn(int w, int prec, std::string tit, std::string suff, double val) noexcept;
    // a column holding text instead of a number, for the context columns
    MarkDownColumn(int w, std::string tit, std::string text) noexcept;
    ANKERL_NANOBENCH(NODISCARD) std::string title() const;
    ANKERL_NANOBENCH(NODISCARD) std::string separator() const;
    ANKERL_NANOBENCH(NODISCARD) std::string invalid() const;
    ANKERL_NANOBENCH(NODISCARD) std::string value() const;

private:
    // the column's padding convention, stated once: right aligned in mWidth, one trailing space
    ANKERL_NANOBENCH(NODISCARD) std::string padded(std::string const& text) const;

    int mWidth;
    int mPrecision;
    std::string mTitle;
    std::string mSuffix;
    double mValue;
    std::string mText{};
    bool mIsText{false};
};
ANKERL_NANOBENCH(IGNORE_PADDED_POP)

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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void render(char const* mustacheTemplate, std::vector<Result> const& results, std::ostream& out) {
    detail::fmt::StreamStateRestorer const restorer(out);

    out.precision(std::numeric_limits<double>::digits10);
    auto nodes = templates::parseMustacheTemplate(&mustacheTemplate);

    for (auto const& n : nodes) {
        ANKERL_NANOBENCH_LOG("n.type=" << static_cast<int>(n.type));
        switch (n.type) {
        case templates::Node::Type::content:
            out.write(n.begin, std::distance(n.begin, n.end));
            break;

        case templates::Node::Type::inverted_section:
            ANKERL_NANOBENCH_THROW(std::runtime_error("unknown list '" + std::string(n.begin, n.end) + "'"));

        case templates::Node::Type::section:
            if (n == "result") {
                const size_t nbResults = results.size();
                for (size_t i = 0; i < nbResults; ++i) {
                    generateResult(n.children, i, results, out);
                }
            } else if (n == "measurement") {
                if (results.size() != 1) {
                    ANKERL_NANOBENCH_THROW(std::runtime_error(
                        "render: can only use section 'measurement' here if there is a single result, but there are " +
                        detail::fmt::to_s(results.size())));
                }
                // when we only have a single result, we can immediately go into its measurement.
                auto const& r = results.front();
                for (size_t i = 0; i < r.size(); ++i) {
                    generateResultMeasurement(n.children, i, r, out);
                }
            } else {
                ANKERL_NANOBENCH_THROW(std::runtime_error("render: unknown section '" + std::string(n.begin, n.end) + "'"));
            }
            break;

        case templates::Node::Type::tag:
            if (results.size() == 1) {
                // result & config are both supported there
                generateResultTag(n, results.front(), out);
            } else {
                // This just uses the last result's config.
                if (!generateConfigTag(n, results.back().config(), out)) {
                    ANKERL_NANOBENCH_THROW(std::runtime_error("unknown tag '" + std::string(n.begin, n.end) + "'"));
                }
            }
            break;
        }
    }
}

void render(std::string const& mustacheTemplate, std::vector<Result> const& results, std::ostream& out) {
    render(mustacheTemplate.c_str(), results, out);
}

void render(char const* mustacheTemplate, const Bench& bench, std::ostream& out) {
    render(mustacheTemplate, bench.results(), out);
}

void render(std::string const& mustacheTemplate, const Bench& bench, std::ostream& out) {
    render(mustacheTemplate.c_str(), bench.results(), out);
}

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

Clock::duration targetRuntimePerEpoch(Bench const& bench) {
    auto target = detail::clockResolution() * bench.clockResolutionMultiple();
    if (target > bench.maxEpochTime()) {
        target = bench.maxEpochTime();
    }
    if (target < bench.minEpochTime()) {
        target = bench.minEpochTime();
    }
    return target;
}

// Windows version of doNotOptimizeAway
// see https://github.com/google/benchmark/blob/v1.7.1/include/benchmark/benchmark.h#L514
// see https://github.com/facebook/folly/blob/v2023.01.30.00/folly/lang/Hint-inl.h#L54-L58
// see https://learn.microsoft.com/en-us/cpp/preprocessor/optimize
#    if !ANKERL_NANOBENCH(ASM_DONT_OPTIMIZE_AWAY)
#        if defined(_MSC_VER)
#            pragma optimize("", off)
#        endif
void doNotOptimizeAwaySink(void const*) {}
#        if defined(_MSC_VER)
#            pragma optimize("", on)
#        endif
#    endif

template <typename T>
T parseFile(std::string const& filename, bool* fail) {
    std::ifstream fin(filename); // NOLINT(misc-const-correctness)
    T num{};
    fin >> num;
    if (fail != nullptr) {
        *fail = fin.fail();
    }
    return num;
}

char const* getEnv(char const* name) {
#    if defined(_MSC_VER)
#        pragma warning(push)
#        pragma warning(disable : 4996) // getenv': This function or variable may be unsafe.
#    endif
    return std::getenv(name); // NOLINT(concurrency-mt-unsafe)
#    if defined(_MSC_VER)
#        pragma warning(pop)
#    endif
}

bool isEndlessRunning(std::string const& name) {
    auto const* const endless = getEnv("NANOBENCH_ENDLESS");
    return nullptr != endless && endless == name;
}

// True when environment variable NANOBENCH_SUPPRESS_WARNINGS is either not set at all, or set to "0"
bool isWarningsEnabled() {
    auto const* const suppression = getEnv("NANOBENCH_SUPPRESS_WARNINGS");
    return nullptr == suppression || suppression == std::string("0");
}

void gatherStabilityInformation(std::vector<std::string>& warnings, std::vector<std::string>& recommendations) {
    warnings.clear();
    recommendations.clear();

#    if defined(DEBUG)
    warnings.emplace_back("DEBUG defined");
    bool const recommendCheckFlags = true;
#    else
    bool const recommendCheckFlags = false;
#    endif

    bool recommendPyPerf = false;
#    if defined(__linux__)
    auto nprocs = sysconf(_SC_NPROCESSORS_CONF);
    if (nprocs <= 0) {
        warnings.emplace_back("couldn't figure out number of processors - no governor, turbo check possible");
    } else {
        // check frequency scaling
        for (long id = 0; id < nprocs; ++id) {
            auto idStr = detail::fmt::to_s(static_cast<uint64_t>(id));
            auto sysCpu = "/sys/devices/system/cpu/cpu" + idStr;
            auto minFreq = parseFile<int64_t>(sysCpu + "/cpufreq/scaling_min_freq", nullptr);
            auto maxFreq = parseFile<int64_t>(sysCpu + "/cpufreq/scaling_max_freq", nullptr);
            if (minFreq != maxFreq) {
                auto minMHz = d(minFreq) / 1000.0;
                auto maxMHz = d(maxFreq) / 1000.0;
                warnings.emplace_back("CPU frequency scaling enabled: CPU " + idStr + " between " +
                                      detail::fmt::Number(1, 1, minMHz).to_s() + " and " + detail::fmt::Number(1, 1, maxMHz).to_s() +
                                      " MHz");
                recommendPyPerf = true;
                break;
            }
        }

        auto fail = false;
        auto currentGovernor = parseFile<std::string>("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", &fail);
        if (!fail && "performance" != currentGovernor) {
            warnings.emplace_back("CPU governor is '" + currentGovernor + "' but should be 'performance'");
            recommendPyPerf = true;
        }

        auto noTurbo = parseFile<int>("/sys/devices/system/cpu/intel_pstate/no_turbo", &fail);
        if (!fail && noTurbo == 0) {
            warnings.emplace_back("Turbo is enabled, CPU frequency will fluctuate");
            recommendPyPerf = true;
        }
    }
#    endif

    if (recommendCheckFlags) {
        recommendations.emplace_back("Make sure you compile for Release");
    }
    if (recommendPyPerf) {
        recommendations.emplace_back("Use 'pyperf system tune' before benchmarking. See https://github.com/psf/pyperf");
    }
}

void printStabilityInformationOnce(std::ostream* outStream) {
    static bool shouldPrint = true;
    if (shouldPrint && (nullptr != outStream) && isWarningsEnabled()) {
        auto& os = *outStream;
        shouldPrint = false;
        std::vector<std::string> warnings;
        std::vector<std::string> recommendations;
        gatherStabilityInformation(warnings, recommendations);
        if (warnings.empty()) {
            return;
        }

        os << "Warning, results might be unstable:" << std::endl;
        for (auto const& w : warnings) {
            os << "* " << w << std::endl;
        }

        os << std::endl << "Recommendations" << std::endl;
        for (auto const& r : recommendations) {
            os << "* " << r << std::endl;
        }
    }
}

// When perf_event_open is refused, the table simply comes out five columns narrower. That reads like a
// nanobench bug rather than a setting of the machine, and people have gone looking for it more than once
// (issues #106, #123). So say what happened - but only where those columns could have appeared at all:
// on a platform that has no perf events in the first place nothing is wrong, and a note on every single
// run would be pure noise.
void printPerformanceCounterHintOnce(std::ostream* outStream, bool wantsPerformanceCounters) {
#    if ANKERL_NANOBENCH(PERF_COUNTERS)
    static bool shouldPrint = true;
    if (!shouldPrint || !wantsPerformanceCounters || (nullptr == outStream) || !isWarningsEnabled()) {
        return;
    }
    shouldPrint = false;

    auto const& has = performanceCounters().has();
    if (has.instructions || has.cpuCycles || has.branchInstructions || has.branchMisses) {
        return;
    }

    *outStream << "Note: perf_event_open failed, so the ins/op, cyc/op, IPC, bra/op and miss% columns are missing." << std::endl
               << "This is usually a container or VM without virtualized performance counters, or" << std::endl
               << "/proc/sys/kernel/perf_event_paranoid being too restrictive." << std::endl
               << std::endl;
#    else
    (void)outStream;
    (void)wantsPerformanceCounters;
#    endif
}

// Which index of the pword/iword array every stream carries is ours. Allocated once per process.
static int streamHeaderHashIndex() {
    static int const index = std::ios_base::xalloc();
    return index;
}

// pword holds a raw void*, so what it points at has to be freed when the stream goes away.
static void streamHeaderHashCallback(std::ios_base::event ev, std::ios_base& ios, int index) {
    if (std::ios_base::erase_event == ev) {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete static_cast<uint64_t*>(ios.pword(index));
        ios.pword(index) = nullptr;
    } else if (std::ios_base::copyfmt_event == ev) {
        // copyfmt just copied our pointer into this stream; only one of the two may own it
        ios.pword(index) = nullptr;
    }
}

// This used to be a single process-wide value, so two Bench objects writing to different streams
// overwrote each other's settings and the second table came out with no header at all (issue #112).
//
// The obvious fix - a static map keyed on the ostream* - grows for the lifetime of the process and,
// worse, hands a freshly constructed stream the state of a destroyed one that happened to be
// allocated at the same address, which shows up as a header going missing at random. iostreams
// already provide per-stream storage whose lifetime is exactly the stream's, which is what this
// wants.
uint64_t& streamHeaderHash(std::ostream& os) {
    auto const index = streamHeaderHashIndex();
    void*& slot = os.pword(index);
    if (nullptr == slot) {
        // iword is a separate array at the same index; it records that the callback is registered so
        // a stream never registers it twice. copyfmt copies iword and the callbacks together, so a
        // stream copied from another is already covered.
        if (0 == os.iword(index)) {
            os.iword(index) = 1;
            os.register_callback(streamHeaderHashCallback, index);
        }
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        slot = new uint64_t{};
    }
    return *static_cast<uint64_t*>(slot);
}

ANKERL_NANOBENCH_NO_SANITIZE("integer", "undefined")
inline uint64_t hash_combine(uint64_t seed, uint64_t val) {
    return seed ^ (val + UINT64_C(0x9e3779b9) + (seed << 6U) + (seed >> 2U));
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
    static Clock::duration const sResolution = calcClockResolution(20);
    return sResolution;
}

ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
struct IterationLogic::Impl {
    enum class State { warmup, upscaling_runtime, measuring, endless };

    explicit Impl(Bench const& bench)
        : mBench(bench)
        , mResult(bench.config()) {
        printStabilityInformationOnce(mBench.output());
        printPerformanceCounterHintOnce(mBench.output(), mBench.performanceCounters());

        // determine target runtime per epoch
        mTargetRuntimePerEpoch = detail::targetRuntimePerEpoch(mBench);

        if (isEndlessRunning(mBench.name())) {
            std::cerr << "NANOBENCH_ENDLESS set: running '" << mBench.name() << "' endlessly" << std::endl;
            mNumIters = (std::numeric_limits<uint64_t>::max)();
            mState = State::endless;
        } else if (0 != mBench.warmup()) {
            mNumIters = mBench.warmup();
            mState = State::warmup;
        } else if (hasExactNumIters()) {
            mNumIters = mBench.epochIterations();
            mState = State::measuring;
        } else {
            mNumIters = mBench.minEpochIterations();
            mState = State::upscaling_runtime;
        }
    }

    // True when an exact number of iterations per epoch was requested, which overrides any calculated one.
    ANKERL_NANOBENCH(NODISCARD) bool hasExactNumIters() const noexcept {
        return 0 != mBench.epochIterations();
    }

    // The number of iterations the next epoch should run, honoring an exact count if one was requested.
    ANKERL_NANOBENCH(NODISCARD) uint64_t calcNextNumIters(std::chrono::nanoseconds elapsed, uint64_t iters) noexcept {
        return hasExactNumIters() ? mBench.epochIterations() : calcBestNumIters(elapsed, iters);
    }

    // directly calculates new iters based on elapsed&iters, and stretches the epoch by 0-20% of random noise so epochs
    // don't get into lockstep with periodic disturbances. Makes sure we don't under- or overflow.
    ANKERL_NANOBENCH(NODISCARD) uint64_t calcBestNumIters(std::chrono::nanoseconds elapsed, uint64_t iters) noexcept {
        auto doubleElapsed = d(elapsed);
        auto doubleTargetRuntimePerEpoch = d(mTargetRuntimePerEpoch);
        auto doubleNewIters = doubleTargetRuntimePerEpoch / doubleElapsed * d(iters);

        // An elapsed time of 0 (possible with a coarse clock) makes the division above produce infinity, or even NaN
        // when the target runtime is 0 as well. Comparing with '!(a >= b)' instead of 'a < b' replaces NaN with the
        // minimum too, so an epoch never ends up with a nonsensical number of iterations.
        auto doubleMinEpochIters = d(mBench.minEpochIterations());
        if (!(doubleNewIters >= doubleMinEpochIters)) {
            doubleNewIters = doubleMinEpochIters;
        }
        doubleNewIters *= 1.0 + 0.2 * mRng.uniform01();

        return u64(doubleNewIters);
    }

    ANKERL_NANOBENCH_NO_SANITIZE("integer", "undefined") void upscale(std::chrono::nanoseconds elapsed) {
        if (elapsed * 10 < mTargetRuntimePerEpoch) {
            // we are far below the target runtime. Multiply iterations by 10 (with overflow check)
            if (mNumIters > (std::numeric_limits<uint64_t>::max)() / 10) {
                // overflow :-(
                showResult("iterations overflow. Maybe your code got optimized away?");
                mNumIters = 0;
                return;
            }
            mNumIters *= 10;
        } else {
            mNumIters = calcBestNumIters(elapsed, mNumIters);
        }
    }

    void add(std::chrono::nanoseconds elapsed, PerformanceCounters const& pc) noexcept {
#    if defined(ANKERL_NANOBENCH_LOG_ENABLED)
        auto oldIters = mNumIters;
#    endif

        switch (mState) {
        case State::warmup:
            // an exact number of iterations makes upscaling pointless, so warmup is over either way
            if (hasExactNumIters() || isCloseEnoughForMeasurements(elapsed)) {
                // if elapsed is close enough, we can skip upscaling and go right to measurements
                // still, we don't add the result to the measurements.
                mState = State::measuring;
                mNumIters = calcNextNumIters(elapsed, mNumIters);
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
                mResult.add(elapsed, mNumIters, pc);
                mNumIters = calcNextNumIters(mTotalElapsed, mTotalNumIters);
            } else {
                upscale(elapsed);
            }
            break;

        case State::measuring:
            // just add measurements - no questions asked. Even when runtime is low. But we can't ignore
            // that fluctuation, or else we would bias the result
            mTotalElapsed += elapsed;
            mTotalNumIters += mNumIters;
            mResult.add(elapsed, mNumIters, pc);
            mNumIters = calcNextNumIters(mTotalElapsed, mTotalNumIters);
            break;

        case State::endless:
            mNumIters = (std::numeric_limits<uint64_t>::max)();
            break;
        }

        if (static_cast<uint64_t>(mResult.size()) == mBench.epochs()) {
            // we got all the results that we need, finish it
            showResult("");
            mNumIters = 0;
        }

        ANKERL_NANOBENCH_LOG(mBench.name() << ": " << detail::fmt::Number(20, 3, d(elapsed.count())) << " elapsed, "
                                           << detail::fmt::Number(20, 3, d(mTargetRuntimePerEpoch.count())) << " target. oldIters="
                                           << oldIters << ", mNumIters=" << mNumIters << ", mState=" << static_cast<int>(mState));
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    void showResult(std::string const& errorMessage) const {
        ANKERL_NANOBENCH_LOG(errorMessage);

        if (mBench.output() != nullptr) {
            // prepare column data ///////
            std::vector<fmt::MarkDownColumn> columns;

            // Whether a column *can* be shown (is the counter available, is relative() on) and whether
            // the caller *wants* it are different questions, and interleaving them at every site made it
            // easy to answer one where the other was meant. The availability checks stay below; the
            // visibility policy lives here.
            auto addColumn = [&](Column column, int width, int precision, std::string title, std::string suffix, double value) {
                if (mBench.isColumnVisible(column)) {
                    columns.emplace_back(width, precision, std::move(title), std::move(suffix), value);
                }
            };

            auto rMedian = mResult.median(Result::Measure::elapsed);

            if (mBench.relative()) {
                double d = 100.0;
                if (!mBench.results().empty()) {
                    // Every other column is per unit, so this one has to be as well. Comparing the raw epoch times would
                    // give a skewed percentage as soon as the baseline was run with a different batch size.
                    // See https://github.com/martinus/nanobench/issues/131
                    // This is (baseline / baselineBatch) / (rMedian / batch) as a single fraction, so one guard does.
                    auto const& baseline = mBench.results().front();
                    auto const num = baseline.median(Result::Measure::elapsed) * mBench.batch();
                    auto const den = rMedian * baseline.config().mBatch;
                    d = den <= 0.0 ? 0.0 : num / den * 100.0;
                }
                addColumn(Column::relative, 11, 1, "relative", "%", d);
            }

            if (mBench.complexityN() > 0) {
                addColumn(Column::complexityN, 14, 0, "complexityN", "", mBench.complexityN());
            }

            // context columns come before the measurements: they say which benchmark this row is, and
            // that reads better on the left. A row without the variable gets a blank cell rather than
            // a missing column, so the table stays rectangular.
            auto const& ctx = mResult.config().mContext;
            for (auto const& variableName : mResult.config().mContextColumns) {
                auto it = ctx.find(variableName);
                auto const width = static_cast<int>((std::max)(variableName.size() + 3, static_cast<size_t>(11)));
                columns.emplace_back(width, variableName, it == ctx.end() ? std::string() : it->second);
            }

            addColumn(Column::timePerUnit, 22, 2, mBench.timeUnitName() + "/" + mBench.unit(), "",
                      rMedian / (mBench.timeUnit().count() * mBench.batch()));
            addColumn(Column::unitPerSecond, 22, 2, mBench.unit() + "/s", "", rMedian <= 0.0 ? 0.0 : mBench.batch() / rMedian);

            double const rErrorMedian = mResult.medianAbsolutePercentError(Result::Measure::elapsed);
            addColumn(Column::error, 10, 1, "err%", "%", rErrorMedian * 100.0);

            double rInsMedian = -1.0;
            if (mBench.performanceCounters() && mResult.has(Result::Measure::instructions)) {
                rInsMedian = mResult.median(Result::Measure::instructions);
                addColumn(Column::instructions, 18, 2, "ins/" + mBench.unit(), "", rInsMedian / mBench.batch());
            }

            double rCycMedian = -1.0;
            if (mBench.performanceCounters() && mResult.has(Result::Measure::cpucycles)) {
                rCycMedian = mResult.median(Result::Measure::cpucycles);
                addColumn(Column::cycles, 18, 2, "cyc/" + mBench.unit(), "", rCycMedian / mBench.batch());
            }
            if (rInsMedian > 0.0 && rCycMedian > 0.0) {
                addColumn(Column::ipc, 9, 3, "IPC", "", rCycMedian <= 0.0 ? 0.0 : rInsMedian / rCycMedian);
            }
            if (mBench.performanceCounters() && mResult.has(Result::Measure::branchinstructions)) {
                double const rBraMedian = mResult.median(Result::Measure::branchinstructions);
                addColumn(Column::branches, 17, 2, "bra/" + mBench.unit(), "", rBraMedian / mBench.batch());
                if (mResult.has(Result::Measure::branchmisses)) {
                    double p = 0.0;
                    if (rBraMedian >= 1e-9) {
                        p = 100.0 * mResult.median(Result::Measure::branchmisses) / rBraMedian;
                    }
                    addColumn(Column::branchMisses, 10, 1, "miss%", "%", p);
                }
            }

            addColumn(Column::total, 12, 2, "total", "", mResult.sumProduct(Result::Measure::iterations, Result::Measure::elapsed));

            // write everything
            auto& os = *mBench.output();

            // combine all elements that are relevant for printing the header
            uint64_t hash = 0;
            hash = hash_combine(std::hash<std::string>{}(mBench.unit()), hash);
            hash = hash_combine(std::hash<std::string>{}(mBench.title()), hash);
            hash = hash_combine(std::hash<std::string>{}(mBench.timeUnitName()), hash);
            hash = hash_combine(std::hash<double>{}(mBench.timeUnit().count()), hash);
            hash = hash_combine(std::hash<bool>{}(mBench.relative()), hash);
            hash = hash_combine(std::hash<bool>{}(mBench.performanceCounters()), hash);
            // hiding a column or adding a context one changes the shape of the table, so it needs a
            // fresh header - otherwise the rows below the old one no longer line up with it
            hash = hash_combine(std::hash<uint32_t>{}(mBench.config().mHiddenColumns), hash);
            for (auto const& variableName : mBench.config().mContextColumns) {
                hash = hash_combine(std::hash<std::string>{}(variableName), hash);
            }

            auto& lastHeaderHash = streamHeaderHash(os);
            if (hash != lastHeaderHash) {
                lastHeaderHash = hash;

                // no result yet, print header
                os << std::endl;
                for (auto const& col : columns) {
                    os << col.title();
                }
                os << "| " << mBench.title() << std::endl;

                for (auto const& col : columns) {
                    os << col.separator();
                }
                os << "|:" << std::string(mBench.title().size() + 1U, '-') << std::endl;
            }

            if (!errorMessage.empty()) {
                for (auto const& col : columns) {
                    os << col.invalid();
                }
                os << "| :boom: " << fmt::MarkDownCode(mBench.name()) << " (" << errorMessage << ')' << std::endl;
            } else {
                for (auto const& col : columns) {
                    os << col.value();
                }
                os << "| ";
                auto showUnstable = isWarningsEnabled() && rErrorMedian >= 0.05;
                if (showUnstable) {
                    os << ":wavy_dash: ";
                }
                os << fmt::MarkDownCode(mBench.name());
                if (showUnstable) {
                    auto avgIters = d(mTotalNumIters) / d(mBench.epochs());
                    auto suggestedIters = u64(avgIters * 10);

                    os << " (Unstable with ~" << detail::fmt::Number(1, 1, avgIters)
                       << " iters. Increase `minEpochIterations` to e.g. " << suggestedIters << ")";
                }
                os << std::endl;
            }
        }
    }

    ANKERL_NANOBENCH(NODISCARD) bool isCloseEnoughForMeasurements(std::chrono::nanoseconds elapsed) const noexcept {
        return elapsed * 3 >= mTargetRuntimePerEpoch * 2;
    }

    uint64_t mNumIters = 1;                            // NOLINT(misc-non-private-member-variables-in-classes)
    Bench const& mBench;                               // NOLINT(misc-non-private-member-variables-in-classes)
    std::chrono::nanoseconds mTargetRuntimePerEpoch{}; // NOLINT(misc-non-private-member-variables-in-classes)
    Result mResult;                                    // NOLINT(misc-non-private-member-variables-in-classes)
    Rng mRng{123};                                     // NOLINT(misc-non-private-member-variables-in-classes)
    std::chrono::nanoseconds mTotalElapsed{};          // NOLINT(misc-non-private-member-variables-in-classes)
    uint64_t mTotalNumIters = 0;                       // NOLINT(misc-non-private-member-variables-in-classes)
    State mState = State::upscaling_runtime;           // NOLINT(misc-non-private-member-variables-in-classes)
};
ANKERL_NANOBENCH(IGNORE_PADDED_POP)

IterationLogic::IterationLogic(Bench const& bench)
    : mPimpl(new Impl(bench)) {}

IterationLogic::~IterationLogic() {
    delete mPimpl;
}

uint64_t IterationLogic::numIters() const noexcept {
    ANKERL_NANOBENCH_LOG(mPimpl->mBench.name() << ": mNumIters=" << mPimpl->mNumIters);
    return mPimpl->mNumIters;
}

void IterationLogic::add(std::chrono::nanoseconds elapsed, PerformanceCounters const& pc) noexcept {
    mPimpl->add(elapsed, pc);
}

void IterationLogic::moveResultTo(std::vector<Result>& results) noexcept {
    results.emplace_back(std::move(mPimpl->mResult));
}

// The counter arithmetic, deliberately outside the PERF_COUNTERS guard - see the declarations.

uint64_t divRounded(uint64_t a, uint64_t divisor) noexcept {
    return (a + divisor / 2) / divisor;
}

size_t perfValueIndex(uint64_t i) noexcept {
    return static_cast<size_t>(3 + i * 2);
}

uint64_t scaleMultiplexed(uint64_t value, uint64_t timeEnabled, uint64_t timeRunning) noexcept {
    if (0U == timeRunning || timeEnabled <= timeRunning) {
        // the event was active the whole time, nothing to extrapolate
        return value;
    }
    // counts stay far below 2^53, so double is exact enough here and cannot overflow the way the
    // integer multiplication would
    return u64(d(value) * (d(timeEnabled) / d(timeRunning)));
}

ANKERL_NANOBENCH_NO_SANITIZE("integer", "undefined")
uint64_t correctOverhead(uint64_t value, uint64_t measuringOverhead, uint64_t loopOverheadPerIter, uint64_t numIters) noexcept {
    value = value >= measuringOverhead ? value - measuringOverhead : UINT64_C(0);
    auto const loopCorrection = loopOverheadPerIter * numIters;
    return value >= loopCorrection ? value - loopCorrection : UINT64_C(0);
}

ANKERL_NANOBENCH_NO_SANITIZE("integer", "undefined")
uint64_t loopOverheadPerIteration(uint64_t single, uint64_t doubled, uint64_t measuringOverhead, uint64_t numIters) noexcept {
    auto const m1 = single > measuringOverhead ? single - measuringOverhead : UINT64_C(0);
    auto const m2 = doubled > measuringOverhead ? doubled - measuringOverhead : UINT64_C(0);
    // the second run does the work twice, so twice the first run minus the second is what the loop
    // costs on its own
    auto const overhead = m1 * 2 > m2 ? m1 * 2 - m2 : UINT64_C(0);
    return divRounded(overhead, numIters);
}

ANKERL_NANOBENCH_NO_SANITIZE("integer", "undefined")
uint64_t correctBranchInstructions(uint64_t rawBranchInstructions, uint64_t numIters) noexcept {
    // one branch per iteration for the loop, plus the one that ends it
    auto const loopBranches = numIters + 1U;
    return rawBranchInstructions > loopBranches ? rawBranchInstructions - loopBranches : UINT64_C(0);
}

double correctBranchMisses(uint64_t rawBranchMisses, double correctedBranchInstructions) noexcept {
    auto branchMisses = d(rawBranchMisses);
    if (branchMisses > correctedBranchInstructions) {
        // can't have more branch misses than there were branches
        branchMisses = correctedBranchInstructions;
    }

    // assuming at least one missed branch for the loop
    branchMisses -= 1.0;
    if (branchMisses < 1.0) {
        branchMisses = 1.0;
    }
    return branchMisses;
}

#    if ANKERL_NANOBENCH(PERF_COUNTERS)

// glibc declares ioctl()'s request parameter as unsigned long, musl as int. PERF_EVENT_IOC_ID and
// friends carry the direction bits in the high end, so they don't fit into an int and passing one
// straight through is a value-changing conversion that -Werror rejects on musl (issue #92). Deduce
// the declared parameter type from ioctl() itself, so the same cast is right for either libc.
// Never defined - only ever asked for its return type.
template <typename Ret, typename Fd, typename Request>
Request ioctlRequestType(Ret (*)(Fd, Request, ...));

#        if defined(__cpp_noexcept_function_type)
// Since C++17 noexcept is part of a function's type, and glibc declares ioctl() with __THROW - so
// without this second overload the deduction above stops matching at -std=c++17.
template <typename Ret, typename Fd, typename Request>
Request ioctlRequestType(Ret (*)(Fd, Request, ...) noexcept);
#        endif

template <typename Arg>
int perfIoctl(int fd, unsigned long request, Arg arg) {
    using Request = decltype(ioctlRequestType(&::ioctl));
    // NOLINTNEXTLINE(hicpp-signed-bitwise,cppcoreguidelines-pro-type-vararg)
    return ioctl(fd, static_cast<Request>(request), arg);
}

ANKERL_NANOBENCH(IGNORE_PADDED_PUSH)
class LinuxPerformanceCounters {
public:
    struct Target {
        Target(uint64_t* targetValue_, bool correctMeasuringOverhead_, bool correctLoopOverhead_)
            : targetValue(targetValue_)
            , correctMeasuringOverhead(correctMeasuringOverhead_)
            , correctLoopOverhead(correctLoopOverhead_) {}

        uint64_t* targetValue{};         // NOLINT(misc-non-private-member-variables-in-classes)
        bool correctMeasuringOverhead{}; // NOLINT(misc-non-private-member-variables-in-classes)
        bool correctLoopOverhead{};      // NOLINT(misc-non-private-member-variables-in-classes)
    };

    LinuxPerformanceCounters() = default;
    LinuxPerformanceCounters(LinuxPerformanceCounters const&) = delete;
    LinuxPerformanceCounters(LinuxPerformanceCounters&&) = delete;
    LinuxPerformanceCounters& operator=(LinuxPerformanceCounters const&) = delete;
    LinuxPerformanceCounters& operator=(LinuxPerformanceCounters&&) = delete;
    ~LinuxPerformanceCounters();

    // quick operation
    inline void start() {}

    inline void stop() {}

    bool monitor(perf_sw_ids swId, Target target);
    bool monitor(perf_hw_id hwId, Target target);

    ANKERL_NANOBENCH(NODISCARD) bool hasError() const noexcept {
        return mHasError;
    }

    // Just reading data is faster than enable & disabling.
    // we subtract data ourselves.
    inline void beginMeasure() {
        if (mHasError) {
            return;
        }

        mHasError = -1 == perfIoctl(mFd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
        if (mHasError) {
            return;
        }

        mHasError = -1 == perfIoctl(mFd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    }

    inline void endMeasure() {
        if (mHasError) {
            return;
        }

        mHasError = (-1 == perfIoctl(mFd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP));
        if (mHasError) {
            return;
        }

        auto const numBytes = sizeof(uint64_t) * mCounters.size();
        auto ret = read(mFd, mCounters.data(), numBytes);
        mHasError = ret != static_cast<ssize_t>(numBytes);
        if (!mHasError) {
            scaleCounters();
        }
    }

    void updateResults(uint64_t numIters);

    // Compensates the counters that were just read for multiplexing.
    //
    // When more events are monitored than the hardware can count at the same time, the kernel time-shares the counters
    // between them. An event is then only active for a fraction of the measurement, and the value read back is just what
    // happened while it was active. Scaling it up by enabled/running extrapolates to the whole measurement, which is
    // exactly what `perf stat` does. Without this, cycles/instructions/branches are silently underreported - e.g. when
    // the NMI watchdog occupies one of the counters, or in a VM that exposes a restricted PMU.
    inline void scaleCounters() noexcept {
        auto const timeEnabled = mCounters[1] - mTotalTimeEnabledNanos;
        auto const timeRunning = mCounters[2] - mTotalTimeRunningNanos;
        mTotalTimeEnabledNanos = mCounters[1];
        mTotalTimeRunningNanos = mCounters[2];

        for (uint64_t i = 0; i < mCounters[0]; ++i) {
            auto const idx = perfValueIndex(i);
            mCounters[idx] = scaleMultiplexed(mCounters[idx], timeEnabled, timeRunning);
        }
    }

    ANKERL_NANOBENCH_NO_SANITIZE("integer", "undefined")
    static inline uint32_t mix(uint32_t x) noexcept {
        x ^= x << 13U;
        x ^= x >> 17U;
        x ^= x << 5U;
        return x;
    }

    template <typename Op>
    ANKERL_NANOBENCH_NO_SANITIZE("integer", "undefined")
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
            // marsaglia's xorshift: mov, sal/shr, xor. Times 3.
            // This has the nice property that the compiler doesn't seem to be able to optimize multiple calls any further.
            // see https://godbolt.org/z/49RVQ5
            uint64_t const numIters = 100000U + (std::random_device{}() & 3U);
            uint64_t n = numIters;
            uint32_t x = 1234567;

            beginMeasure();
            while (n-- > 0) {
                x = mix(x);
            }
            endMeasure();
            detail::doNotOptimizeAway(x);
            auto measure1 = mCounters;

            n = numIters;
            beginMeasure();
            while (n-- > 0) {
                // we now run *twice* so we can easily calculate the overhead
                x = mix(x);
                x = mix(x);
            }
            endMeasure();
            detail::doNotOptimizeAway(x);
            auto measure2 = mCounters;

            for (size_t i = 0; i < mCounters.size(); ++i) {
                mLoopOverhead[i] = loopOverheadPerIteration(measure1[i], measure2[i], mCalibratedOverhead[i], numIters);
            }
        }
    }

private:
    bool monitor(uint32_t type, uint64_t eventid, Target target);

    std::map<uint64_t, Target> mIdToTarget{};

    // start with minimum size of 3 for read_format
    std::vector<uint64_t> mCounters = std::vector<uint64_t>(3);
    std::vector<uint64_t> mCalibratedOverhead = std::vector<uint64_t>(3);
    std::vector<uint64_t> mLoopOverhead = std::vector<uint64_t>(3);

    // PERF_EVENT_IOC_RESET resets the counters, but not time_enabled/time_running: those keep accumulating over the
    // whole lifetime of the event. So the times of a single measurement are the difference to the previous read.
    uint64_t mTotalTimeEnabledNanos = 0;
    uint64_t mTotalTimeRunningNanos = 0;
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
ANKERL_NANOBENCH_NO_SANITIZE("integer", "undefined")
void LinuxPerformanceCounters::updateResults(uint64_t numIters) {
    // clear old data
    for (auto& id_value : mIdToTarget) {
        *id_value.second.targetValue = UINT64_C(0);
    }

    if (mHasError) {
        return;
    }

    // mCounters has already been compensated for multiplexing in endMeasure(), and so has mCalibratedOverhead
    for (uint64_t i = 0; i < mCounters[0]; ++i) {
        auto idx = perfValueIndex(i);
        auto id = mCounters[idx + 1U];

        auto it = mIdToTarget.find(id);
        if (it != mIdToTarget.end()) {
            auto& tgt = it->second;
            // a correction the target did not ask for is applied as a correction of zero
            *tgt.targetValue = correctOverhead(mCounters[idx], tgt.correctMeasuringOverhead ? mCalibratedOverhead[idx] : UINT64_C(0),
                                               tgt.correctLoopOverhead ? mLoopOverhead[idx] : UINT64_C(0), numIters);
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

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    auto fd = static_cast<int>(syscall(__NR_perf_event_open, &pea, pid, cpu, mFd, flags));
    if (-1 == fd) {
        return false;
    }
    if (-1 == mFd) {
        // first call: set to fd, and use this from now on
        mFd = fd;
    }
    uint64_t id = 0;
    if (-1 == perfIoctl(fd, PERF_EVENT_IOC_ID, &id)) {
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

    // HW events
    mHas.cpuCycles = mPc->monitor(PERF_COUNT_HW_REF_CPU_CYCLES, LinuxPerformanceCounters::Target(&mVal.cpuCycles, true, false));
    if (!mHas.cpuCycles) {
        // Fallback to cycles counter, reference cycles not available in many systems.
        mHas.cpuCycles = mPc->monitor(PERF_COUNT_HW_CPU_CYCLES, LinuxPerformanceCounters::Target(&mVal.cpuCycles, true, false));
    }
    mHas.instructions = mPc->monitor(PERF_COUNT_HW_INSTRUCTIONS, LinuxPerformanceCounters::Target(&mVal.instructions, true, true));
    mHas.branchInstructions =
        mPc->monitor(PERF_COUNT_HW_BRANCH_INSTRUCTIONS, LinuxPerformanceCounters::Target(&mVal.branchInstructions, true, false));
    mHas.branchMisses = mPc->monitor(PERF_COUNT_HW_BRANCH_MISSES, LinuxPerformanceCounters::Target(&mVal.branchMisses, true, false));
    // mHas.branchMisses = false;

    // SW events
    mHas.pageFaults = mPc->monitor(PERF_COUNT_SW_PAGE_FAULTS, LinuxPerformanceCounters::Target(&mVal.pageFaults, true, false));
    mHas.contextSwitches =
        mPc->monitor(PERF_COUNT_SW_CONTEXT_SWITCHES, LinuxPerformanceCounters::Target(&mVal.contextSwitches, true, false));

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
    // no need to check for nullptr, delete nullptr has no effect
    delete mPc;
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

// Groups the integer digits of an already formatted number in threes: "1234.50" -> "1,234.50".
//
// This used to be a std::numpunct facet imbued into the stream, which is the idiomatic way and works
// right up until someone builds with -fno-rtti: installing a facet goes through __dynamic_cast, which
// without RTTI reads through a null pointer and takes the process with it (issue #122). Grouping the
// digits by hand costs a few lines, produces the same output for everyone, and drops an allocation
// per formatted number along the way.
//
// Only a run of digits is touched, so "inf" and "nan" pass through unchanged.
std::string addThousandsSeparators(std::string str, char sep) {
    size_t const start = (!str.empty() && ('-' == str[0] || '+' == str[0])) ? 1U : 0U;
    size_t end = start;
    while (end < str.size() && str[end] >= '0' && str[end] <= '9') {
        ++end;
    }

    // insert from the right, so the positions still to be visited stay valid
    for (size_t pos = end; pos > start + 3;) {
        pos -= 3;
        str.insert(pos, 1, sep);
    }
    return str;
}

std::ostream& Number::write(std::ostream& os) const {
    // No StreamStateRestorer: the only thing this used to change on os was the imbued locale, and
    // std::setw below is consumed by the very next insertion.

    // format without the stream's locale, so a global locale that already groups digits cannot group
    // them a second time
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::setprecision(mPrecision) << std::fixed << mValue;

    os << std::setw(mWidth) << addThousandsSeparators(ss.str(), ',');
    return os;
}

std::string Number::to_s() const {
    std::stringstream ss;
    write(ss);
    return ss.str();
}

std::string to_s(uint64_t n) {
    std::string str;
    do {
        str += static_cast<char>('0' + static_cast<char>(n % 10));
        n /= 10;
    } while (n != 0);
    std::reverse(str.begin(), str.end());
    return str;
}

std::ostream& operator<<(std::ostream& os, Number const& n) {
    return n.write(os);
}

MarkDownColumn::MarkDownColumn(int w, int prec, std::string tit, std::string suff, double val) noexcept
    : mWidth(w)
    , mPrecision(prec)
    , mTitle(std::move(tit))
    , mSuffix(std::move(suff))
    , mValue(val) {}

MarkDownColumn::MarkDownColumn(int w, std::string tit, std::string text) noexcept
    : mWidth(w)
    , mPrecision(0)
    , mTitle(std::move(tit))
    , mSuffix()
    , mValue(0.0)
    , mText(std::move(text))
    , mIsText(true) {}

std::string MarkDownColumn::padded(std::string const& text) const {
    std::stringstream ss;
    ss << '|' << std::setw(mWidth - 2) << std::right << text << ' ';
    return ss.str();
}

std::string MarkDownColumn::title() const {
    return padded(mTitle);
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
    if (mIsText) {
        // aligned like the header, so the ':' the separator puts on the right stays honest
        return padded(mText);
    }
    std::stringstream ss;
    auto width = mWidth - 2 - static_cast<int>(mSuffix.size());
    ss << '|' << Number(width, mPrecision, mValue) << mSuffix << ' ';
    return ss.str();
}

// Formats any text as markdown code, escaping backticks.
MarkDownCode::MarkDownCode(std::string const& what) {
    mWhat.reserve(what.size() + 2);
    mWhat.push_back('`');
    for (char const c : what) {
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
} // namespace fmt
} // namespace detail

// provide implementation here so it's only generated once
Config::Config() = default;
Config::~Config() = default;
Config& Config::operator=(Config const&) = default;
Config& Config::operator=(Config&&) noexcept(ANKERL_NANOBENCH(NOEXCEPT_STRING_MOVE)) = default;
Config::Config(Config const&) = default;
Config::Config(Config&&) noexcept = default;

// provide implementation here so it's only generated once
Result::~Result() = default;
Result& Result::operator=(Result const&) = default;
Result& Result::operator=(Result&&) noexcept(ANKERL_NANOBENCH(NOEXCEPT_STRING_MOVE)) = default;
Result::Result(Result const&) = default;
Result::Result(Result&&) noexcept = default;

namespace detail {
template <typename T>
inline constexpr typename std::underlying_type<T>::type u(T val) noexcept {
    return static_cast<typename std::underlying_type<T>::type>(val);
}
} // namespace detail

// Result returned after a benchmark has finished. Can be used as a baseline for relative().
Result::Result(Config benchmarkConfig)
    : mConfig(std::move(benchmarkConfig))
    // One slot per measure, plus one for _size itself. Result::fromString() returns _size for a name
    // it does not know, so it reaches these accessors whenever a caller resolves a measure from
    // user input - and reading one slot past the end of the storage is not the answer to a typo.
    // With the extra slot it is a permanently empty measurement list, which reads as "nothing was
    // measured": 0.0 from the statistics and false from has(), the same thing the mustache renderer
    // already does with a measure it does not recognise.
    , mNameToMeasurements{detail::u(Result::Measure::_size) + 1U} {}

void Result::add(Clock::duration totalElapsed, uint64_t iters, detail::PerformanceCounters const& pc) {
    using detail::d;
    using detail::u;

    double const dIters = d(iters);
    mNameToMeasurements[u(Result::Measure::iterations)].push_back(dIters);

    mNameToMeasurements[u(Result::Measure::elapsed)].push_back(d(totalElapsed) / dIters);
    if (pc.has().pageFaults) {
        mNameToMeasurements[u(Result::Measure::pagefaults)].push_back(d(pc.val().pageFaults) / dIters);
    }
    if (pc.has().cpuCycles) {
        mNameToMeasurements[u(Result::Measure::cpucycles)].push_back(d(pc.val().cpuCycles) / dIters);
    }
    if (pc.has().contextSwitches) {
        mNameToMeasurements[u(Result::Measure::contextswitches)].push_back(d(pc.val().contextSwitches) / dIters);
    }
    if (pc.has().instructions) {
        mNameToMeasurements[u(Result::Measure::instructions)].push_back(d(pc.val().instructions) / dIters);
    }
    if (pc.has().branchInstructions) {
        double const branchInstructions = d(detail::correctBranchInstructions(pc.val().branchInstructions, iters));
        mNameToMeasurements[u(Result::Measure::branchinstructions)].push_back(branchInstructions / dIters);

        if (pc.has().branchMisses) {
            auto const branchMisses = detail::correctBranchMisses(pc.val().branchMisses, branchInstructions);
            mNameToMeasurements[u(Result::Measure::branchmisses)].push_back(branchMisses / dIters);
        }
    }
}

Config const& Result::config() const noexcept {
    return mConfig;
}

inline double calcMedian(std::vector<double>& data) {
    if (data.empty()) {
        return 0.0;
    }
    std::sort(data.begin(), data.end());

    auto midIdx = data.size() / 2U;
    if (1U == (data.size() & 1U)) {
        return data[midIdx];
    }
    return (data[midIdx - 1U] + data[midIdx]) / 2U;
}

namespace detail {

std::vector<double> pairedLogRatios(std::vector<double> const& a, std::vector<double> const& b) {
    std::vector<double> logRatios;
    auto const numPairs = (std::min)(a.size(), b.size());
    logRatios.reserve(numPairs);
    for (size_t i = 0; i < numPairs; ++i) {
        // A round where either side measured 0 carries no ratio at all - the logarithm of it is not a
        // large number, it is negative infinity, and one of those poisons every statistic downstream.
        if (a[i] > 0.0 && b[i] > 0.0) {
            logRatios.push_back(std::log(a[i]) - std::log(b[i]));
        }
    }
    return logRatios;
}

std::vector<double> pairedLogRatios(Result const& a, Result const& b) {
    auto const perRound = [](Result const& result) {
        std::vector<double> values;
        values.reserve(result.size());
        for (size_t r = 0; r < result.size(); ++r) {
            values.push_back(result.get(r, Result::Measure::elapsed));
        }
        return values;
    };
    return pairedLogRatios(perRound(a), perRound(b));
}

double medianOf(std::vector<double> values) {
    return calcMedian(values);
}

double bonferroniConfidence(size_t numComparisons) noexcept {
    return 1.0 - 0.05 / d((std::max)(numComparisons, static_cast<size_t>(1)));
}

std::pair<size_t, size_t> medianIntervalIndices(size_t n, double confidence) {
    auto const alphaHalf = (1.0 - confidence) / 2.0;
    size_t k = 0;

    if (n <= 1024U) {
        // Exact: walk the binomial tail up from P(X = 0) = 2^-n, each term from the one before it.
        // 2^-1024 is still a representable double, so nothing underflows into a wrong answer.
        double p = std::pow(0.5, d(n));
        double cumulative = 0.0;
        for (size_t i = 0; i < n; ++i) {
            if (i > 0U) {
                p = p * d(n - i + 1U) / d(i);
            }
            if (cumulative + p > alphaHalf) {
                break;
            }
            cumulative += p;
            k = i + 1U;
        }
    } else {
        // well past the point where the normal approximation of a binomial is worth arguing about
        auto const z = 1.959963985;
        auto const approx = d(n) / 2.0 - z * std::sqrt(d(n)) / 2.0;
        k = approx <= 0.0 ? 0U : static_cast<size_t>(approx);
    }

    if (0U == k || 2U * k > n) {
        // no interval at this confidence for this few observations
        return {1U, 0U};
    }
    return {k - 1U, n - k};
}

size_t countTiedRounds(std::vector<double> const& logRatios) {
    size_t tied = 0;
    for (auto logRatio : logRatios) {
        // written without == so that -Wfloat-equal stays on for everyone else
        if (!(logRatio < 0.0) && !(logRatio > 0.0)) {
            ++tied;
        }
    }
    return tied;
}

std::pair<double, double> medianInterval(std::vector<double> values, double confidence) {
    auto const indices = medianIntervalIndices(values.size(), confidence);
    if (indices.first > indices.second) {
        return {0.0, 0.0};
    }
    std::sort(values.begin(), values.end());
    return {values[indices.first], values[indices.second]};
}

} // namespace detail

CompareResult::Entry::Entry(std::string entryName, Result entryResult, double entryRelative, double entryRelativeLow,
                            double entryRelativeHigh, size_t entryTiedRounds)
    : name(std::move(entryName))
    , result(std::move(entryResult))
    , relative(entryRelative)
    , relativeLow(entryRelativeLow)
    , relativeHigh(entryRelativeHigh)
    , tiedRounds(entryTiedRounds) {}

CompareResult::CompareResult(std::vector<Entry> entries, size_t numRounds)
    : mEntries(std::move(entries))
    , mRounds(numRounds) {}

size_t CompareResult::size() const noexcept {
    return mEntries.size();
}

CompareResult::Entry const& CompareResult::operator[](size_t idx) const {
    return mEntries.at(idx);
}

size_t CompareResult::rounds() const noexcept {
    return mRounds;
}

size_t CompareResult::comparisons() const noexcept {
    return mEntries.empty() ? 0U : mEntries.size() - 1U;
}

bool CompareResult::isSignificant(size_t idx) const {
    if (0U == idx) {
        // the baseline is not compared against itself
        return false;
    }
    auto const& entry = mEntries.at(idx);
    return entry.relativeLow > 1.0 || entry.relativeHigh < 1.0;
}

size_t CompareResult::fastest() const {
    size_t best = 0;
    auto bestMedian = mEntries.empty() ? 0.0 : mEntries[0].result.median(Result::Measure::elapsed);
    for (size_t i = 1; i < mEntries.size(); ++i) {
        auto const median = mEntries[i].result.median(Result::Measure::elapsed);
        if (median < bestMedian) {
            bestMedian = median;
            best = i;
        }
    }
    return best;
}

// The table half of a comparison: an ordinary benchmark table with the ratio to the baseline and an
// interval for it in front. Split out of operator<< because clang-tidy caps cognitive complexity at
// 25 and the two halves together sat well over it.
static void writeCompareTable(std::ostream& os, CompareResult const& compareResult) {
    auto const& config = compareResult[0].result.config();
    auto ratioText = [](double low, double high) {
        std::ostringstream ss;
        ss << detail::fmt::Number(1, 1, low * 100.0) << "% .. " << detail::fmt::Number(1, 1, high * 100.0) << "%";
        return ss.str();
    };

    auto columnsFor = [&](CompareResult::Entry const& entry, bool isBaseline) {
        std::vector<detail::fmt::MarkDownColumn> columns;
        auto const median = entry.result.median(Result::Measure::elapsed);
        // hideColumn() applies here the same as it does to an ordinary table. The two extra columns
        // are the comparison itself, so they are not hideable - without them this is just a table.
        auto addColumn = [&](Column column, int width, int precision, std::string title, std::string suffix, double value) {
            if (detail::isColumnVisible(config, column)) {
                columns.emplace_back(width, precision, std::move(title), std::move(suffix), value);
            }
        };
        columns.emplace_back(11, 1, "relative", "%", entry.relative * 100.0);
        columns.emplace_back(22, "95% CI", isBaseline ? std::string() : ratioText(entry.relativeLow, entry.relativeHigh));
        addColumn(Column::timePerUnit, 22, 2, config.mTimeUnitName + "/" + config.mUnit, "",
                  median / (config.mTimeUnit.count() * config.mBatch));
        addColumn(Column::unitPerSecond, 22, 2, config.mUnit + "/s", "", median <= 0.0 ? 0.0 : config.mBatch / median);
        addColumn(Column::error, 10, 1, "err%", "%", entry.result.medianAbsolutePercentError(Result::Measure::elapsed) * 100.0);
        addColumn(Column::total, 12, 2, "total", "", entry.result.sumProduct(Result::Measure::iterations, Result::Measure::elapsed));
        return columns;
    };

    // An ordinary table only reprints its header when the shape changes, and that state lives on the
    // stream. A comparison table always prints its own header, so the remembered shape is now wrong:
    // clear it, or the next run() on this stream matches against a header it never wrote and streams
    // its rows straight under the comparison's summary with no header of their own.
    detail::streamHeaderHash(os) = 0;

    os << std::endl;
    auto const header = columnsFor(compareResult[0], true);
    for (auto const& col : header) {
        os << col.title();
    }
    os << "| " << config.mBenchmarkTitle << std::endl;
    for (auto const& col : header) {
        os << col.separator();
    }
    os << "|:" << std::string(config.mBenchmarkTitle.size() + 1U, '-') << std::endl;

    for (size_t i = 0; i < compareResult.size(); ++i) {
        auto const columns = columnsFor(compareResult[i], 0U == i);
        for (auto const& col : columns) {
            os << col.value();
        }
        os << "| " << detail::fmt::MarkDownCode(compareResult[i].name) << std::endl;
    }
}

// Two alternatives read better as a sentence than as a row to look up.
// One spelling of the interval, so the table and the two summary shapes cannot drift apart.
static void writeCompareInterval(std::ostream& os, double low, double high) {
    os << "95% CI [" << detail::fmt::Number(1, 2, low) << " .. " << detail::fmt::Number(1, 2, high) << "]";
}

static void writeComparePair(std::ostream& os, CompareResult const& compareResult) {
    auto const baseline = detail::fmt::MarkDownCode(compareResult[0].name);
    auto const& entry = compareResult[1];
    auto const other = detail::fmt::MarkDownCode(entry.name);

    if (!compareResult.isSignificant(1)) {
        os << "    no difference resolved between " << baseline << " and " << other << std::endl
           << "    ratio " << detail::fmt::Number(1, 2, entry.relative) << "x, ";
        writeCompareInterval(os, entry.relativeLow, entry.relativeHigh);
        return;
    }

    // stated the way round the reader asked the question, so the interval inverts with it
    auto const faster = entry.relative > 1.0;
    os << "    " << other << " ran " << detail::fmt::Number(1, 2, faster ? entry.relative : 1.0 / entry.relative)
       << (faster ? "x faster than " : "x slower than ") << baseline << std::endl
       << "    ";
    writeCompareInterval(os, faster ? entry.relativeLow : 1.0 / entry.relativeHigh,
                         faster ? entry.relativeHigh : 1.0 / entry.relativeLow);
}

// Picking the winner out of many is a selection, not a test: whichever came first is flattered by the
// same luck that made it first. So the claim is not "this one is fastest" on its own, it is how far
// ahead of the *runner-up* it is - and if that interval contains 1, the top two were not separated.
static void writeCompareWinner(std::ostream& os, CompareResult const& compareResult) {
    auto const best = compareResult.fastest();
    size_t runnerUp = best;
    auto runnerUpMedian = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < compareResult.size(); ++i) {
        if (i == best) {
            continue;
        }
        auto const median = compareResult[i].result.median(Result::Measure::elapsed);
        if (median < runnerUpMedian) {
            runnerUpMedian = median;
            runnerUp = i;
        }
    }

    // The rounds are paired for every alternative, not only against the baseline, so the top two can
    // be compared to each other after the fact from the per-round measurements.
    auto const logRatios = detail::pairedLogRatios(compareResult[runnerUp].result, compareResult[best].result);
    auto const confidence = detail::bonferroniConfidence(compareResult.comparisons());
    auto const interval = detail::medianInterval(logRatios, confidence);
    auto const lead = std::exp(detail::medianOf(logRatios));
    auto const leadLow = std::exp(interval.first);
    auto const leadHigh = std::exp(interval.second);

    auto const bestCode = detail::fmt::MarkDownCode(compareResult[best].name);
    auto const runnerUpCode = detail::fmt::MarkDownCode(compareResult[runnerUp].name);
    if (leadLow > 1.0) {
        os << "    " << bestCode << " is fastest of " << compareResult.size() << ", " << detail::fmt::Number(1, 2, lead)
           << "x ahead of " << runnerUpCode << std::endl;
    } else {
        os << "    " << bestCode << " and " << runnerUpCode << " are the two fastest of " << compareResult.size()
           << ", and were not separated" << std::endl;
    }
    os << "    ";
    writeCompareInterval(os, leadLow, leadHigh);
    os << ", intervals corrected for " << compareResult.comparisons() << " comparisons";
}

std::ostream& operator<<(std::ostream& os, CompareResult const& compareResult) {
    detail::fmt::StreamStateRestorer const restorer(os);
    if (0U == compareResult.size()) {
        return os;
    }

    // The same table an ordinary benchmark prints, plus the ratio to the baseline and an interval for
    // it. A ratio with no scale beside it cannot be told from the same ratio on a completely
    // different scale, and a side that was wildly unstable is invisible in one - so the absolute
    // numbers come first and the verdict after.
    writeCompareTable(os, compareResult);

    os << std::endl << "  Summary" << std::endl;
    if (2U == compareResult.size()) {
        writeComparePair(os, compareResult);
    } else {
        writeCompareWinner(os, compareResult);
    }
    os << ", " << compareResult.rounds() << " paired rounds, interleaved" << std::endl;

    size_t tied = 0;
    for (size_t i = 1; i < compareResult.size(); ++i) {
        tied = (std::max)(tied, compareResult[i].tiedRounds);
    }
    if (0U != tied) {
        os << "    up to " << tied << " of " << compareResult.rounds() << " rounds tied at the clock's resolution" << std::endl;
    }
    return os;
}

uint64_t Bench::compareCalibrate(detail::ErasedOp const& op) const {
    // An exact epochIterations() overrides any calculated count, exactly as it does in a normal run.
    if (0 != epochIterations()) {
        return epochIterations();
    }

    // An epoch of the same length a normal run would use - the same rule, from the same function, so
    // the two cannot drift apart.
    auto const target = detail::targetRuntimePerEpoch(*this);

    uint64_t numIters = minEpochIterations();
    for (size_t attempt = 0; attempt < 64; ++attempt) {
        Clock::time_point const before = Clock::now();
        op.run(op.op, numIters);
        auto const elapsed = Clock::now() - before;
        if (elapsed >= target) {
            return numIters;
        }

        // grow towards the target, but never by more than 10x at once - the same shape as the normal
        // upscaling, so a wildly wrong first guess still converges in a few steps
        auto const elapsedCount = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
        auto const targetCount = std::chrono::duration_cast<std::chrono::nanoseconds>(target).count();
        auto factor = 10.0;
        if (elapsedCount > 0) {
            factor = detail::d(targetCount) / detail::d(elapsedCount);
        }
        factor = (std::min)(10.0, (std::max)(1.5, factor));
        if (numIters > (std::numeric_limits<uint64_t>::max)() / 16U) {
            break;
        }
        numIters = static_cast<uint64_t>(detail::d(numIters) * factor) + 1U;
    }
    return numIters;
}

CompareResult Bench::compareImpl(std::vector<std::string> const& names, std::vector<detail::ErasedOp> const& ops) {
    auto const numOps = ops.size();

    // A comparison on a machine with frequency scaling on needs these at least as much as a run()
    // does, and a program that only ever calls compare() would otherwise never see them.
    detail::printStabilityInformationOnce(output());
    detail::printPerformanceCounterHintOnce(output(), performanceCounters());

    // Honored, though it is not the tool it looks like here: calibration below already runs each side
    // for about a full epoch, and the serial correlation a warmup would be aimed at is removed by the
    // pairing rather than by running longer first. Measured over 200 rounds, the raw per-round times
    // carry a lag-1 autocorrelation around +0.10 while the paired differences carry -0.01 to -0.08,
    // and dropping the first twenty rounds changes neither.
    for (auto const& op : ops) {
        op.run(op.op, warmup());
    }

    // The same count for every alternative, which matters more than it looks. An epoch carries a
    // fixed overhead, and what gets compared is time per iteration, so that overhead is divided by
    // the count. Calibrating each separately amortizes it differently between them: a systematic bias
    // in the ratio that no amount of pairing removes. It measured 1.2% on 200us epochs.
    auto iters = (std::numeric_limits<uint64_t>::max)();
    for (auto const& op : ops) {
        iters = (std::min)(iters, compareCalibrate(op));
    }

    std::vector<Result> results;
    results.reserve(numOps);
    for (auto const& name : names) {
        Config entryConfig = mConfig;
        entryConfig.mBenchmarkName = name;
        results.emplace_back(entryConfig);
    }

    // Every alternative besides the baseline is one more chance to be wrong, so the intervals are
    // widened to keep the whole table at 95% rather than each row separately.
    auto const confidence = detail::bonferroniConfidence(numOps - 1U);

    // Rounds come in blocks of one epoch per alternative, and the count is raised until the interval
    // is possible at all - with many alternatives the corrected confidence needs more rounds before
    // any pair of order statistics reaches it.
    auto numRounds = ((epochs() + numOps - 1U) / numOps) * numOps;
    while (true) {
        auto const indices = detail::medianIntervalIndices(numRounds, confidence);
        if (indices.first <= indices.second) {
            break;
        }
        numRounds += numOps;
    }

    Rng orderRng;
    std::vector<uint32_t> order(numOps);
    for (size_t i = 0; i < numOps; ++i) {
        order[i] = static_cast<uint32_t>(i);
    }

    for (size_t round = 0; round < numRounds; ++round) {
        auto const positionInBlock = round % numOps;
        if (0 == positionInBlock) {
            // A fresh random permutation per block, then rotated one step per round: every
            // alternative occupies every position exactly once over the block, so their mean
            // positions are equal and a drift that is linear over the block cancels. For two
            // alternatives this produces exactly ABBA or BAAB.
            orderRng.shuffle(order);
        }
        for (size_t slot = 0; slot < numOps; ++slot) {
            auto const which = order[(slot + positionInBlock) % numOps];
            compareEpoch(ops[which], iters, results[which]);
        }
    }

    std::vector<CompareResult::Entry> entries;
    entries.reserve(numOps);
    entries.emplace_back(names[0], std::move(results[0]), 1.0, 1.0, 1.0, static_cast<size_t>(0));
    for (size_t i = 1; i < numOps; ++i) {
        // ln(t_baseline) - ln(t_i), so a positive difference means the alternative was quicker and
        // the ratio comes out above 1 - the same direction relative() reports. The baseline was moved
        // into entries[0] above, which is where it is read from now.
        auto const logRatios = detail::pairedLogRatios(entries[0].result, results[i]);
        auto const interval = detail::medianInterval(logRatios, confidence);
        entries.emplace_back(names[i], std::move(results[i]), std::exp(detail::medianOf(logRatios)), std::exp(interval.first),
                             std::exp(interval.second), detail::countTiedRounds(logRatios));
    }

    CompareResult compareResult{std::move(entries), numRounds};
    if (nullptr != output()) {
        *output() << compareResult;
    }
    return compareResult;
}

void Bench::compareEpoch(detail::ErasedOp const& op, uint64_t numIters, Result& result) {
    auto& pc = detail::performanceCounters();

    pc.beginMeasure();
    Clock::time_point const before = Clock::now();
    op.run(op.op, numIters);
    Clock::time_point const after = Clock::now();
    pc.endMeasure();
    pc.updateResults(numIters);

    result.add(after - before, numIters, pc);
}

double Result::median(Measure m) const {
    // create a copy so we can sort
    auto data = mNameToMeasurements.at(detail::u(m));
    return calcMedian(data);
}

double Result::average(Measure m) const {
    using detail::d;
    auto const& data = mNameToMeasurements.at(detail::u(m));
    if (data.empty()) {
        return 0.0;
    }

    // create a copy so we can sort
    return sum(m) / d(data.size());
}

double Result::medianAbsolutePercentError(Measure m) const {
    // create copy
    auto data = mNameToMeasurements.at(detail::u(m));

    // calculates MdAPE which is the median of percentage error
    // see https://support.numxl.com/hc/en-us/articles/115001223503-MdAPE-Median-Absolute-Percentage-Error
    auto med = calcMedian(data);

    // The error is relative to the measurement itself, so a measurement of 0 has none that is finite: it is 0 when the
    // median is 0 as well (they agree), and infinite otherwise, which is the limit of |(x - med) / x| for x -> 0.
    // Calculating it directly would give NaN, and a NaN in the data breaks the ordering that calcMedian's sort needs.
    auto const zeroError = med <= 0.0 ? 0.0 : std::numeric_limits<double>::infinity();

    // transform the data to absolute error
    for (auto& x : data) {
        if (x <= 0.0) {
            x = zeroError;
            continue;
        }
        x = (x - med) / x;
        if (x < 0) {
            x = -x;
        }
    }
    return calcMedian(data);
}

double Result::sum(Measure m) const noexcept {
    auto const& data = mNameToMeasurements.at(detail::u(m));
    return std::accumulate(data.begin(), data.end(), 0.0);
}

double Result::sumProduct(Measure m1, Measure m2) const noexcept {
    auto const& data1 = mNameToMeasurements.at(detail::u(m1));
    auto const& data2 = mNameToMeasurements.at(detail::u(m2));

    if (data1.size() != data2.size()) {
        return 0.0;
    }

    double result = 0.0;
    for (size_t i = 0, s = data1.size(); i != s; ++i) {
        result += data1[i] * data2[i];
    }
    return result;
}

bool Result::has(Measure m) const noexcept {
    return !mNameToMeasurements.at(detail::u(m)).empty();
}

double Result::get(size_t idx, Measure m) const {
    auto const& data = mNameToMeasurements.at(detail::u(m));
    return data.at(idx);
}

bool Result::empty() const noexcept {
    return 0U == size();
}

size_t Result::size() const noexcept {
    auto const& data = mNameToMeasurements.at(detail::u(Measure::elapsed));
    return data.size();
}

double Result::minimum(Measure m) const noexcept {
    auto const& data = mNameToMeasurements.at(detail::u(m));
    if (data.empty()) {
        return 0.0;
    }

    // here its save to assume that at least one element is there
    return *std::min_element(data.begin(), data.end());
}

double Result::maximum(Measure m) const noexcept {
    auto const& data = mNameToMeasurements.at(detail::u(m));
    if (data.empty()) {
        return 0.0;
    }

    // here its save to assume that at least one element is there
    return *std::max_element(data.begin(), data.end());
}

std::string const& Result::context(char const* variableName) const {
    return mConfig.mContext.at(variableName);
}

std::string const& Result::context(std::string const& variableName) const {
    return mConfig.mContext.at(variableName);
}

Result::Measure Result::fromString(std::string const& str) {
    if (str == "elapsed") {
        return Measure::elapsed;
    }
    if (str == "iterations") {
        return Measure::iterations;
    }
    if (str == "pagefaults") {
        return Measure::pagefaults;
    }
    if (str == "cpucycles") {
        return Measure::cpucycles;
    }
    if (str == "contextswitches") {
        return Measure::contextswitches;
    }
    if (str == "instructions") {
        return Measure::instructions;
    }
    if (str == "branchinstructions") {
        return Measure::branchinstructions;
    }
    if (str == "branchmisses") {
        return Measure::branchmisses;
    }
    // not found, return _size
    return Measure::_size;
}

// Configuration of a microbenchmark.
Bench::Bench() {
    mConfig.mOut = &std::cout;
}

Bench::Bench(Bench&&) noexcept = default;
Bench& Bench::operator=(Bench&&) noexcept(ANKERL_NANOBENCH(NOEXCEPT_STRING_MOVE)) = default;
Bench::Bench(Bench const&) = default;
Bench& Bench::operator=(Bench const&) = default;
Bench::~Bench() noexcept = default;

double Bench::batch() const noexcept {
    return mConfig.mBatch;
}

double Bench::complexityN() const noexcept {
    return mConfig.mComplexityN;
}

// Set a baseline to compare it to. 100% it is exactly as fast as the baseline, >100% means it is faster than the baseline, <100%
// means it is slower than the baseline.
Bench& Bench::relative(bool isRelativeEnabled) noexcept {
    mConfig.mIsRelative = isRelativeEnabled;
    return *this;
}
bool Bench::relative() const noexcept {
    return mConfig.mIsRelative;
}

Bench& Bench::performanceCounters(bool showPerformanceCounters) noexcept {
    mConfig.mShowPerformanceCounters = showPerformanceCounters;
    return *this;
}
bool Bench::performanceCounters() const noexcept {
    return mConfig.mShowPerformanceCounters;
}

namespace detail {

// One bit of mHiddenColumns per Column, so there had better be at most 32 of them.
static_assert(static_cast<size_t>(Column::_size) <= 32, "Column no longer fits in Config::mHiddenColumns");

ANKERL_NANOBENCH(NODISCARD) inline uint32_t columnBit(Column column) noexcept {
    return UINT32_C(1) << static_cast<uint32_t>(column);
}

bool isColumnVisible(Config const& config, Column column) noexcept {
    return 0 == (config.mHiddenColumns & columnBit(column));
}

} // namespace detail

Bench& Bench::hideColumn(Column column) noexcept {
    mConfig.mHiddenColumns |= detail::columnBit(column);
    return *this;
}

Bench& Bench::showColumn(Column column) noexcept {
    mConfig.mHiddenColumns &= ~detail::columnBit(column);
    return *this;
}

bool Bench::isColumnVisible(Column column) const noexcept {
    return detail::isColumnVisible(mConfig, column);
}

Bench& Bench::contextColumn(std::string const& variableName) {
    // adding the same name twice would print the same column twice
    if (mConfig.mContextColumns.end() != std::find(mConfig.mContextColumns.begin(), mConfig.mContextColumns.end(), variableName)) {
        return *this;
    }
    mConfig.mContextColumns.push_back(variableName);
    return *this;
}

Bench& Bench::clearContextColumns() noexcept {
    mConfig.mContextColumns.clear();
    return *this;
}

// Operation unit. Defaults to "op", could be e.g. "byte" for string processing.
// If u differs from currently set unit, the stored results will be cleared.
// Use singular (byte, not bytes).
Bench& Bench::unit(char const* u) {
    if (u != mConfig.mUnit) {
        mResults.clear();
    }
    mConfig.mUnit = u;
    return *this;
}

Bench& Bench::unit(std::string const& u) {
    return unit(u.c_str());
}

std::string const& Bench::unit() const noexcept {
    return mConfig.mUnit;
}

Bench& Bench::timeUnit(std::chrono::duration<double> const& tu, std::string const& tuName) {
    mConfig.mTimeUnit = tu;
    mConfig.mTimeUnitName = tuName;
    return *this;
}

std::string const& Bench::timeUnitName() const noexcept {
    return mConfig.mTimeUnitName;
}

std::chrono::duration<double> const& Bench::timeUnit() const noexcept {
    return mConfig.mTimeUnit;
}

// If benchmarkTitle differs from currently set title, the stored results will be cleared.
Bench& Bench::title(const char* benchmarkTitle) {
    if (benchmarkTitle != mConfig.mBenchmarkTitle) {
        mResults.clear();
    }
    mConfig.mBenchmarkTitle = benchmarkTitle;
    return *this;
}
Bench& Bench::title(std::string const& benchmarkTitle) {
    if (benchmarkTitle != mConfig.mBenchmarkTitle) {
        mResults.clear();
    }
    mConfig.mBenchmarkTitle = benchmarkTitle;
    return *this;
}

std::string const& Bench::title() const noexcept {
    return mConfig.mBenchmarkTitle;
}

Bench& Bench::name(const char* benchmarkName) {
    mConfig.mBenchmarkName = benchmarkName;
    return *this;
}

Bench& Bench::name(std::string const& benchmarkName) {
    mConfig.mBenchmarkName = benchmarkName;
    return *this;
}

#    if ANKERL_NANOBENCH(HAS_STRING_VIEW)
Bench& Bench::name(std::string_view benchmarkName) {
    mConfig.mBenchmarkName.assign(benchmarkName.data(), benchmarkName.size());
    return *this;
}
#    endif

std::string const& Bench::name() const noexcept {
    return mConfig.mBenchmarkName;
}

Bench& Bench::context(char const* variableName, char const* variableValue) {
    mConfig.mContext[variableName] = variableValue;
    return *this;
}

Bench& Bench::context(std::string const& variableName, std::string const& variableValue) {
    mConfig.mContext[variableName] = variableValue;
    return *this;
}

Bench& Bench::clearContext() {
    mConfig.mContext.clear();
    return *this;
}

// Number of epochs to evaluate. The reported result will be the median of evaluation of each epoch.
Bench& Bench::epochs(size_t numEpochs) noexcept {
    mConfig.mNumEpochs = numEpochs;
    return *this;
}
size_t Bench::epochs() const noexcept {
    return mConfig.mNumEpochs;
}

// Desired evaluation time is a multiple of clock resolution. Default is to be 1000 times above this measurement precision.
Bench& Bench::clockResolutionMultiple(size_t multiple) noexcept {
    mConfig.mClockResolutionMultiple = multiple;
    return *this;
}
size_t Bench::clockResolutionMultiple() const noexcept {
    return mConfig.mClockResolutionMultiple;
}

// Sets the maximum time each epoch should take. Default is 100ms.
Bench& Bench::maxEpochTime(std::chrono::nanoseconds t) noexcept {
    mConfig.mMaxEpochTime = t;
    return *this;
}
std::chrono::nanoseconds Bench::maxEpochTime() const noexcept {
    return mConfig.mMaxEpochTime;
}

// Sets the minimum time each epoch should take. Default is 1ms.
Bench& Bench::minEpochTime(std::chrono::nanoseconds t) noexcept {
    mConfig.mMinEpochTime = t;
    return *this;
}
std::chrono::nanoseconds Bench::minEpochTime() const noexcept {
    return mConfig.mMinEpochTime;
}

Bench& Bench::minEpochIterations(uint64_t numIters) noexcept {
    mConfig.mMinEpochIterations = (numIters == 0) ? 1 : numIters;
    return *this;
}
uint64_t Bench::minEpochIterations() const noexcept {
    return mConfig.mMinEpochIterations;
}

Bench& Bench::epochIterations(uint64_t numIters) noexcept {
    mConfig.mEpochIterations = numIters;
    return *this;
}
uint64_t Bench::epochIterations() const noexcept {
    return mConfig.mEpochIterations;
}

Bench& Bench::warmup(uint64_t numWarmupIters) noexcept {
    mConfig.mWarmup = numWarmupIters;
    return *this;
}
uint64_t Bench::warmup() const noexcept {
    return mConfig.mWarmup;
}

Bench& Bench::config(Config const& benchmarkConfig) {
    mConfig = benchmarkConfig;
    return *this;
}
Config const& Bench::config() const noexcept {
    return mConfig;
}

Bench& Bench::output(std::ostream* outstream) noexcept {
    mConfig.mOut = outstream;
    return *this;
}

ANKERL_NANOBENCH(NODISCARD) std::ostream* Bench::output() const noexcept {
    return mConfig.mOut;
}

std::vector<Result> const& Bench::results() const noexcept {
    return mResults;
}

Bench& Bench::render(char const* templateContent, std::ostream& os) {
    ::ankerl::nanobench::render(templateContent, *this, os);
    return *this;
}

Bench& Bench::render(std::string const& templateContent, std::ostream& os) {
    ::ankerl::nanobench::render(templateContent, *this, os);
    return *this;
}

std::vector<BigO> Bench::complexityBigO() const {
    std::vector<BigO> bigOs;
    auto rangeMeasure = BigO::collectRangeMeasure(mResults);
    bigOs.emplace_back("O(1)", rangeMeasure, [](double) {
        return 1.0;
    });
    bigOs.emplace_back("O(n)", rangeMeasure, [](double n) {
        return n;
    });
    bigOs.emplace_back("O(log n)", rangeMeasure, [](double n) {
        return std::log2(n);
    });
    bigOs.emplace_back("O(n log n)", rangeMeasure, [](double n) {
        return n * std::log2(n);
    });
    bigOs.emplace_back("O(n^2)", rangeMeasure, [](double n) {
        return n * n;
    });
    bigOs.emplace_back("O(n^3)", rangeMeasure, [](double n) {
        return n * n * n;
    });
    std::sort(bigOs.begin(), bigOs.end());
    return bigOs;
}

Rng::Rng()
    : mX(0)
    , mY(0) {
    std::random_device rd;
    std::uniform_int_distribution<uint64_t> dist;
    do {
        mX = dist(rd);
        mY = dist(rd);
    } while (mX == 0 && mY == 0);
}

ANKERL_NANOBENCH_NO_SANITIZE("integer", "undefined")
uint64_t splitMix64(uint64_t& state) noexcept {
    uint64_t z = (state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27U)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31U);
}

// Seeded as described in romu paper (update april 2020)
Rng::Rng(uint64_t seed) noexcept
    : mX(splitMix64(seed))
    , mY(splitMix64(seed)) {
    for (size_t i = 0; i < 10; ++i) {
        operator()();
    }
}

// only internally used to copy the RNG.
Rng::Rng(uint64_t x, uint64_t y) noexcept
    : mX(x)
    , mY(y) {}

Rng Rng::copy() const noexcept {
    return Rng{mX, mY};
}

Rng::Rng(std::vector<uint64_t> const& data)
    : mX(0)
    , mY(0) {
    if (data.size() != 2) {
        ANKERL_NANOBENCH_THROW(std::runtime_error("ankerl::nanobench::Rng::Rng: needed exactly 2 entries in data, but got " +
                                                  detail::fmt::to_s(data.size())));
    }
    mX = data[0];
    mY = data[1];
}

std::vector<uint64_t> Rng::state() const {
    std::vector<uint64_t> data(2);
    data[0] = mX;
    data[1] = mY;
    return data;
}

BigO::RangeMeasure BigO::collectRangeMeasure(std::vector<Result> const& results) {
    BigO::RangeMeasure rangeMeasure;
    for (auto const& result : results) {
        if (result.config().mComplexityN > 0.0) {
            rangeMeasure.emplace_back(result.config().mComplexityN, result.median(Result::Measure::elapsed));
        }
    }
    return rangeMeasure;
}

BigO::BigO(std::string bigOName, RangeMeasure const& rangeMeasure)
    : mName(std::move(bigOName)) {

    // Nothing to fit against: leave the constant and the error at 0 rather than dividing by zero
    // three times over. complexityBigO() reaches this whenever it is called on a Bench where no
    // complexityN was ever set, since collectRangeMeasure() then returns nothing - the six models
    // used to come out all-NaN, which prints as "nan" and, being unordered, is a poor thing to hand
    // to std::sort.
    if (rangeMeasure.empty()) {
        return;
    }

    // estimate the constant factor
    double sumRangeMeasure = 0.0;
    double sumRangeRange = 0.0;

    for (const auto& rm : rangeMeasure) {
        sumRangeMeasure += rm.first * rm.second;
        sumRangeRange += rm.first * rm.first;
    }
    // a sum of squares, so this is only 0 when every n is
    mConstant = sumRangeRange <= 0.0 ? 0.0 : sumRangeMeasure / sumRangeRange;

    // calculate root mean square
    double err = 0.0;
    double sumMeasure = 0.0;
    for (const auto& rm : rangeMeasure) {
        auto diff = mConstant * rm.first - rm.second;
        err += diff * diff;

        sumMeasure += rm.second;
    }

    auto n = detail::d(rangeMeasure.size());
    auto mean = sumMeasure / n;
    // the error is relative to the mean measurement, so there is no relative error to report when
    // everything measured as 0 - and 0/0 is not it
    mNormalizedRootMeanSquare = mean <= 0.0 ? 0.0 : std::sqrt(err / n) / mean;
}

BigO::BigO(const char* bigOName, RangeMeasure const& rangeMeasure)
    : BigO(std::string(bigOName), rangeMeasure) {}

std::string const& BigO::name() const noexcept {
    return mName;
}

double BigO::constant() const noexcept {
    return mConstant;
}

double BigO::normalizedRootMeanSquare() const noexcept {
    return mNormalizedRootMeanSquare;
}

bool BigO::operator<(BigO const& other) const noexcept {
    return (mNormalizedRootMeanSquare < other.mNormalizedRootMeanSquare) ||
           (!(mNormalizedRootMeanSquare > other.mNormalizedRootMeanSquare) && mName < other.mName);
}

std::ostream& operator<<(std::ostream& os, BigO const& bigO) {
    return os << bigO.constant() << " * " << bigO.name() << ", rms=" << bigO.normalizedRootMeanSquare();
}

std::ostream& operator<<(std::ostream& os, std::vector<ankerl::nanobench::BigO> const& bigOs) {
    detail::fmt::StreamStateRestorer const restorer(os);
    os << std::endl << "|   coefficient |   err% | complexity" << std::endl << "|--------------:|-------:|------------" << std::endl;
    for (auto const& bigO : bigOs) {
        os << "|" << std::setw(14) << std::setprecision(7) << std::scientific << bigO.constant() << " ";
        os << "|" << detail::fmt::Number(6, 1, bigO.normalizedRootMeanSquare() * 100.0) << "% ";
        os << "| " << bigO.name();
        os << std::endl;
    }
    return os;
}

} // namespace nanobench
} // namespace ankerl

#endif // ANKERL_NANOBENCH_IMPLEMENT
#endif // ANKERL_NANOBENCH_H_INCLUDED
