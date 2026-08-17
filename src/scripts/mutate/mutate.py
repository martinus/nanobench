#!/usr/bin/env python3
"""Mutation testing for nanobench.

Coverage says a line ran. This says something would have noticed it misbehaving.
It breaks `nanobench.h`, rebuilds, runs the suite, and asks whether anything went
red. What nothing notices is a hole in the tests.

Two ways to use it, and the first is the everyday one.

**Put specific bugs back.** The check that decides whether a new test is worth
keeping: break the thing it covers and confirm it goes red. Bugs are independent,
so a batch runs across lanes at once rather than one rebuild after another.

    mutate.py --bugs bugs.txt                  # a file of them, in parallel
    mutate.py --replace OLD NEW                # one, repeatable
    mutate.py --reverse HEAD                   # undo a fix, keep today's tests
    mutate.py --bugs bugs.txt --reuse          # again, against an edited test

`--reuse` keeps the lanes and syncs only what changed next time, which saves the
copying and the configuring and not the compiling: the file every mutant
rewrites is restamped on the way in, so the baseline rebuilds it either way.
Measured here on one bug, 4 lanes: 54s cold against 52s reused, of which the
baseline is 29s. Worth it for a sweep over many lanes, not for a single bug.

A bug file is a name and a block each. Old text must match exactly once, and a
block that does not apply stops the run - otherwise a typo substitutes nothing,
the suite stays green, and the report blames your tests for it:

    # IPC keyed off measured values rather than availability
    <<<
        if (hasIns && hasCyc) {
    ===
        if (rInsMedian > 0.0 && rCycMedian > 0.0) {
    >>>

Bug files worth keeping live in `src/scripts/mutate/bugs/`. They are snapshots
against the code as it was, so one that stops applying is not a failure - it is
the tool saying that part has been rewritten and the questions need re-deriving.

**Or sweep for holes you have not thought of**, changing one place at a time:

    mutate.py --diff                           # whatever is uncommitted
    mutate.py --diff HEAD~1                    # only what that change touched
    mutate.py --lines 2890-2960,3100           # a function, or a scattering
    mutate.py                                  # the whole header
    mutate.py --diff --dry-run                 # how many, and how long

    mutate.py --operators deletions            # what a token sweep cannot ask
    mutate.py --operators bitwise              # ^ and | alone

**What this cannot see here.** Performance counters are Linux-only and refused in
most containers and VMs, and where they are refused nanobench records nothing -
so every mutant in the `ins`/`cyc`/`IPC`/`bra`/`miss` columns comes back
`survived` however good the tests are. The fingerprint printed at the start of
every run says whether this machine has them; read it before believing a
survivor in that code. It is also why this is not a CI gate.

`--cmake-arg` is how you ask a different question - whether some other leg
catches a bug the default Release build cannot see:

    mutate.py --replace OLD NEW --cmake-arg=-DNB_sanitizer=ON
    mutate.py --diff --cmake-arg=-DCMAKE_CXX_COMPILER=clang++

Everything above this line is the same in unordered_dense, and the code that does
it lives in `mutate_core.py`, which is a vendored copy shared with that
repository. What is below is only what this project has to answer for itself.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mutate_core  # noqa: E402 - the path above is what makes it importable


class Nanobench(mutate_core.Project):
    slug = "nanobench"
    repo = os.path.abspath(os.path.join(os.path.dirname(__file__),
                                        "..", "..", ".."))

    # The library is one header, so that is the default target; --file overrides it.
    target = os.path.join("src", "include", "nanobench.h")

    # The one TU that compiles the implementation block. Everything below
    # `ANKERL_NANOBENCH_IMPLEMENT` is in it, which is most of what there is to
    # mutate, and it is also the TU the real build spends its time on.
    syntax_tu = os.path.join("src", "test", "app", "nanobench.cpp")

    build_dir = "build"
    test_binary = "nb"
    backend = mutate_core.CMakeBackend()
    # The suite is built and run the way CI runs it. Not a detail: several tests
    # assert on measured time, and at -O0 an epoch does different work.
    buildtype = "release"

    # Only the *top level* `docs` is skipped - it is the gitignored sphinx
    # preview. `src/docs` must come along: unit_templates compares the built-in
    # templates against `src/docs/_generated`, and a lane without it fails the
    # baseline for a reason that has nothing to do with any mutant. Same for
    # `build*` matching `src/scripts/build.sh` if it were global.
    root_ignore = ("build*", "b", "bsan", "docs")

    lane_bytes = 75 * mutate_core.MIB  # ~2 MB of sources, ~68 MB of build directory

    # A mutant here is one translation unit rebuilt, then a link and a suite run:
    # nearly all serial work inside one lane, so it is the lanes that divide it
    # and not the machine. The floor is what neither divides - at 32 lanes a
    # mutant takes over twice the lane-seconds it takes at 4, because concurrent
    # compiles contend for L2 and a shared L3 long before they run out of cores.
    # Fitted to the 4 lane and 32 lane ends of a full sweep, and it lands within
    # 5% of the 16 lane middle. Setup is almost all baseline, and barely grows
    # with the lane count.
    cpu_seconds_per_mutant = 0.0
    lane_seconds_per_mutant = 6.4
    overhead_seconds_per_mutant = 0.46
    setup_seconds = 15.0
    setup_seconds_per_lane = 0.05

    def test_cwd(self, lane):
        # Running `nb` writes example artifacts - *.json, mustache.*,
        # always_the_same.html - into the current directory, so it runs from the
        # build directory the way CLAUDE.md says to run it by hand. The lane is
        # deleted afterwards either way; what this keeps clean is a lane's own
        # source tree, which sync_tree would otherwise have to delete them from.
        return lane.build

    def lane_env(self, lane_dir):
        return {"UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1:suppressions="
                                 + os.path.join(lane_dir, "ubsan.supp")}

    def sanitizer(self, setup_args):
        # The project's own option rather than a compiler flag, and deliberately
        # so: it is a *stronger* set than a hand-rolled
        # -fsanitize=address,undefined, and float-divide-by-zero is only in it.
        on = any(a.startswith("-DNB_sanitizer=") and not a.endswith(("=OFF", "=off", "=0"))
                 for a in setup_args)
        return "NB_sanitizer=ON" if on else "none"

    def extra_facts(self, args):
        """Whether this machine will let nanobench read performance counters.

        A verdict is only meaningful for the machine that produced it, and the
        way that goes wrong here is silent: where `perf_event_open` is refused -
        most VMs and containers - nanobench records no counters, so every bug in
        the counter columns comes back survived no matter how good the tests
        are. Someone reading that later would conclude the suite is worse than
        it is, which is the one direction this tool must not be wrong in.
        """
        counters = None
        paranoid = "/proc/sys/kernel/perf_event_paranoid"
        if os.path.exists(paranoid):
            try:
                with open(paranoid, encoding="utf-8") as f:
                    counters = int(f.read().strip()) <= 2
            except (OSError, ValueError):
                counters = None
        else:
            counters = False
        return dict(perf_counters=counters)

    def extra_fingerprint(self, facts):
        counters = {True: "yes", False: "no", None: "unknown"}[facts["perf_counters"]]
        rows = ["perf counters   %s" % counters]
        notes = []
        if facts["perf_counters"] is not True:
            notes.append(
                "NOTE: no performance counters here, so nanobench records none\n"
                "      and every bug in the ins/cyc/IPC/bra/miss columns will\n"
                "      look survived whatever the tests do. Those verdicts\n"
                "      mean nothing on this machine.")
        return rows, notes


if __name__ == "__main__":
    mutate_core.run(Nanobench(), __doc__)
