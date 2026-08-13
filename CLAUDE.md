# CLAUDE.md

Guidance for working on `nanobench` — a single-header C++11..20 microbenchmark library
(`ankerl::nanobench::Bench`).

The whole library is `src/include/nanobench.h` (~3.5k lines). Everything below
`#if defined(ANKERL_NANOBENCH_IMPLEMENT)` is the implementation and is compiled into exactly one TU
(`src/test/app/nanobench.cpp`). Tests, examples and tutorials are in `src/test/` and link into a
single doctest binary, `nb`.

## Build & test

```sh
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release
ninja -C build
./build/nb                     # all tests;  -ltc lists them,  -tc=<name> runs one,  -s shows detail
```

Options: `-DNB_cxx_standard=17` (default 11), `-DNB_sanitizer=ON` (clang only).

**Build gotchas**

- clang builds use `-Weverything -Werror`, so a new clang release can add a warning that breaks
  the build; the two that already did are switched off in `src/cmake/CMakeLists.txt`.
  clang-tidy is *not* part of the build (it broke every clang build whenever it gained a check,
  issue #108) — the `lint` CI job runs it pinned. Locally: `-DCMAKE_CXX_CLANG_TIDY=clang-tidy-18`.
- Running `./nb` writes example artifacts (`*.json`, `mustache.*`, `always_the_same.html`, …) into
  the *current* directory. Run it from the build dir, and check `git status` before `git add -A`.
- `unit_templates` compares the built-in templates against `src/docs/_generated/*` using a path
  derived from `__FILE__`, so it only passes when `__FILE__` is absolute (cmake) or cwd is the repo
  root. A failure there after a manual compile is an artifact, not a real regression.

## Before pushing

`.github/workflows/main.yml` builds every leg the same way, so any of them reproduces locally:

```sh
CXX=<compiler> cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNB_cxx_standard=<std> <cmake_args>
cmake --build build --parallel 4 && ./build/nb        # run from the repo root
```

It covers gcc/clang × C++11..20, 32 bit, libc++, sanitizers, ARM64, macOS, MSVC, clang-cl and
MinGW, plus a `lint` job (pinned clang-format-18 / clang-tidy-18) and a CMake consumer job. Before
pushing it is still worth running the quick local sweep:

```sh
# 1. both compilers, all standards, warning-free.
#    tu.cpp is just: #define ANKERL_NANOBENCH_IMPLEMENT
#                    #include <nanobench.h>
for s in 11 14 17 20; do
  clang++ -std=c++$s -Werror -Weverything -Wno-c++98-compat -Wno-c++98-compat-pedantic \
          -Wno-unsafe-buffer-usage -Wno-padded -Wno-switch-default -c -o /dev/null -Isrc/include tu.cpp
  g++     -std=c++$s -Werror -Wall -Wextra -Wconversion -Wold-style-cast -Wfloat-equal \
          -Wsign-conversion -c -o /dev/null -Isrc/include tu.cpp
done

# 2. sanitizers (run from the repo root, see unit_templates note above)
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -Isrc/include -Isrc/test \
    -o /tmp/nb-san src/test/app/*.cpp src/test/*.cpp && /tmp/nb-san

# 3. formatting: 0 replacements == clean
clang-format --output-replacements-xml <file> | grep -c "<replacement "
```

`-Wfloat-equal` is on, so never compare a double with `==`/`!=` — use `x <= 0.0` and friends.
There are **two** clang-format configs: `src/include/.clang-format` (135 columns, the header) and
`src/.clang-format` (80 columns, everything under `src/test/`).

`docs/` is generated (doxygen + sphinx + breathe, via `src/docs/generate.sh`) and regenerated in its
own commit — never hand-edit it. Doc comments live in the header.

## How a measurement works

`Bench::run()` drives `detail::IterationLogic` (pimpl), a state machine over
`warmup → upscaling_runtime → measuring` (plus `endless` when `NANOBENCH_ENDLESS=<name>` is set).
Per epoch it times `n` calls of `op()` between two `Clock::now()`s, then `IterationLogic::add()`
decides the next `n`; `numIters() == 0` ends the run.

- Target epoch runtime = `clockResolution() * clockResolutionMultiple()` (default 1000), clamped to
  `[minEpochTime, maxEpochTime]` (defaults 1ms / 100ms). If they conflict, **min wins**.
  On Linux the measured clock resolution is ~21ns, so 1000× ≈ 21µs is below the 1ms floor —
  `minEpochTime` is what actually sets the epoch length in practice. 11 epochs → ~12ms per benchmark.
- The reported number is the **median over epochs** of (epoch elapsed / iterations); `err%` is the
  MdAPE of the same data. Epoch iteration counts are randomized 0–20% upward on purpose.
- Only `measuring` (and the transition out of `upscaling_runtime`) records results; warmup never does.

Performance counters are Linux-only (`perf_event_open`, kernel ≥ 3.3). `read_format` is
`GROUP|ID|TOTAL_TIME_ENABLED|TOTAL_TIME_RUNNING`, so `mCounters` is
`[nr, time_enabled, time_running, (value, id) × nr]`. Two facts worth knowing:
`PERF_EVENT_IOC_RESET` resets the counter values but **not** `time_enabled`/`time_running` (they
accumulate for the event's lifetime — use deltas), and the calibrated overhead is a min over 100
empty measurements, subtracted per epoch.

**Hardware counters are unavailable in most containers/VMs** (`perf_event_open` fails, `mHas` ends up
all false), so changes to that code path cannot be exercised locally there — say so rather than
claiming it was tested.

## Benchmarking claims

Any claim about accuracy needs numbers. A workable ground truth is the minimum of several long
single-shot runs of the same op; compare nanobench's median against it. On a shared VM expect
`err%` ≈ 1% and ~0.5% run-to-run spread of the reported median, so treat sub-1% differences as noise.
