#!/usr/bin/env python3
"""What mutation testing means for nanobench in particular.

`--help` prints the shared manual from mutate_core.py first; this is the part
underneath it. The file mutated by default is `src/include/nanobench.h`, the
suite is the doctest binary `nb`, and cmake configures the lanes.

    src/scripts/mutate/mutate.py --bugs src/scripts/mutate/bugs/env-config.txt
    src/scripts/mutate/mutate.py --replace 'OLD CODE' 'NEW CODE'
    src/scripts/mutate/mutate.py --diff                # whatever is uncommitted
    src/scripts/mutate/mutate.py --lines 2890-2960,3100

**What this cannot see here.** Performance counters are Linux-only and refused in
most containers and VMs, and where they are refused nanobench records nothing -
so every mutant in the `ins`/`cyc`/`IPC`/`bra`/`miss` columns comes back
`survived` however good the tests are. The fingerprint printed at the start of
every run says whether this machine has them; read it before believing a
survivor in that code. It is also why this is not a CI gate.

A mutant here rebuilds one translation unit - everything below
`ANKERL_NANOBENCH_IMPLEMENT` is compiled in exactly one - then links and runs the
suite, so it costs seconds rather than the minutes it costs in unordered_dense.
`--reuse` saves the copying and the configuring and not the compiling: the file
every mutant rewrites is restamped on the way in, so the baseline rebuilds it
either way. Measured on one bug over 4 lanes, 54s cold against 52s reused, of
which the baseline is 29s.

`--cmake-arg` is how you ask a different question - whether some other leg
catches a bug the default Release build cannot see:

    src/scripts/mutate/mutate.py --replace OLD NEW --cmake-arg=-DNB_sanitizer=ON
    src/scripts/mutate/mutate.py --diff --cmake-arg=-DCMAKE_CXX_COMPILER=clang++
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

    test_binary = "nb"
    backend = mutate_core.CMakeBackend()
    harness = mutate_core.DoctestHarness()
    # The suite is built and run the way CI runs it. Not a detail: several tests
    # assert on measured time, and at -O0 an epoch does different work.
    buildtype = "release"

    # Only the *top level* `docs` is skipped - it is the gitignored sphinx
    # preview. `src/docs` must come along: unit_templates compares the built-in
    # templates against `src/docs/_generated`, and a lane without it fails the
    # baseline for a reason that has nothing to do with any mutant. Same for
    # `build*` matching `src/scripts/build.sh` if it were global.
    root_ignore = ("build*", "b", "bsan", "docs")

    # Measured: ~2 MB of sources, ~68 MB of build directory.
    lane_bytes = 75 * mutate_core.MIB

    # A mutant here is one translation unit rebuilt, then a link and a suite run:
    # nearly all serial work inside one lane, so it is the lanes that divide it
    # and not the machine. The floor is what neither divides - at 32 lanes a
    # mutant takes over twice the lane-seconds it takes at 4, because concurrent
    # compiles contend for L2 and a shared L3 long before they run out of cores.
    # Fitted to the 4 lane and 32 lane ends of a full sweep, and it lands within
    # 5% of the 16 lane middle. Setup is almost all baseline, and barely grows
    # with the lane count.
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

    def ubsan_suppressions(self, lane):
        return os.path.join(lane.dir, "ubsan.supp")

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
