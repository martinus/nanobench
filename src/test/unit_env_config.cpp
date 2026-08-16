#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// NANOBENCH_CONFIG lets a benchmark's timing knobs be changed from the shell.
// The parsing is a separate function taking a string exactly so that these
// tests exist: reading the variable happens once per process, so a test that
// set it could only ever check one value, and setting it at all is putenv on
// one platform and _putenv_s on another.
//
// Everything asserted here is exact arithmetic on a parsed value - no elapsed
// time is involved - so the assertions can be tight.
namespace {

using ankerl::nanobench::Config;
using ankerl::nanobench::detail::applyConfigString;

namespace chrono = std::chrono;

// Applies a config string and requires that nanobench found nothing to complain
// about.
Config applied(std::string const& configStr) {
    Config cfg;
    std::vector<std::string> errors;
    applyConfigString(cfg, configStr, errors);
    INFO("config string: " << configStr);
    for (auto const& error : errors) {
        INFO("unexpected: " << error);
    }
    REQUIRE(errors.empty());
    return cfg;
}

// The single message a bad config string produces. Total on purpose: a helper
// that indexed errors[0] would crash rather than fail when a rejection stops
// happening.
std::string singleError(std::string const& configStr) {
    Config cfg;
    std::vector<std::string> errors;
    applyConfigString(cfg, configStr, errors);
    INFO("config string: " << configStr);
    REQUIRE(errors.size() == 1U);
    return errors.front();
}

} // namespace

// NOLINTNEXTLINE
TEST_CASE("unit_env_config_each_key_reaches_its_own_field") {
    // Every key against a Config that is otherwise untouched, so a setter
    // writing a neighbouring field shows up as the default it moved rather than
    // being hidden by the value next to it.
    Config const defaults;

    auto epochs = applied("epochs=51");
    CHECK(epochs.mNumEpochs == 51U);
    CHECK(epochs.mWarmup == defaults.mWarmup);
    CHECK(epochs.mClockResolutionMultiple == defaults.mClockResolutionMultiple);

    CHECK(applied("warmup=7").mWarmup == UINT64_C(7));
    CHECK(applied("minEpochIterations=1234").mMinEpochIterations ==
          UINT64_C(1234));
    CHECK(applied("epochIterations=99").mEpochIterations == UINT64_C(99));
    CHECK(applied("clockResolutionMultiple=2000").mClockResolutionMultiple ==
          2000U);

    auto minTime = applied("minEpochTime=5ms");
    CHECK(minTime.mMinEpochTime == chrono::milliseconds(5));
    CHECK(minTime.mMaxEpochTime == defaults.mMaxEpochTime);

    auto maxTime = applied("maxEpochTime=5ms");
    CHECK(maxTime.mMaxEpochTime == chrono::milliseconds(5));
    CHECK(maxTime.mMinEpochTime == defaults.mMinEpochTime);
}

// NOLINTNEXTLINE
TEST_CASE("unit_env_config_durations") {
    // every unit, against the same duration written in another one
    CHECK(applied("minEpochTime=1500000000ns").mMinEpochTime ==
          chrono::milliseconds(1500));
    CHECK(applied("minEpochTime=2500us").mMinEpochTime ==
          chrono::microseconds(2500));
    CHECK(applied("minEpochTime=5ms").mMinEpochTime ==
          chrono::microseconds(5000));
    CHECK(applied("minEpochTime=2s").mMinEpochTime ==
          chrono::milliseconds(2000));

    // A fractional number is the whole point of having units: nobody should
    // have to write 1500ms because seconds only take integers.
    CHECK(applied("maxEpochTime=1.5s").mMaxEpochTime ==
          chrono::milliseconds(1500));
    CHECK(applied("minEpochTime=0.5ms").mMinEpochTime ==
          chrono::microseconds(500));
    CHECK(applied("minEpochTime=2.5ms").mMinEpochTime ==
          applied("minEpochTime=2500us").mMinEpochTime);
    CHECK(applied("minEpochTime=0.25s").mMinEpochTime ==
          chrono::milliseconds(250));

    // Scaling by an exact power of ten rather than multiplying a double: 2.3 *
    // 1e6 lands just below 2300000 in binary floating point, so truncating
    // there would give 2299999ns.
    CHECK(applied("minEpochTime=2.3ms").mMinEpochTime ==
          chrono::nanoseconds(2300000));
    CHECK(applied("minEpochTime=8.7s").mMinEpochTime ==
          chrono::nanoseconds(8700000000));

    // More precision than a nanosecond rounds to one, rather than being
    // rejected. Only the last digit dropped decides it, so 1.449 is 1 rather
    // than rounding up twice through 1.45.
    CHECK(applied("minEpochTime=1.4ns").mMinEpochTime ==
          chrono::nanoseconds(1));
    CHECK(applied("minEpochTime=1.5ns").mMinEpochTime ==
          chrono::nanoseconds(2));
    CHECK(applied("minEpochTime=1.7ns").mMinEpochTime ==
          chrono::nanoseconds(2));
    CHECK(applied("minEpochTime=1.75ns").mMinEpochTime ==
          chrono::nanoseconds(2));
    CHECK(applied("minEpochTime=1.449ns").mMinEpochTime ==
          chrono::nanoseconds(1));
    CHECK(applied("minEpochTime=1.234ns").mMinEpochTime ==
          chrono::nanoseconds(1));

    // 0 is a legitimate setting - it is what turns the floor off
    CHECK(applied("minEpochTime=0ms").mMinEpochTime == chrono::nanoseconds(0));
}

// NOLINTNEXTLINE
TEST_CASE("unit_env_config_several_entries") {
    auto cfg = applied("epochs=3,minEpochTime=2ms,maxEpochTime=1.5s,warmup=4");
    CHECK(cfg.mNumEpochs == 3U);
    CHECK(cfg.mMinEpochTime == chrono::milliseconds(2));
    CHECK(cfg.mMaxEpochTime == chrono::milliseconds(1500));
    CHECK(cfg.mWarmup == UINT64_C(4));

    // Whitespace is somebody laying the variable out to be read, and an empty
    // entry is a trailing comma. Neither is a mistake worth a message.
    auto spaced = applied("  epochs = 3 ,, minEpochTime=2ms,  ");
    CHECK(spaced.mNumEpochs == 3U);
    CHECK(spaced.mMinEpochTime == chrono::milliseconds(2));

    // an empty variable leaves everything alone
    Config const defaults;
    CHECK(applied("").mNumEpochs == defaults.mNumEpochs);
    CHECK(applied("").mMinEpochTime == defaults.mMinEpochTime);

    // the last entry for a key wins, so a config string can be appended to
    CHECK(applied("epochs=3,epochs=9").mNumEpochs == 9U);
}

// NOLINTNEXTLINE
TEST_CASE("unit_env_config_rejects") {
    // Each of these is one message naming the entry it came from. The wording
    // matters as much as the rejection does - it is all the user gets to work
    // out what they mistyped.
    CHECK(singleError("epoch=3") ==
          "NANOBENCH_CONFIG: unknown key 'epoch' - valid keys are "
          "clockResolutionMultiple, epochIterations, epochs, "
          "maxEpochTime, minEpochIterations, minEpochTime, warmup");
    // keys are the Bench setter names verbatim, so they are case sensitive
    CHECK(singleError("Epochs=3").find("unknown key 'Epochs'") !=
          std::string::npos);
    CHECK(singleError("epochs") ==
          "NANOBENCH_CONFIG: 'epochs' is not a key=value pair");

    // the trap that a single NANOBENCH_MIN_EPOCH_TIME in bare nanoseconds would
    // have set
    CHECK(singleError("minEpochTime=5") ==
          "NANOBENCH_CONFIG: 'minEpochTime=5' is missing a time unit - use ns, "
          "us, ms or s");
    CHECK(singleError("minEpochTime=5min") ==
          "NANOBENCH_CONFIG: 'minEpochTime=5min' is missing a time unit - use "
          "ns, us, ms or s");

    CHECK(singleError("epochs=1.5") ==
          "NANOBENCH_CONFIG: 'epochs=1.5' is not a whole number");
    CHECK(singleError("epochs=") ==
          "NANOBENCH_CONFIG: 'epochs=' is not a number");
    CHECK(singleError("epochs=abc") ==
          "NANOBENCH_CONFIG: 'epochs=abc' is not a number");
    CHECK(singleError("minEpochTime=1.2.3ms") ==
          "NANOBENCH_CONFIG: 'minEpochTime=1.2.3ms' is not a number");
    CHECK(singleError("minEpochTime=ms") ==
          "NANOBENCH_CONFIG: 'minEpochTime=ms' is not a number");
    // a unit and nothing else is a missing number, not a missing unit
    CHECK(singleError("minEpochTime=s") ==
          "NANOBENCH_CONFIG: 'minEpochTime=s' is not a number");

    // A stream, and std::stoull, both read "-1" as a signed value and negate
    // it, so this would be 18446744073709551615 epochs rather than a rejection.
    CHECK(singleError("epochs=-1") ==
          "NANOBENCH_CONFIG: 'epochs=-1' is not a number");
    CHECK(singleError("minEpochTime=-5ms") ==
          "NANOBENCH_CONFIG: 'minEpochTime=-5ms' is not a number");

    // too many digits to hold, and enough digits that the unit pushes it out of
    // range
    CHECK(singleError("epochs=99999999999999999999999") ==
          "NANOBENCH_CONFIG: 'epochs=99999999999999999999999' is too large");
    CHECK(singleError("minEpochTime=99999999999s") ==
          "NANOBENCH_CONFIG: 'minEpochTime=99999999999s' is too large");

    // The setters take a uint64_t, so the whole of its range has to arrive
    // rather than a guard being conservative by the last few values.
    CHECK(applied("minEpochIterations=18446744073709551615")
              .mMinEpochIterations == UINT64_MAX);
    CHECK(singleError("minEpochIterations=18446744073709551616") ==
          "NANOBENCH_CONFIG: 'minEpochIterations=18446744073709551616' is too "
          "large");

    // fits on its own, and does not once the unit has scaled it
    CHECK(
        singleError("minEpochTime=2000000000000000000us") ==
        "NANOBENCH_CONFIG: 'minEpochTime=2000000000000000000us' is too large");
    CHECK(applied("minEpochTime=2000000000000000000ns").mMinEpochTime ==
          chrono::nanoseconds(2000000000000000000));

    // the top of what a nanoseconds can hold still arrives
    CHECK(applied("minEpochTime=9223372036854775807ns").mMinEpochTime ==
          chrono::nanoseconds(INT64_MAX));
    CHECK(singleError("minEpochTime=9223372036854775808ns") ==
          "NANOBENCH_CONFIG: 'minEpochTime=9223372036854775808ns' is too "
          "large");
}

// NOLINTNEXTLINE
TEST_CASE("unit_env_config_bad_entry_does_not_stop_the_others") {
    Config cfg;
    std::vector<std::string> errors;
    applyConfigString(
        cfg,
        "epochs=3,nonsense=1,minEpochTime=2ms,warmup=oops,maxEpochTime=1.5s",
        errors);

    CHECK(errors.size() == 2U);
    CHECK(cfg.mNumEpochs == 3U);
    CHECK(cfg.mMinEpochTime == chrono::milliseconds(2));
    CHECK(cfg.mMaxEpochTime == chrono::milliseconds(1500));

    // the entry that failed left its field alone rather than half-applying
    // something
    Config const defaults;
    CHECK(cfg.mWarmup == defaults.mWarmup);
}

// NOLINTNEXTLINE
TEST_CASE("unit_env_config_explicit_setter_wins") {
    // The environment moves the default. A benchmark that needs a particular
    // setting to mean anything must not be broken by somebody's shell, so what
    // it sets itself comes last - which is what Bench::Bench() applying the
    // config string, and only then the setters running, gives.
    ankerl::nanobench::Bench bench;
    bench.config(applied("epochs=51,minEpochTime=5ms"));
    CHECK(bench.epochs() == 51U);
    CHECK(bench.minEpochTime() == chrono::milliseconds(5));

    bench.epochs(3).minEpochTime(chrono::milliseconds(2));
    CHECK(bench.epochs() == 3U);
    CHECK(bench.minEpochTime() == chrono::milliseconds(2));
}
