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

Options: `-DNB_cxx_standard=17` (default 11), `-DNB_sanitizer=ON` (gcc and clang both supported).

**Build gotchas**

- clang builds use `-Weverything -Werror`, which opts in to warnings that don't exist yet, so a new
  clang can break the build. Version-guarding each `-Wno-` doesn't work — AppleClang's version
  numbers don't map to upstream clang's — so `src/cmake/CMakeLists.txt` uses
  `-Wno-unknown-warning-option` plus a list. Put only nanobench's *own* warnings in that list:
  warnings from the vendored `doctest.h` are handled by including `src/test` as a `SYSTEM` directory,
  which suppresses them as a class instead of blinding `nanobench.h` to them too.
- clang-tidy is *not* part of the build (it broke every clang build whenever it gained a check,
  issue #108) — the `lint` CI job runs it pinned. Locally: `-DCMAKE_CXX_CLANG_TIDY=clang-tidy-18`.
- Running `./nb` writes example artifacts (`*.json`, `mustache.*`, `always_the_same.html`, …) into
  the *current* directory. Run it from the build dir, and check `git status` before `git add -A`.
- `unit_templates` compares the built-in templates against `src/docs/_generated/*` using a path
  derived from `__FILE__`, so it only passes when `__FILE__` is absolute (cmake) or cwd is the repo
  root. A failure there after a manual compile is an artifact, not a real regression.
- A test that parses the markdown table must give its `Bench` its own `title()`. The header is only
  written when the table's shape changes, and that state lives on the output stream, so a second test
  configuring the table identically gets **no header at all** — after which a helper that indexes the
  header row crashes instead of failing. Keep such helpers total for the same reason.
- Don't name anything in `src/test/` after a `<cmath>` function. libc++ puts `::fma` in scope, so a
  local `fma` template joins it in one overload set and `fma<float>` stops resolving — that broke
  the libc++ leg until `tutorial_context.cpp`'s helper became `fma_bench`.

## Landing a change

Every change reaches `master` through a pull request that is green — one-line fixes, docs edits and
lint reformats included. This is enforced rather than agreed: `master` requires the `CI green` status
check with `enforce_admins` on, so a direct push is rejected with `GH006: Protected branch update
failed`. Rebase merge is the only method the repository allows, so `gh pr merge <n> --rebase` is the
call. Merging someone else's PR is fine once its checks pass.

The rule is here because local verification cannot stand in for the matrix, however thorough it
looks. It was introduced after a commit pushed straight to `master` turned eleven clang and macOS
legs red: a new test whose empty-capture lambda instantiated `SetupRunner` for the first time tripped
`-Wpadded`, and the pre-push check had only run clang over the header — where that template never
gets instantiated at all.

`CI green` is a gate job at the end of `main.yml` that `needs:` every other job in the workflow, so
protection requires one check instead of thirty job names that silently stop being required as legs
get renamed or added. Two parts of it are load-bearing: `if: always()`, because a job whose
dependency failed is *skipped* rather than failed, and the explicit result test, because GitHub
counts a skipped required check as **satisfied** — without both, the gate would go green exactly when
the matrix did not. `lint-ci-gate.py` fails the build if a job is missing from its `needs:`, if an
entry is stale, or if `if: always()` disappears.

Old PRs still carry red and green checks from Travis, Cirrus and AppVeyor. All three are dead for
this repository and their results mean nothing — read the GitHub Actions checks instead.

## Verifying locally

Nothing below replaces the PR: it is how you arrive with a change that has a chance of being green.
`.github/workflows/main.yml` builds every leg the same way, so any of them reproduces locally:

```sh
CXX=<compiler> cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNB_cxx_standard=<std> <cmake_args>
cmake --build build --parallel 4 && ./build/nb        # run from the repo root
```

It covers gcc/clang × C++11..20, 32 bit, libc++, sanitizers, ARM64, macOS, MSVC, clang-cl and
MinGW, plus a `lint` job (pinned clang-format-18 / clang-tidy-18) and a CMake consumer job. What it
cannot cover is the pre-gcc-5 half of `src/scripts/all.sh`, which is what that script is still for.

Two legs exist because a bug hid for years behind everything else being alike, and both need a
container to reproduce:

- `musl libc (Alpine, gcc)` — glibc declares `ioctl()`'s request parameter as `unsigned long`, musl
  as `int`, and `PERF_EVENT_IOC_ID` does not fit in an `int`, so nanobench did not compile on Alpine
  from 2023 until issue #92 was fixed. Every other Linux leg is glibc. It runs Alpine inside a
  container step rather than through the job-level `container:`, because `actions/checkout` needs a
  glibc node.
- `Consumer without RTTI (gcc + clang)` — installing a `std::numpunct` facet goes through
  `__dynamic_cast`, which reads through a null pointer under `-fno-rtti` (issue #122). The number
  formatting groups digits by hand for that reason; keep `std::locale` out of it. The failure was at
  runtime, so the leg runs the binary rather than only compiling it.

Only the `build` legs compile the test suite with the project's flags. `header`, `clang-cl` and
`mingw` build just the header plus a small consumer, because the strict flag set doesn't survive
there: for clang-cl, CMake sets `CMAKE_CXX_COMPILER_ID` to `Clang` **and** `MSVC` to true, so a
CMake build collects the `-Weverything` branch and the `/W4` one at once. Two more traps in that
corner — MSVC forces `/std:c++latest` whatever `NB_cxx_standard` says, and clang-cl targets x64
whatever `msvc-dev-cmd` selected, so its 32 bit leg needs `-m32`.

The sweep worth running locally is the sanitizers and the linters:

```sh
# sanitizers - the project's own option, which is exactly what the two CI legs pass. Do not
# hand-roll `-fsanitize=address,undefined` instead: it is a *weaker* set than this, and the gap is
# not theoretical. float-divide-by-zero is only in the CMake set, and a 0/0 in BigO went green
# locally and red on both sanitizer legs because of precisely that.
# halt_on_error is what makes UBSan fail the run rather than print and exit 0.
# Use gcc here: the clang set adds -fsanitize=integer, which Fedora's libstdc++ 16 trips inside
# stl_uninitialized.h on master as well - a local artifact, not a finding. Container for that leg.
CXX=g++ cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Release -DNB_sanitizer=ON
cmake --build build-san --parallel 4
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ./build-san/nb

# formatting and the version macros. lint-all.py runs every lint-* next to it; CI pins the
# clang-format binary, so pass the same one or the check silently uses a different version.
NANOBENCH_CLANG_FORMAT=clang-format-18 src/scripts/lint/lint-all.py
```

Plus the header compiled warning-free for a consumer, both compilers × C++11..20 — copy those two
commands out of the `header` job rather than duplicating the flag list here, as the copy this file
used to keep drifted from the workflow twice.

**The gap in that sweep that has turned master red.** A syntax-only check over the header never
instantiates a template that only the tests use, so `-Wpadded` and its neighbours stay silent until
the *test suite* is compiled with clang — so build it that way, `CXX=clang++ cmake -S . -B build …`.
That works on this machine since the vendored doctest moved to 2.5.3; with 2.4.11 Fedora's clang
rejected `doctest.h` outright (`__COUNTER__` is "a C2y extension"), which is why this file used to
say the check was impossible here. clang-tidy still is: Fedora's libstdc++ is newer than
clang-tidy-18 can parse, so it fails for reasons that have nothing to do with the change. Use a
container for it, and for a second opinion from the clang version CI pins:

```sh
podman run --rm -v "$PWD:/src" -w /src ubuntu:24.04 bash -c '
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq && apt-get install -y -qq clang-18 clang-tidy-18 cmake make g++
  ln -sf /usr/bin/clang++-18 /usr/bin/clang++
  CXX=clang++ cmake -S . -B b -DCMAKE_BUILD_TYPE=Release && cmake --build b --parallel 4 && ./b/nb
  clang-tidy-18 -p b src/test/app/nanobench.cpp'
```

Six rules for editing the header, each of which costs a round trip when forgotten:

- `-Wfloat-equal` is on, so never compare a double with `==`/`!=` — use `x <= 0.0` and friends.
- A new free function in the implementation block needs a declaration in the declarations block
  above it, or `static`: `-Wmissing-declarations` is on.
- clang-tidy caps cognitive complexity at 25, and `generateResultTag` sits just under it, so added
  branches there have to be lifted into a helper.
- clang-format realigns trailing comments when a value's width changes, which is why bumping the
  version reformats the lines around it.
- gcc's `-Wnoexcept` fails the build on a constructor that only moves members whose own move
  constructors are `noexcept` but is not itself declared so — it shows up as an error inside
  `stl_construct.h` on the vector growth path, not at the constructor. It only fires once the code is
  a plain function: a template instantiated solely in the test TU stays silent, so *moving* a body out
  of a template can surface it. `CompareResult::Entry` cost a leg this way.
- Sphinx roles like `:ref:` and `:cpp:func:` are inert in ordinary doxygen prose — breathe renders
  the text verbatim, so the reader sees the literal characters `:ref:`. They only work inside a
  `@verbatim embed:rst` block, which must **not** be asterisk-prefixed. Grep the built HTML for
  `:cpp:` and `:ref:` after touching a doc comment; there is no warning.

There are **two** clang-format configs: `src/include/.clang-format` (135 columns, the header) and
`src/.clang-format` (80 columns, everything under `src/test/`). `lint-clang-format.py` only looks at
`src/include` and `src/test`, minus `thirdparty/` — so `src/scripts/` and `src/comparisons/` are not
ours to reformat.

Doc comments live in the header. The site at <https://nanobench.ankerl.com/> is built from them by
`.github/workflows/docs.yml` (doxygen + sphinx + breathe) on every push and PR, and deployed from
master, so `docs/` is **not** committed: `src/docs/generate.sh` writes it as a gitignored local
preview and needs doxygen plus the pinned `src/docs/requirements.txt` — `pip install -r` into a venv,
which is what CI does too. Fedora's `python3-sphinx` etc. still work for a look at the prose, but
they are no longer equivalent: F44 packages Sphinx 8.2.3 against the pinned 9.1.0, whose rewritten
search machinery changes `_static` and `searchindex.js`. Cloudflare fronts
the site with a 24h TTL, so a finished deployment can still serve the old page until that expires or
the cache is purged.

`generate.sh` deletes `docs/` and rebuilds it, so a preview server started against the old directory
keeps serving a deleted inode — restart it after every regeneration, and note that `sphinx-build`
succeeding says nothing about the page being *readable*. Two things it will not warn about: the
inert-role trap above, and `sphinx_rtd_theme`'s `white-space: nowrap` on every table cell, which
turns any table with a sentence in it into a horizontal scrollbar. The second is overridden in
`src/docs/_static/css/custom.css`, registered through `html_css_files` — the numeric tables are
unaffected because their cells hold single tokens with nowhere to break.

The version is written down in **three** places — the `ANKERL_NANOBENCH_VERSION_*` macros,
`src/docs/conf.py`, and `project(VERSION)` in `CMakeLists.txt`. `lint-version.py` checks all three
against the macros, so a release bump touches all of them or fails the lint. The generated HTML
carries no version any more, since sphinx-rtd-theme 3.x dropped `display_version`.

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
- An exact `epochIterations()` overrides any calculated count in every state — go through
  `calcNextNumIters()`, never `calcBestNumIters()` directly, or the rule gets a fourth copy.

Performance counters are Linux-only (`perf_event_open`, kernel ≥ 3.3). `read_format` is
`GROUP|ID|TOTAL_TIME_ENABLED|TOTAL_TIME_RUNNING`, so `mCounters` is
`[nr, time_enabled, time_running, (value, id) × nr]`. Three facts worth knowing:
`PERF_EVENT_IOC_RESET` resets the counter values but **not** `time_enabled`/`time_running` (they
accumulate for the event's lifetime — use deltas); the calibrated overhead is a min over 100
empty measurements, subtracted per epoch; and `endMeasure()` scales the values by
`enabled / running` before anyone reads them, so multiplexed counters are extrapolated the way
`perf stat` does it, and the calibration data ends up in the same unit as the measurements.

**Hardware counters are unavailable in most containers/VMs** (`perf_event_open` fails, `mHas` ends up
all false), so changes to that code path cannot be exercised locally there — say so rather than
claiming it was tested.

## Benchmarking claims

Any claim about accuracy needs numbers. A workable ground truth is the minimum of several long
single-shot runs of the same op; compare nanobench's median against it. On a shared VM expect
`err%` ≈ 1% and ~0.5% run-to-run spread of the reported median, so treat sub-1% differences as noise.

## Tests

`src/test/unit_*.cpp` are the tests that assert. `example_*.cpp` and `tutorial_*.cpp` are the samples
the documentation is built from, and running them is what writes `src/docs/_generated`; they assert
nothing on purpose. Leave them that way — the behaviour they demonstrate is covered by the unit
files, and rewriting a sample changes what the site shows.

**Coverage is not the bar — catching a regression is.** At 56 test cases and 88% line coverage this
suite caught **4 of 24** deliberately injected one-line bugs in `nanobench.h`, and two of those four
were caught by the compiler rather than by a test. Line coverage says a line ran, not that anything
would notice it misbehaving. So when adding a test, break the thing it covers — flip a constant in
the header, rebuild, and confirm that test goes red and the others do not. A test that survives that
is decoration. A test that contains a *copy* of the code it checks, as `unit_to_s.cpp` once did,
cannot fail at all.

To measure coverage, build the suite in one go with `--coverage` (out of tree, so the `.gcda` files
and the benchmark artifacts do not land in the repo) and merge it over every TU:

```sh
REPO=$PWD                                 # from the repo root
mkdir -p /tmp/cov && cd /tmp/cov
# absolute source paths on purpose: they make __FILE__ absolute, which is what unit_templates needs
g++ -std=c++17 -O0 -g --coverage -I$REPO/src/include -I$REPO/src/test \
    -o nb-cov $REPO/src/test/app/*.cpp $REPO/src/test/*.cpp && ./nb-cov
# --ignore-errors mismatch is needed for the vendored doctest.h, not for anything of ours
lcov --capture --directory . -o all.info --ignore-errors mismatch,unused,empty,negative,source
lcov --extract all.info '*/nanobench.h' -o nb.info --ignore-errors unused,empty
```

Seven traps, each of which has cost a round trip:

- A test of `compare()` needs **more rounds than the default**, and the more alternatives it compares
  the more. Bonferroni builds each interval at `1 - 0.05/(N-1)`, and a stricter confidence reaches
  further out into the order statistics: at 12 rounds and three comparisons the interval spans the
  middle **75%** of the paired data, so three rounds of the machine misbehaving make it straddle 1.0.
  A clear 2x difference went unresolved on a CI runner for exactly that reason, with the ratio itself
  sitting at 0.51. At 40 rounds the interval spans 38%. Where a test asserts that a difference *is*
  resolved, give it rounds in proportion to the correction, not the default 11.
- Diagnose a red leg from the numbers before fixing it. The failure above looks exactly like
  zero-length epochs being dropped by `pairedLogRatios`, and that reading survives until you check
  the arithmetic: the shortest epoch involved is 25µs against a 20ns clock, so a zero was never
  possible. A fix built on the wrong mechanism is worse than no fix — it adds a permanent guard
  against something that cannot happen, and leaves the real cause in place.

- A test must not relate **two** timing measurements to each other, however obviously equal the work
  is. Comparing the rows of two identical sleeps at different batch sizes passes on an idle Linux box
  and fails elsewhere — macOS went red on it and MSVC reported half the expected ratio, because a
  `sleep_for` overshoots by a platform-dependent and call-dependent amount. Compare a printed number
  against **its own** `Result` instead: exact arithmetic, with only the table's two-decimal rounding
  to allow for. Where a test really is about elapsed time, as in `unit_epoch_time.cpp`, assert a loose
  one-sided bound — a broken clamp moves the runtime by orders of magnitude, so there is nothing to
  gain by being tight and a flaky leg to lose.
- Run the clang-tidy container **before** pushing a header change, not after CI says so. The command
  below takes a couple of minutes from cold and catches what nothing on this machine can: unnamed
  parameters, C-style functional casts like `size_t(0)`, a member that could be `static`, and the
  cognitive-complexity cap. Skipping it has cost two round trips on `lint` alone.
- A new loop written as `while (n-- > 0)` wraps to `UINT64_MAX` on its last turn. That is defined
  behaviour, but `-fsanitize=integer` flags it, so the enclosing function needs
  `ANKERL_NANOBENCH_NO_SANITIZE("integer")` the way `runImpl` has it. Only the *measuring* loops are
  worth that: the attribute switches the check off for the whole function, so on anything that also
  does arithmetic worth checking, count upwards instead. This has cost two round trips, both times
  on a clang sanitizer leg and neither time visible to a plain `-fsanitize=address,undefined`.
- Most of `Result`'s getters are `[[nodiscard]]`, and `ANKERL_NANOBENCH(NODISCARD)` expands to nothing
  before C++17. So `CHECK_THROWS_AS(r.get(2, m), std::out_of_range)` builds clean at the default
  C++11 and fails `-Werror` on every C++17 and C++20 leg. Call such a getter through a lambda inside
  the assertion macros, and build one non-default standard locally before pushing.
- The helpers below `ANKERL_NANOBENCH_IMPLEMENT` — `fmt::Number`, `MarkDownColumn`, the mustache
  parser, `u64` — are visible only in `src/test/app/nanobench.cpp`, and that file is *also* the sole
  source of the installed `nanobench` static library, so putting doctest cases in it would ship test
  code to consumers. Cover them through their output, the way `unit_number_format.cpp` checks the
  digit grouping by reading the rendered table.
- `Result`'s per-measure storage has exactly `Measure::_size` entries, so `get(idx, Measure::_size)`
  indexes one past the end. It is the enum's end marker, not a measure; never pass it.
- The markdown table is not rectangular in the obvious sense. Only the measurement columns are fixed
  width: the last cell holds the title on the header line and the benchmark name on a data line, and
  the separator's last cell is a dash per title character, so it is one character wider than the
  header. What lines up, and what a test should assert, is the offset of the **last** `|`.
