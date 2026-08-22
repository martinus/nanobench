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

Three legs exist because a bug hid for years behind everything else being alike:

- `musl libc (Alpine, gcc)` — glibc declares `ioctl()`'s request parameter as `unsigned long`, musl
  as `int`, and `PERF_EVENT_IOC_ID` does not fit in an `int`, so nanobench did not compile on Alpine
  from 2023 until issue #92 was fixed. Every other Linux leg is glibc. It runs Alpine inside a
  container step rather than through the job-level `container:`, because `actions/checkout` needs a
  glibc node.
- `Android NDK (bionic, cross-compile)` — the third libc, and the third answer to the same `ioctl()`
  question. bionic declares `ioctl()` *twice*, taking `int` and taking `unsigned`, so `&::ioctl` is
  an overload set and the deduction that settles glibc against musl deduces nothing at all;
  nanobench 4.5.0 built for no Android ABI in vcpkg (microsoft/vcpkg#53422). `__BIONIC__` names
  `unsigned` outright instead — bionic's own header points at doing that. The leg cross-compiles all
  four ABIs and does not run them; the runners have no Android, and the failure was a compile error.
  Reproducing it locally needs the NDK, which the runner image ships and this machine does not:
  `curl -LO https://dl.google.com/android/repository/android-ndk-r27c-linux.zip`, unzip, and the
  compilers are `toolchains/llvm/prebuilt/linux-x86_64/bin/<triple><api>-clang++`.
- `Consumer without RTTI (gcc + clang)` — installing a `std::numpunct` facet goes through
  `__dynamic_cast`, which reads through a null pointer under `-fno-rtti` (issue #122). The number
  formatting groups digits by hand for that reason; keep `std::locale` out of it. The failure was at
  runtime, so the leg runs the binary rather than only compiling it.

Only the `build` legs compile the test suite with the project's flags. `header`, `android`,
`clang-cl` and `mingw` build just the header plus a small consumer, because the strict flag set doesn't survive
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
would notice it misbehaving. So when adding a test, break the thing it covers and confirm that test
goes red and the others do not. A test that survives that is decoration. A test that contains a
*copy* of the code it checks, as `unit_to_s.cpp` once did, cannot fail at all.

`src/scripts/mutate/mutate.py` is that check, and it is where the injected-bug campaign above now
lives. Do not hand-roll it with `sed` on the real header: it builds in a throwaway copy, so a run
that dies half way cannot leave a mutated library behind, and it refuses to score anything until the
suite is green first.

```sh
src/scripts/mutate/mutate.py --replace 'OLD CODE' 'NEW CODE'   # does a test catch this bug?
src/scripts/mutate/mutate.py --diff                             # whatever is uncommitted
src/scripts/mutate/mutate.py --diff HEAD~1                      # what did my change leave uncovered?
```

`--bugs` takes a file of them and runs the batch in parallel, `--reverse REF` undoes a whole fix
while keeping its tests, and `--cmake-arg` asks whether some other leg catches it. `--help` has the
grammar and the rest; do not copy it here, or the copies drift.

**The tool is two files, and one of them is shared with unordered_dense and oans.**
`mutate_core.py` is everything that is not about any one project — the lanes, the mutants, the
baseline discipline, the verdicts, the report — and both of those repositories hold a byte-identical
copy. `mutate.py` beside it is nanobench's adapter: the header, that cmake configures the build,
that the binary is `nb` and runs from the build directory, the `ubsan.supp` path, the
performance-counter question, and the measured constants behind `--dry-run`. Roughly 1900 lines
shared against 100 of adapter.

Three build systems and two test runners live in the core, of which this repository uses cmake and
doctest. meson is unordered_dense's; make and minunit are oans's, a C project whose whole suite is
one minunit binary. That is why `Backend.build_argv` takes the argument namespace — make remembers
nothing between invocations, so its pass-through arguments have to ride on the build line — and why
a harness that accepts no filter arguments is not offered `--test-filter` at all: a flag accepted
and then ignored would run the whole suite while the fingerprint claimed otherwise.

The core is *tested over there*, by `scripts/test_mutate.py` — a hermetic suite that covers the
cmake backend this repository drives even though that one builds with meson, and the make backend
and minunit harness that neither of them runs. So a change to the core is only part of a change:
make it in unordered_dense, run that suite, copy the file into all three, and record the new hash in
each `mutate_core.sha256`. `lint-mutate-core.py` fails if this copy has been edited without that
hash moving with it, and separately checks that the adapter still fits the core it is vendored
against — a renamed hook is otherwise silent, and `test_cwd` misspelled leaves `nb` writing its
example artifacts into a lane's source tree.

Two capabilities worth knowing, both of which arrived with the shared core. `--operators` picks what
a sweep changes: `deletions` removes whole statements, which is the shape nearly every hand-written
bug turns out to have — "the code forgot to do this" is never one token — and it costs *less* than
the token sweep, since half of them are rejected by the `-fsyntax-only` pre-filter rather than a
rebuild. `bitwise` mutates `^` and `|`, which the token table leaves alone. And a mutant in a branch
this configuration does not compile is now dropped before it costs a rebuild, with the run saying
which lines those were — this header picks between platforms with `#if` more than most.

Five things about it are worth knowing before trusting a number:

- **Every way this tool has been wrong so far flattered the tests** — it reported "nothing caught it"
  when the fault was its own. That is why the baseline must be green before scoring and why a bug
  block that fails to apply aborts the run rather than substituting nothing and reporting a survivor.
  Distrust a `survived` you cannot explain before distrusting the test.
- **Without performance counters the counter verdicts are meaningless.** Where `perf_event_open` is
  refused — most VMs and containers — nanobench records nothing, so every bug in the `ins`/`cyc`/
  `IPC`/`bra`/`miss` columns comes back `survived` however good the tests are. Every run prints the
  environment it could observe, and says so outright when the counters are missing; read it. This is
  also why it is not a CI gate.
- **It varies configuration, not platform.** The musl, bionic, MSVC and ARM64 questions still need
  the container or CI. On this machine the clang sanitizer leg cannot even baseline, for the
  libstdc++ reason above.
- **A full sweep is tens of minutes**; `--diff` against your own change is the version worth running
  every time — it measures from the merge base, so a branch that has not caught up with master does
  not sweep every line master moved on without it. `--dry-run` sizes either, though its estimate is
  calibrated to one machine.
- **One flaky case stops the whole run**, because the baseline refuses to score until the suite is
  green twice — which is the point of it, not a defect. `unit_compare_calibration_is_not_fooled_by_
  one_slow_reading` is the one that goes red on a loaded machine here, asserting an epoch of at
  least 0.1ms against the 0.2ms it asked for. `--exclude-filter NAME` (doctest's `-tce=`) is the
  honest way past it: the fingerprint then prints what was skipped, where lowering `--baseline-runs`
  would hide it.

What it found when it was written is the standing example of why the distinction matters: the
comparison table's performance counters could be deleted outright and the whole suite stayed green,
because the test asserted that a comparison shows *the same* columns as an ordinary table — true
either way once both lose them.

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
