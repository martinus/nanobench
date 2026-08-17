#include <nanobench.h>
#include <thirdparty/doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

// NANOBENCH_CONFIG lets a benchmark's timing knobs be changed from the shell.
// The parsing is a separate function taking a string exactly so that these
// tests exist: setting an environment variable is putenv on one platform and
// _putenv_s on another, and a test that went through it could only ever check
// one value per process.
//
// Everything asserted here is exact arithmetic on a parsed value - no elapsed
// time is involved - so the assertions can be tight.
namespace {

using ankerl::nanobench::Bench;
using ankerl::nanobench::detail::applyConfigString;

namespace chrono = std::chrono;

// Applies a config string and requires that nanobench found nothing to complain
// about.
Bench applied(std::string const& configStr) {
    Bench bench;
    std::vector<std::string> errors;
    applyConfigString(bench, configStr, errors);
    INFO("config string: " << configStr);
    for (auto const& error : errors) {
        INFO("unexpected: " << error);
    }
    REQUIRE(errors.empty());
    return bench;
}

// The single message a bad config string produces. Total on purpose: a helper
// that indexed errors[0] would crash rather than fail when a rejection stops
// happening.
std::string singleError(std::string const& configStr) {
    Bench bench;
    std::vector<std::string> errors;
    applyConfigString(bench, configStr, errors);
    INFO("config string: " << configStr);
    REQUIRE(errors.size() == 1U);
    return errors.front();
}

// Every rejection repeats the prefix and the entry, so only the reason is worth
// writing down at the call site - and writing the entry once means a typo in it
// cannot masquerade as a parser bug.
void checkReason(std::string const& entry, std::string const& reason) {
    CHECK(singleError(entry) == "NANOBENCH_CONFIG: '" + entry + "' " + reason);
}

} // namespace

// NOLINTNEXTLINE
TEST_CASE("unit_env_config_each_key_reaches_its_own_field") {
    // Every key against a Bench that is otherwise untouched, so a key writing a
    // neighbouring setting shows up as the default it moved rather than being
    // hidden by the value next to it.
    Bench const defaults;

    auto epochs = applied("epochs=51");
    CHECK(epochs.epochs() == 51U);
    CHECK(epochs.warmup() == defaults.warmup());
    CHECK(epochs.clockResolutionMultiple() ==
          defaults.clockResolutionMultiple());

    CHECK(applied("warmup=7").warmup() == UINT64_C(7));
    CHECK(applied("minEpochIterations=1234").minEpochIterations() ==
          UINT64_C(1234));
    CHECK(applied("epochIterations=99").epochIterations() == UINT64_C(99));
    CHECK(applied("clockResolutionMultiple=2000").clockResolutionMultiple() ==
          2000U);

    auto minTime = applied("minEpochTime=5ms");
    CHECK(minTime.minEpochTime() == chrono::milliseconds(5));
    CHECK(minTime.maxEpochTime() == defaults.maxEpochTime());

    auto maxTime = applied("maxEpochTime=5ms");
    CHECK(maxTime.maxEpochTime() == chrono::milliseconds(5));
    CHECK(maxTime.minEpochTime() == defaults.minEpochTime());
}

// NOLINTNEXTLINE
TEST_CASE("unit_env_config_goes_through_the_setters") {
    // The keys are documented as the Bench setter names, and they are applied
    // by calling those setters - so a rule a setter enforces holds for a value
    // that arrived from the environment too. minEpochIterations is the one that
    // has such a rule today: it refuses 0, because an epoch of no iterations is
    // not a measurement.
    CHECK(applied("minEpochIterations=0").minEpochIterations() == UINT64_C(1));

    Bench bench;
    CHECK(bench.minEpochIterations(0).minEpochIterations() == UINT64_C(1));
}

// NOLINTNEXTLINE
TEST_CASE("unit_env_config_durations") {
    // every unit, against the same duration written in another one
    CHECK(applied("minEpochTime=1500000000ns").minEpochTime() ==
          chrono::milliseconds(1500));
    CHECK(applied("minEpochTime=2500us").minEpochTime() ==
          chrono::microseconds(2500));
    CHECK(applied("minEpochTime=5ms").minEpochTime() ==
          chrono::microseconds(5000));
    CHECK(applied("minEpochTime=2s").minEpochTime() ==
          chrono::milliseconds(2000));

    // A fractional number is the whole point of having units: nobody should
    // have to write 1500ms because seconds only take integers.
    CHECK(applied("maxEpochTime=1.5s").maxEpochTime() ==
          chrono::milliseconds(1500));
    CHECK(applied("minEpochTime=0.5ms").minEpochTime() ==
          chrono::microseconds(500));
    CHECK(applied("minEpochTime=2.5ms").minEpochTime() ==
          chrono::microseconds(2500));
    CHECK(applied("minEpochTime=0.25s").minEpochTime() ==
          chrono::milliseconds(250));

    // Scaling by an exact power of ten rather than multiplying a double: 2.3 *
    // 1e6 lands just below 2300000 in binary floating point, so truncating
    // there would give 2299999ns.
    CHECK(applied("minEpochTime=2.3ms").minEpochTime() ==
          chrono::nanoseconds(2300000));
    CHECK(applied("minEpochTime=8.7s").minEpochTime() ==
          chrono::nanoseconds(8700000000));

    // More precision than a nanosecond rounds to one, rather than being
    // rejected. Only the last digit dropped decides it, so 1.449 is 1 rather
    // than rounding up twice through 1.45.
    CHECK(applied("minEpochTime=1.4ns").minEpochTime() ==
          chrono::nanoseconds(1));
    CHECK(applied("minEpochTime=1.5ns").minEpochTime() ==
          chrono::nanoseconds(2));
    CHECK(applied("minEpochTime=1.7ns").minEpochTime() ==
          chrono::nanoseconds(2));
    CHECK(applied("minEpochTime=1.75ns").minEpochTime() ==
          chrono::nanoseconds(2));
    CHECK(applied("minEpochTime=1.449ns").minEpochTime() ==
          chrono::nanoseconds(1));
    CHECK(applied("minEpochTime=1.234ns").minEpochTime() ==
          chrono::nanoseconds(1));

    // 0 is a legitimate setting - it is what turns the floor off
    CHECK(applied("minEpochTime=0ms").minEpochTime() == chrono::nanoseconds(0));
}

// NOLINTNEXTLINE
TEST_CASE("unit_env_config_several_entries") {
    auto cfg = applied("epochs=3,minEpochTime=2ms,maxEpochTime=1.5s,warmup=4");
    CHECK(cfg.epochs() == 3U);
    CHECK(cfg.minEpochTime() == chrono::milliseconds(2));
    CHECK(cfg.maxEpochTime() == chrono::milliseconds(1500));
    CHECK(cfg.warmup() == UINT64_C(4));

    // Whitespace is somebody laying the variable out to be read, and an empty
    // entry is a trailing comma. Neither is a mistake worth a message.
    auto spaced = applied("  epochs = 3 ,, minEpochTime=2ms,  ");
    CHECK(spaced.epochs() == 3U);
    CHECK(spaced.minEpochTime() == chrono::milliseconds(2));

    // an empty variable leaves everything alone
    Bench const defaults;
    auto empty = applied("");
    CHECK(empty.epochs() == defaults.epochs());
    CHECK(empty.minEpochTime() == defaults.minEpochTime());

    // the last entry for a key wins, so a config string can be appended to
    CHECK(applied("epochs=3,epochs=9").epochs() == 9U);
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
    checkReason("epochs", "is not a key=value pair");

    // the trap that a single NANOBENCH_MIN_EPOCH_TIME in bare nanoseconds would
    // have set
    checkReason("minEpochTime=5",
                "is missing a time unit - use ns, us, ms or s");
    checkReason("minEpochTime=5min",
                "is missing a time unit - use ns, us, ms or s");

    checkReason("epochs=1.5", "is not a whole number");
    checkReason("epochs=", "is not a number");
    checkReason("epochs=abc", "is not a number");
    checkReason("minEpochTime=1.2.3ms", "is not a number");
    checkReason("minEpochTime=ms", "is not a number");
    // a unit and nothing else is a missing number, not a missing unit
    checkReason("minEpochTime=s", "is not a number");

    // A stream, and std::stoull, both read "-1" as a signed value and negate
    // it, so this would be 18446744073709551615 epochs rather than a rejection.
    checkReason("epochs=-1", "is not a number");
    checkReason("minEpochTime=-5ms", "is not a number");

    // too many digits to hold, and enough digits that the unit pushes it out of
    // range
    checkReason("epochs=99999999999999999999999", "is too large");
    checkReason("minEpochTime=99999999999s", "is too large");

    // The setters take a uint64_t, so the whole of its range has to arrive
    // rather than a guard being conservative by the last few values.
    CHECK(applied("minEpochIterations=18446744073709551615")
              .minEpochIterations() == UINT64_MAX);
    checkReason("minEpochIterations=18446744073709551616", "is too large");

    // fits on its own, and does not once the unit has scaled it
    checkReason("minEpochTime=2000000000000000000us", "is too large");
    CHECK(applied("minEpochTime=2000000000000000000ns").minEpochTime() ==
          chrono::nanoseconds(2000000000000000000));

    // the top of what a nanoseconds can hold still arrives
    CHECK(applied("minEpochTime=9223372036854775807ns").minEpochTime() ==
          chrono::nanoseconds(INT64_MAX));
    checkReason("minEpochTime=9223372036854775808ns", "is too large");
}

// NOLINTNEXTLINE
TEST_CASE("unit_env_config_bad_entry_does_not_stop_the_others") {
    Bench bench;
    std::vector<std::string> errors;
    applyConfigString(
        bench,
        "epochs=3,nonsense=1,minEpochTime=2ms,warmup=oops,maxEpochTime=1.5s",
        errors);

    CHECK(errors.size() == 2U);
    CHECK(bench.epochs() == 3U);
    CHECK(bench.minEpochTime() == chrono::milliseconds(2));
    CHECK(bench.maxEpochTime() == chrono::milliseconds(1500));

    // the entry that failed left its setting alone rather than half-applying
    Bench const defaults;
    CHECK(bench.warmup() == defaults.warmup());
}

namespace {

// Sets NANOBENCH_CONFIG for as long as it is alive. This is the one thing that
// cannot be reached through applyConfigString(): whether Bench's constructor
// reads the variable at all, and whether it spells it right.
class ScopedConfigEnv {
public:
    explicit ScopedConfigEnv(char const* value) {
#if defined(_MSC_VER)
        _putenv_s("NANOBENCH_CONFIG", value);
#else
        setenv("NANOBENCH_CONFIG", value, 1);
#endif
    }

    ~ScopedConfigEnv() {
#if defined(_MSC_VER)
        _putenv_s("NANOBENCH_CONFIG", "");
#else
        unsetenv("NANOBENCH_CONFIG");
#endif
    }

    ScopedConfigEnv(ScopedConfigEnv const&) = delete;
    ScopedConfigEnv& operator=(ScopedConfigEnv const&) = delete;
    ScopedConfigEnv(ScopedConfigEnv&&) = delete;
    ScopedConfigEnv& operator=(ScopedConfigEnv&&) = delete;
};

} // namespace

// NOLINTNEXTLINE
TEST_CASE("unit_env_config_bench_reads_the_environment") {
    // Deleting the applyEnvConfig() call from Bench::Bench() leaves everything
    // above green - the parser would be perfect and reach nothing. This is the
    // test that fails for that, so it is also the one that pins the variable's
    // name.
    {
        ScopedConfigEnv const env("epochs=51,minEpochTime=5ms");

        Bench const bench;
        CHECK(bench.epochs() == 51U);
        CHECK(bench.minEpochTime() == chrono::milliseconds(5));

        // The environment moves the default; it does not overrule a benchmark
        // that needs a particular setting to mean anything. The constructor
        // applies it, so anything the benchmark sets afterwards comes last.
        Bench explicitly;
        explicitly.epochs(3).minEpochTime(chrono::milliseconds(2));
        CHECK(explicitly.epochs() == 3U);
        CHECK(explicitly.minEpochTime() == chrono::milliseconds(2));
    }

    // and the defaults are back once it is unset, which is what keeps
    // unit_bench_config_defaults from depending on the order tests run in
    Bench const clean;
    CHECK(clean.epochs() == 11U);
    CHECK(clean.minEpochTime() == chrono::milliseconds(1));
}
