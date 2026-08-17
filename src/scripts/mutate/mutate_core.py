#!/usr/bin/env python3
"""The half of mutation testing that is not about any one project.

**This file is vendored.** It is byte-identical in `unordered_dense` and in
`nanobench`, and `lint-mutate-core.py` fails if a copy has been edited in place.
Change it here, re-vendor it there, and re-run both suites - the point of the
sharing is that one test suite covers the code both repositories run, and a
local patch quietly ends that.

What is left to a project is `mutate.py` next to this file: where the header is,
how a build is configured, what the test binary is called, and the constants
that only a measurement on that project can supply. Everything else - the lanes,
the mutants, the baseline discipline, the verdicts and the report - is here.

The manual the two projects share is MANUAL below, which each `mutate.py`'s
`--help` prints ahead of its own project section. It lives there rather than
here so that there is one copy of it rather than one per repository: this
docstring is for whoever opens this file, MANUAL is for whoever runs the tool.
"""

import argparse
import bisect
import concurrent.futures
import contextlib
import filecmp
import fnmatch
import json
import os
import queue
import random
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import threading
import time

MANUAL = """Coverage says a line ran. This says something would have noticed it misbehaving.
It breaks a file, rebuilds, runs the suite, and asks whether anything went red.
What nothing notices is a hole in the tests.

Two ways to use it, and the first is the everyday one.

**Put specific bugs back.** The check that decides whether a new test is worth
keeping: break the thing it covers and confirm it goes red. Bugs are
independent, so a batch runs across lanes at once rather than one rebuild after
another.

    mutate.py --bugs bugs.txt                  # a file of them, in parallel
    mutate.py --replace OLD NEW                # one, repeatable
    mutate.py --reverse HEAD                   # undo a fix, keep today's tests
    mutate.py --bugs bugs.txt --reuse          # again, against an edited test

A bug file is a name and a block each. Old text must match exactly once, and a
block that does not apply stops the run - otherwise a typo substitutes nothing,
the suite stays green, and the report blames your tests for it:

    # the erase moved the last element without fixing up its bucket
    <<<
        auto mh = mixed_hash(get_key(m_values.back()));
    ===
        auto mh = mixed_hash(get_key(*it));
    >>>

Bug files worth keeping live in `bugs/` next to this file. They are snapshots
against the code as it was, so one that stops applying is not a failure - it is
the tool saying that part has been rewritten and the questions need re-deriving.

**Or sweep for holes you have not thought of**, changing one place at a time:

    mutate.py --diff                           # whatever is uncommitted
    mutate.py --diff HEAD~1                    # only what that change touched
    mutate.py --lines 1200-1260,1300           # a function, or a scattering
    mutate.py                                  # the whole file
    mutate.py --diff --dry-run                 # how many, and how long

`--diff` is the everyday one, and it measures from the merge base: on a branch
that has not caught up, comparing against a ref's tip sweeps every line the
mainline moved on without you as though it were yours.

`--operators` picks what to change. `tokens` changes one token at a time,
`bitwise` does the same for `^` and `|` -- the two spellings the token table
leaves alone -- and `deletions` removes whole statements. The default is all of
them, so a plain run asks every question this knows how to ask; naming one
sweeps for that alone, which is the cheaper thing to do and the reason they are
named at all.

Two kinds of mutant are never generated, because nothing could ever catch them.
Comments, string literals and preprocessor lines are not code and are skipped by
the lexer. `std::enable_if_t<..., bool> = true>` is skipped too: the parameter
exists so the substitution has somewhere to fail and nothing reads its value, so
flipping it is a rebuild to prove that nothing happened. A third kind is found
rather than predicted -- a mutant in a branch this configuration does not
compile is dropped once the lanes exist and the preprocessor has been asked
which lines survived, and the run says which lines those were.

The two modes compose, and a change is best asked both questions at once - the
named bugs the tests were written for, and the sweep for what nobody thought to
ask.

Nothing is scored until the suite is green repeatedly, because a flaky test
counted as a kill inflates the number in the flattering direction. The working
tree is never touched: every build happens in a throwaway copy, so a run that
dies half way cannot leave a mutated file behind, and two runs at once get their
own copies rather than deleting each other's.

Every mutant ends in one of five verdicts, and the difference matters.
`compiler` means the build refused it - real protection, but not your tests
doing the work. `caught` means an assertion failed, which is the number worth
moving. `hang` means it ran forever; a mutated loop bound does that rather than
failing. `oom` means it asked for more memory than it was allowed, which is what
a mutated growth policy does. `survived` means nothing noticed, which is the
answer you are looking for. A sixth, `error`, means this tool fell over on that
mutant; it is left out of the score rather than guessed at, and the rest of the
run carries on regardless.

Each lane runs inside a cgroup with a memory cap, because `oom` has to be that
mutant's verdict and not the machine's problem: uncapped, one insert asking for
a terabyte ends with the kernel choosing a victim, and it is as likely to be
another lane, the ninja driving one, or this script. `--memory-limit` sets it,
the default is the smaller of a lane's share of the machine and a gigabyte per
build job, and the run says which up front. Where no cgroup can be had - another
init, no session bus, an undelegated container - the run says that instead of
pretending.

The lanes themselves are the other half of that: a copy of the tree each, in a
workdir that defaults to /tmp, which on most current distributions is a tmpfs -
so `--lanes` buys memory as much as it buys parallelism. The run says how much
room it is about to take and whether that room is RAM, and refuses before
copying rather than half way through. Core dumps are turned off for the same
reason: a crashing mutant is an ordinary verdict here, and each one would
otherwise leave ~30 MB in a lane that is about to be deleted.
"""

VERDICTS = ("caught", "compiler", "hang", "oom", "survived", "error")

MIB = 1024 * 1024
GIB = 1024 * MIB


# ---------------------------------------------------------------- backends

class Backend:
    """How a build is configured, which is nearly all that separates the two
    build systems this has to drive.

    Both end in `ninja -C <build>`, so only the configure step and the spelling
    of "pass this through to it" differ. Both live here rather than one in each
    project's adapter on purpose: the test suite that covers this file runs in
    one repository and has to cover the code the other one executes, and a
    backend that only exists over there is exactly as untested as it was before
    the two tools were merged.
    """

    #: `--meson-arg` / `--cmake-arg`. Named after the tool because that is what
    #: the value is - the help text and both CLAUDE.md files say so.
    name = ""

    @property
    def arg_flag(self):
        return "--%s-arg" % self.name

    def configure_argv(self, args, source, build, configured):
        raise NotImplementedError

    def setup_args(self, args):
        """What this run configures with, for the fingerprint and for `--dry-run`."""
        raise NotImplementedError

    def sanitizer(self, setup_args):
        """Which sanitizer this configuration builds with, as a word for the
        fingerprint.

        Asked of the backend rather than of the project, because the spelling is
        the build system's: meson has one for every project, and a cmake project
        has whatever it called its own switch. The fingerprint prints "no
        sanitizer in this build" off the back of this, and that sentence is a
        claim about what the run could observe - so a backend that cannot know
        says so rather than answering "none".
        """
        return "unknown"


class MesonBackend(Backend):
    name = "meson"

    def setup_args(self, args):
        """What every lane is configured with.

        `-Ddebug=false` is not about debugging: meson's `debug` buildtype means
        -O0 *and* -g, and the debug info is a third of the compile time and two
        thirds of what each lane's build directory would otherwise be - times
        however many lanes. Nothing here reads a symbol. Skipped if the
        pass-through says otherwise, so it stays overridable rather than baked
        in.
        """
        extra = ["--buildtype", args.buildtype]
        if not any(a.startswith("-Ddebug=") for a in args.configure_arg):
            extra.append("-Ddebug=false")
        # A unity build is 2.5x less compiling for the same work -- measured on
        # unordered_dense, one mutant rebuild of the whole suite: 67 CPU-seconds
        # separately, 27 merged. Every translation unit includes the header, and
        # merged into chunks that work is done once per chunk instead of once
        # per file.
        #
        # The usual objection does not apply here. Unity is bad for development
        # because touching one file recompiles its whole chunk; a mutant
        # recompiles every file either way, so there is nothing left to spoil.
        #
        # Asked of the project rather than forced, because *whether a tree builds
        # as a unity at all* is not a general fact: an anonymous namespace stops
        # isolating a file once its neighbours share the chunk, and a header with
        # no include guard only collides there. A project that has not proved its
        # tree survives it would get a baseline that fails to compile and the
        # message "fix the tree first".
        if args.unity and not any("unity" in a for a in args.configure_arg):
            extra.append("--unity=on")
        return extra + list(args.configure_arg)

    def configure_argv(self, args, source, build, configured):
        return (["meson", "setup"] + self.setup_args(args)
                + (["--reconfigure"] if configured else []) + [build, source])

    def sanitizer(self, setup_args):
        return next((a.split("=", 1)[1] for a in setup_args
                     if a.startswith("-Db_sanitize=")), "none")


class CMakeBackend(Backend):
    name = "cmake"

    # `--buildtype` is one shared flag over two vocabularies, and the translation
    # has to be a table rather than `.capitalize()`. cmake accepts an unknown
    # CMAKE_BUILD_TYPE *silently*, dropping every per-configuration flag with it -
    # so `debugoptimized` would configure a lane with no optimisation at all, and
    # a project whose tests assert on measured time would then produce a wall of
    # verdicts about the wrong binary. Anything not in here is refused instead.
    BUILD_TYPES = {"debug": "Debug", "release": "Release",
                   "debugoptimized": "RelWithDebInfo", "minsize": "MinSizeRel"}

    def build_type(self, buildtype):
        try:
            return self.BUILD_TYPES[buildtype.lower()]
        except KeyError:
            raise RuntimeError(
                "cmake has no configuration for --buildtype %s. Known: %s. It "
                "would be accepted silently and drop every optimisation flag "
                "with it, so it is refused here."
                % (buildtype, ", ".join(sorted(self.BUILD_TYPES))))

    def setup_args(self, args):
        # Ninja because the lanes rebuild constantly and nothing else here
        # depends on the generator, and the compile database because the syntax
        # pre-filter is read out of it rather than reassembled by hand - see
        # Lane._syntax_command for why that matters.
        extra = ["-GNinja",
                 "-DCMAKE_BUILD_TYPE=" + self.build_type(args.buildtype),
                 "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
        return extra + list(args.configure_arg)

    def configure_argv(self, args, source, build, configured):
        # cmake re-runs in place, so there is nothing to say about an existing
        # build directory the way meson needs `--reconfigure`.
        return ["cmake", "-S", source, "-B", build] + self.setup_args(args)


# ---------------------------------------------------------------- projects

class Project:
    """Everything this tool cannot know without being told which repository it
    is in. A subclass in `mutate.py` fills it in; the defaults here are the
    answers that are the same everywhere.
    """

    #: Prefixes the temporary directories, so two projects' lanes are told apart
    #: in /tmp and a stale reuse directory names the tree it belongs to.
    slug = "mutate"
    #: Absolute path of the repository root.
    repo = ""
    #: Repo-relative file `--file` defaults to: the library, for a header-only one.
    target = ""
    #: The TU the syntax pre-filter compiles when `target` is what is mutated. It
    #: wants to be a file that *instantiates* the library rather than merely
    #: including it - a mutation inside a member template is a parse error either
    #: way, but one that only breaks type checking is not seen until something
    #: instantiates it.
    syntax_tu = None
    #: Where a lane configures its build, relative to the lane.
    build_dir = "build"
    #: The test binary, relative to the build directory.
    test_binary = ""
    #: A MesonBackend or a CMakeBackend. No default: which build system a project
    #: uses is the most consequential thing it has to say, and inheriting one
    #: silently is how the wrong tool gets run against the right tree.
    backend = None
    #: What `--buildtype` defaults to. A project whose suite only asserts what it
    #: asserts in an optimised build has to say so.
    buildtype = "debug"
    #: Whether lanes merge the translation units. Worth 2.5x on a many-TU project
    #: and nothing on a single-TU one, and only the project knows whether its tree
    #: survives it - see MesonBackend.setup_args.
    unity = False

    #: Never copied into a lane. Build directories go in `root_ignore` instead
    #: when their names are ordinary words, since a pattern matching `build`
    #: everywhere also eats `scripts/build.py`.
    ignore = (".git", "__pycache__", ".cache", ".ccache", ".vscode",
              "*.pcm", "*.o", "a.out", "compile_commands.json")
    root_ignore = ()

    #: Roughly what a lane occupies once it has been built in, for the check that
    #: refuses before copying. Sources plus a build directory. A floor rather than
    #: a considered value - a project that has looked should say what it saw.
    lane_bytes = 64 * MIB

    # The cost model behind --dry-run, in three per-mutant terms because projects
    # differ in which one dominates. A many-TU project spends CPU-seconds that no
    # number of lanes creates or removes, so that term is divided by the
    # *machine*; a single-TU project spends serial seconds inside one lane, so
    # that term is divided by the lanes; and the last is what neither divides -
    # the loss to cache and memory contention when every lane compiles at once.
    #
    # All five default to zero on purpose. They are measurements, and one
    # project's numbers standing in for another's would make --dry-run confidently
    # wrong rather than obviously unanswered; a project that has measured nothing
    # gets an estimate of nothing.
    cpu_seconds_per_mutant = 0.0
    lane_seconds_per_mutant = 0.0
    overhead_seconds_per_mutant = 0.0
    setup_seconds = 0.0
    setup_seconds_per_lane = 0.0

    def problems(self, repo=None):
        """Everything wrong with this project object, as sentences.

        Here rather than in each repository's lint because none of it is about
        any one project, and because the half that was written first lived in
        nanobench's lint - the repository with no test suite, so the checks that
        exist to catch a silent failure were themselves uncovered.

        Every one of these is silent at runtime. A `syntax_tu` naming a file that
        has moved makes the pre-filter borrow another TU's flags and swap in a
        path that does not exist, so it fails for every mutant and the whole run
        reports `compiler` - a verdict in the flattering direction, which is the
        one way this tool must not be wrong.
        """
        repo = repo or self.repo
        found = []
        for attribute in ("slug", "repo", "target", "build_dir", "test_binary"):
            if not getattr(self, attribute, None):
                found.append("%s is empty, and nothing else can fill it in" % attribute)
        if self.backend is None:
            found.append("no backend: say MesonBackend() or CMakeBackend()")
        if repo and not os.path.isdir(repo):
            found.append("repo %s is not a directory" % repo)
        elif repo:
            for attribute in ("target", "syntax_tu"):
                named = getattr(self, attribute, None)
                if named and not os.path.exists(os.path.join(repo, named)):
                    found.append("%s names %s, which does not exist" % (attribute, named))
        return found

    def lane_env(self, lane):
        """Environment a lane needs beyond the defaults.

        Applied with setdefault, so what the caller already exported still wins.
        A sanitizer suppressions file goes through `ubsan_suppressions` instead -
        setting UBSAN_OPTIONS here would replace the core's own options rather
        than adding to them.
        """
        return {}

    def ubsan_suppressions(self, lane):
        """A suppressions file for this lane, or None.

        Separate from `lane_env` because UBSAN_OPTIONS is not a project's to own:
        `halt_on_error=1` is what makes UBSan fail a run rather than print and
        exit 0, and a project that set the variable wholesale would drop it - and
        with it every mutant only UBSan can see, reported as survivors.
        """
        return None

    def test_cwd(self, lane):
        """Where the test binary runs. The lane's source tree unless a project
        says otherwise - which it has to when the suite writes artifacts into the
        working directory, or reads a path it derived from one."""
        return lane.dir

    def default_syntax_tu(self, path):
        """The pre-filter TU for whatever `--file` names.

        Mutating a header the TU includes is the case it was built for; mutating
        a TU means checking that TU itself; and against anything else it would
        compile something the mutation cannot reach, pass every time, and quietly
        stop filtering - so it is turned off instead.
        """
        if path == self.target:
            return self.syntax_tu
        if path.endswith((".cpp", ".cc", ".cxx")):
            return path
        return None

    def sanitizer(self, setup_args):
        """Which sanitizer this configuration builds with, as a word for the
        fingerprint. The build system knows the common spelling; a project that
        wraps it in an option of its own overrides this."""
        return self.backend.sanitizer(setup_args)

    def extra_facts(self, args):
        """Anything else this run's verdicts depend on, for the fingerprint."""
        return {}

    def extra_fingerprint(self, facts):
        """(rows, notes) to add to the fingerprint: rows join the table, notes
        are the paragraphs under it that say what this run cannot observe."""
        return [], []


# ---------------------------------------------------------------- lanes

def lane_ignore_for(project):
    """`shutil.copytree`'s ignore callback for this project's lanes.

    `root_ignore` is matched at the repository root only, and is where build
    directories belong: their names are ordinary words, so a pattern matching
    `build` everywhere also eats `scripts/build.sh`, and one matching `docs`
    everywhere also eats the `src/docs` a test reads its expected output from.
    Both of those are real - one from each project.
    """
    patterns = shutil.ignore_patterns(*project.ignore)
    root = os.path.abspath(project.repo)

    def ignore(directory, names):
        skip = list(patterns(directory, names))
        if os.path.abspath(directory) == root:
            for pattern in project.root_ignore:
                skip.extend(fnmatch.filter(names, pattern))
        return skip

    return ignore


def sync_tree(src, dst, ignore):
    """Bring an existing lane back in line with the repo, copying only what
    actually changed.

    The point is the mtimes. A wholesale re-copy would restamp every source and
    cost a full rebuild in every lane, which is most of what reuse was meant to
    save - so a file that matches is left alone, and ninja rebuilds exactly what
    the working tree touched since last time.

    Sameness is decided by *content*, and a file that differs is stamped with
    the current time rather than the repo's. Both halves are load-bearing.
    Comparing mtimes would call the previous run's mutated header a change on
    every file in the tree; and copying the repo's older mtime onto it would
    leave the lane's object files looking newer than their source, so ninja
    would build nothing and the suite would run against the last mutant."""
    for root, dirs, files in os.walk(src):
        skip = set(ignore(root, dirs + files))
        dirs[:] = [d for d in dirs if d not in skip]
        rel = os.path.relpath(root, src)
        target = dst if "." == rel else os.path.join(dst, rel)
        os.makedirs(target, exist_ok=True)
        wanted = set()
        for name in files:
            if name in skip:
                continue
            wanted.add(name)
            source, copy = os.path.join(root, name), os.path.join(target, name)
            if os.path.exists(copy) and filecmp.cmp(source, copy, shallow=False):
                continue
            shutil.copyfile(source, copy)
            os.utime(copy, None)
        # A file deleted from the repo has to go, or it keeps being compiled.
        # Only files: the lane's build directory is a directory and is not in
        # the source tree, so it is never a candidate.
        for name in os.listdir(target):
            stale = os.path.join(target, name)
            if os.path.isfile(stale) and name not in wanted:
                os.remove(stale)


def estimate_seconds(project, count, lanes, cores):
    lanes, cores = max(1, lanes), max(1, cores)
    return (project.setup_seconds + project.setup_seconds_per_lane * lanes
            + count * (project.cpu_seconds_per_mutant / cores
                       + project.lane_seconds_per_mutant / lanes
                       + project.overhead_seconds_per_mutant))


def estimate(project, count, lanes, cores):
    seconds = estimate_seconds(project, count, lanes, cores)
    if seconds > 5400:
        return "%.1f h" % (seconds / 3600)
    return "%.0f min" % (seconds / 60) if seconds > 90 else "%.0f s" % seconds


# ---------------------------------------------------------------- memory

# Why there is a cap at all, because it is not the obvious one. A suite is tiny -
# tens of megabytes - and one -O0 translation unit peaks at a couple of hundred.
# Nothing honest here needs a gigabyte. What needs the cap is the mutant: a
# growth policy, a bucket mask, an iteration count or a shift amount moved by one
# turns an allocation into a request for more memory than the machine has, and
# uncapped that ends with the kernel's OOM killer choosing a victim - as likely
# to be another lane, the ninja driving one, or this script as the mutant that
# caused it. One runaway mutant then corrupts every verdict running beside it,
# which is the failure this tool exists to avoid.
#
# The second reason is quieter: lanes are sized off the core count without anyone
# asking what that costs in RAM, and 32 lanes each building is 32 compilers at
# once. Capping each lane at its share of the machine makes that arithmetic
# explicit instead of hoping.

# What a build is never capped below, whatever the cap says. Every build job is a
# compiler and a translation unit peaks at ~185 MB, so a cap under that would
# report `oom` for every mutant and blame a growth policy for this tool's own
# arithmetic. Half a gigabyte a job is that figure with room for the link, which
# is the one step holding all of it at once.
MEMORY_PER_JOB = 512 * MIB

SIZE = re.compile(r"^(\d+(?:\.\d+)?)\s*([KMGT]?)(?:i?B)?$", re.IGNORECASE)
SIZE_UNITS = {"": 1, "K": 1024, "M": MIB, "G": GIB, "T": 1024 * GIB}


def parse_size(text):
    """A size like `4G`, `512M` or `0`, in bytes. Bare digits are bytes."""
    m = SIZE.match(text.strip())
    if not m:
        raise argparse.ArgumentTypeError(
            "%r is not a size - write it as 4G, 512M or 0" % text)
    return int(float(m.group(1)) * SIZE_UNITS[m.group(2).upper()])


def human(size):
    if not size:
        return "off"
    return ("%.1f GiB" % (size / GIB)) if size >= GIB else ("%.0f MiB" % (size / MIB))


def total_memory():
    """Bytes this machine will actually hand out, or 0 if it will not say."""
    limits = []
    try:
        limits.append(os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES"))
    except (ValueError, OSError, AttributeError):
        pass
    # Inside a container the cgroup limit is the real answer and physical RAM is
    # the host's. `memory.max` is readable at this path only when the cgroup
    # namespace root is our own cgroup - which is exactly when that is the case.
    try:
        with open("/sys/fs/cgroup/memory.max", encoding="utf-8") as f:
            limits.append(int(f.read().strip()))
    except (OSError, ValueError):
        pass
    return min(limits) if limits else 0


def default_memory_limit(lanes, jobs, total=None):
    """What one lane may use: the smaller of its share of the machine and what a
    lane can honestly need.

    The share is what keeps the lanes together inside the machine; four fifths
    of it, because the page cache holding a lane's objects is not free either.
    The honest figure is a gigabyte per build job - twice the floor a build is
    never capped below, so an ordinary one never comes near it. Whichever is
    smaller is still orders of magnitude above what a suite itself uses, which
    is the only number a mutant has to beat to be called runaway.
    """
    limits = [max(2, jobs) * 2 * MEMORY_PER_JOB]
    total = total_memory() if total is None else total
    if total:
        limits.append(int(0.8 * total) // max(1, lanes))
    return max(GIB, min(limits))


def build_memory_limit(limit, jobs):
    """The cap a build gets, which is the lane's unless its jobs need more.

    Raising it here rather than raising the lane's keeps the two questions
    apart. What a build needs is known - it is the job count times a compiler -
    and what a mutant needs is the thing being asked about, so an explicit
    `--memory-limit` tightens the second without breaking the first. In
    aggregate nothing is given away either: lanes times jobs is the core count,
    so every lane building at its floor at once is what a plain `ninja` on this
    machine would use anyway.
    """
    return max(limit, jobs * MEMORY_PER_JOB) if limit else 0


def memory_capped(argv, limit):
    """`argv` inside a cgroup that caps it, when there is one to be had.

    `systemd-run --scope` puts the command - and everything it goes on to spawn,
    so ninja's compilers too - into a transient cgroup with MemoryMax set, and
    the kernel stops the lot the moment they exceed it. Swap is pinned to zero
    as well: without that a runaway first fills whatever swap the machine has,
    and on a box already swapping that is minutes of thrashing slowing every
    other lane down before anything is killed at all.

    A scope rather than a service, which is what makes this a wrapper and not a
    different way of running things: the command stays a child of this process,
    so its stdout, its environment, its working directory and its exit status
    all arrive exactly as they would without it.
    """
    if not limit:
        return list(argv)
    return ["systemd-run", "--user", "--scope", "--quiet", "--collect",
            "-p", "MemoryMax=%d" % limit, "-p", "MemorySwapMax=0",
            "--"] + list(argv)


def memory_cap_reason(limit):
    """Why the cap cannot be had here, or "" when it can.

    It needs systemd, a session bus and the memory controller delegated to the
    user - a container, a CI runner or a machine with another init has some
    subset of those. Asking by trying beats testing for the pieces one at a
    time, and it costs one 5 ms scope for the whole run.
    """
    try:
        r = subprocess.run(memory_capped(["true"], limit), capture_output=True,
                           text=True, timeout=60)
    except FileNotFoundError:
        return "systemd-run is not on PATH"
    except (OSError, subprocess.SubprocessError) as e:
        return "systemd-run: %s" % e
    if r.returncode == 0:
        return ""
    # Its own complaint says which piece is missing - "Failed to connect to bus"
    # on a machine with no session, a delegation error on a container - and that
    # is more use than anything this could say in its place.
    said = (r.stderr or r.stdout).strip()
    return said.splitlines()[-1][:100] if said \
        else "systemd-run --user --scope exited %d" % r.returncode


# A cgroup that goes over its limit is stopped, and which signal arrives says
# only which half of the kernel got there first. The kernel OOM-kills the
# offending process outright (-9); when that process was not the one we are
# waiting on, systemd sees a member die and stops the rest of the scope, which
# is a SIGTERM (-15). Nothing this tool runs sends either signal itself, so both
# mean the same thing here: it asked for more than it was allowed.
MEMORY_KILL_SIGNALS = (-9, -15)


def killed_for_memory(proc):
    return proc is not None and proc.returncode in MEMORY_KILL_SIGNALS


def drop_core_dumps():
    """Stop the lanes filling up with cores, which they otherwise do quickly.

    A crashing mutant is an ordinary verdict here - a segfault is one of the
    ways the suite notices - and the default `core_pattern` writes the dump into
    the crashing process's working directory, which is the lane. That is ~30 MB
    of a mutated binary that no longer exists, once per crash, in a directory
    that on most machines is under /tmp and therefore in RAM. Nothing reads
    them: the lane is deleted at the end of the run and the binary they refer to
    was overwritten by the next mutant.

    Lowering a soft limit needs no privilege, and the children inherit it.
    """
    try:
        import resource  # noqa: PLC0415 - POSIX only, and not needed elsewhere
        _, hard = resource.getrlimit(resource.RLIMIT_CORE)
        resource.setrlimit(resource.RLIMIT_CORE, (0, hard))
    except (ImportError, ValueError, OSError):
        pass


def memory_backed(path):
    """Whether writing to `path` costs RAM rather than disk.

    /tmp is a tmpfs on most current distributions, so the default workdir
    usually is - and a lane there is not free the way a directory normally is.
    """
    try:
        with open("/proc/mounts", encoding="utf-8") as f:
            mounts = [line.split()[1:3] for line in f if len(line.split()) > 2]
    except OSError:
        return False
    path, deepest = os.path.abspath(path), ("", "")
    for point, kind in mounts:
        point = point.replace("\\040", " ")
        under = path == point or path.startswith(point.rstrip("/") + "/")
        if under and len(point) >= len(deepest[0]):
            deepest = (point, kind)
    return deepest[1] in ("tmpfs", "ramfs")


# ---------------------------------------------------------------- lexing

def code_mask(src):
    """True for every byte that is real code.

    False inside comments, string/char/raw-string literals and preprocessor
    directives. Mutating those is the main way a token-level mutator wastes its
    budget: a third of a documented header is prose, and the `#define` lines hold
    the version macros that a lint job checks separately.
    """
    mask = bytearray(b"\x01" * len(src))

    def blank(start, end):
        end = min(end, len(src))
        mask[start:end] = b"\x00" * max(0, end - start)

    i, n = 0, len(src)
    at_line_start = True
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ""

        if c in " \t":
            i += 1
            continue

        if c == "\n":
            at_line_start = True
            i += 1
            continue

        # preprocessor directive: the whole logical line, continuations included
        if at_line_start and c == "#":
            j = i
            while j < n:
                if src[j] == "\n" and not (j > 0 and src[j - 1] == "\\"):
                    break
                j += 1
            blank(i, j)
            i = j
            continue

        at_line_start = False

        if c == "/" and nxt == "/":
            j = src.find("\n", i)
            j = n if j < 0 else j
            blank(i, j)
            i = j
            continue

        if c == "/" and nxt == "*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            blank(i, j)
            i = j
            continue

        # raw string: R"delim( ... )delim"
        if c == "R" and nxt == '"':
            m = re.compile(r'R"([^(]*)\(').match(src, i)
            if m:
                close = ')' + m.group(1) + '"'
                j = src.find(close, m.end())
                j = n if j < 0 else j + len(close)
                blank(i, j)
                i = j
                continue

        if c in '"\'':
            quote = c
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == quote:
                    j += 1
                    break
                if src[j] == "\n":  # unterminated, do not run away
                    break
                j += 1
            blank(i, j)
            i = j
            continue

        i += 1

    return mask


# ------------------------------------------------------------- operators

# Longest first, so `<=` is never split into `<`. Shifts, `->` and `::` are left
# alone on purpose: mutating them is a compile error essentially every time, and
# a mutant that cannot build costs a rebuild to tell us nothing. Shifts are the
# painful exclusion here - a header shifts to build a fingerprint and to size an
# array - but `<<` to `>>` is caught by the compiler in the places it is not
# caught by every test at once, and a shift *amount* is a number, which is
# mutated.
OPERATOR_MUTATIONS = [
    ("<<=", []), (">>=", []), ("<<", []), (">>", []), ("->", []), ("::", []),
    ("<=", ["<", ">=", "=="]),
    (">=", [">", "<=", "=="]),
    ("==", ["!="]),
    ("!=", ["=="]),
    ("&&", ["||"]),
    ("||", ["&&"]),
    ("++", ["--"]),
    ("--", ["++"]),
    ("+=", ["-="]),
    ("-=", ["+="]),
    ("*=", ["/="]),
    ("/=", ["*="]),
    ("<", ["<=", ">"]),
    (">", [">=", "<"]),
    ("+", ["-"]),
    ("-", ["+"]),
    ("*", ["/"]),
    ("/", ["*"]),
]

# Bitwise, kept out of the table above so that it can be asked for on its own.
#
# `&` is missing on purpose and for the same reason `->` and `::` are: it is three
# operators sharing a spelling -- bitwise and, address-of, and the reference
# declarator in `auto& x` -- and only a parser can tell them apart. `^` and `|`
# have one meaning each, so they cost nothing to be sure about.
#
# The empty entries are consumers, not omissions: `||` has to be matched before
# the `|` inside it, or a mutant turns `a || b` into `a &| b`, which is a rebuild
# spent on a syntax error. The table is read in order and the first match wins.
BITWISE_MUTATIONS = [
    ("||", []),
    ("|=", ["&=", "^="]),
    ("^=", ["&=", "|="]),
    ("^", ["&", "|"]),
    ("|", ["&", "^"]),
]

# `# 123 "some/file.h" 1` -- what -E emits when the next line it prints is not
# the one that would follow. The path may be quoted with escapes; nothing here
# has one, and realpath on a mangled path simply fails to match.
LINEMARKER = re.compile(r'^#\s+(\d+)\s+"([^"]*)"')

IDENT = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")
# integer literal, not part of an identifier or a float/exponent
NUMBER = re.compile(r"\b(\d+)([uUlL]*)\b")

WORD_MUTATIONS = {"true": ["false"], "false": ["true"]}

COMPOUND_OPERATOR_TAIL = "=!<>+-*/&|^%"


def nonspace_before(src, offset):
    while offset > 0 and src[offset - 1] in " \t\n":
        offset -= 1
    return (src[offset - 1], offset - 1) if offset > 0 else ("", -1)


def nonspace_after(src, offset):
    n = len(src)
    while offset < n and src[offset] in " \t\n":
        offset += 1
    return src[offset] if offset < n else ""


def is_template_default(src, offset, word):
    """Whether this `true`/`false` is a template parameter's default value.

    `std::enable_if_t<is_map_v<Q>, bool> = true>` is the SFINAE idiom: the
    parameter exists only so the substitution has somewhere to fail, and nothing
    ever reads its value, so flipping it is a mutant that cannot be killed. One
    header has 47 of them and every one costs a full rebuild to prove that
    nothing happened.

    Recognised by shape rather than by looking for `enable_if_t`, because the
    idiom is the default value in a `>`-terminated list and not any particular
    trait: preceded by an `=` that is an assignment rather than the tail of `==`
    or `<=`, and followed by the `>` that closes the template parameter list.
    """
    before, at = nonspace_before(src, offset)
    if "=" != before:
        return False
    # Only an *adjacent* character makes the `=` the tail of `==`, `<=` or `>=`.
    # The one that closes `bool>` sits a space away and is the usual sight here,
    # so testing for it without asking about the space rejects every real case.
    prev, prev_at = nonspace_before(src, at)
    if prev_at == at - 1 and prev in COMPOUND_OPERATOR_TAIL:
        return False
    return ">" == nonspace_after(src, offset + len(word))


def is_comparison(src, offset, op):
    """Whether a bare `<`/`>` is a comparison rather than a template bracket.

    Most sites are angle brackets - `std::is_same<`, `static_cast<size_t>`,
    `template <class Key>` - and mutating one is a compile error every time.
    They are not free: each costs a syntax check, and they swamp the `compiler`
    bucket, which is the one the report explicitly calls not the number worth
    moving. These headers are clang-formatted, so a comparison always has a
    space on both sides and a template bracket never does.
    """
    if op not in ("<", ">"):
        return True
    before = src[offset - 1] if offset else ""
    after = src[offset + 1] if offset + 1 < len(src) else ""
    return before == " " and after == " "


def line_starts(src):
    starts = [0]
    start = src.find("\n")
    while start >= 0:
        starts.append(start + 1)
        start = src.find("\n", start + 1)
    return starts


def mutation_sites(src, mask, line_filter=None, skipped=None,
                   operator_table=OPERATOR_MUTATIONS, words_and_numbers=True):
    """Every single-token change worth trying, as {offset, original, ...} dicts.

    `skipped` collects the sites left out as provably equivalent, so the caller can say how many
    there were. Every other "we will not run this" rule in this file reports itself -- see
    drop_uncompiled -- and a rule that recognises an idiom by shape is exactly the one that should,
    because the shape is wider than the idiom and a real hole would be indistinguishable from
    silence.

    `operator_table` and `words_and_numbers` are what let one scanner serve two named operators:
    `tokens` takes the defaults, `bitwise` goes through bitwise_sites below. No spelling *mutates*
    in both tables -- `||` is in both, but only as a consumer -- so asking for both operators
    produces no duplicate site."""
    sites = []
    n = len(src)
    starts = line_starts(src)

    def line_of(off):
        return bisect.bisect_right(starts, off)

    def add(offset, original, replacement):
        line = line_of(offset)
        if line_filter is not None and line not in line_filter:
            return
        sites.append(dict(offset=offset, original=original,
                          replacement=replacement, line=line,
                          description="%s -> %s" % (original, replacement)))

    i = 0
    while i < n:
        if not mask[i]:
            i += 1
            continue

        m = IDENT.match(src, i) if words_and_numbers else None
        if m:
            word = m.group(0)
            # The guard is inside the lookup because only `true` and `false` can produce a mutation
            # at all, and it scans whitespace either side of whatever it is given.
            if word in WORD_MUTATIONS:
                if is_template_default(src, i, word):
                    if skipped is not None:
                        skipped.append(line_of(i))
                else:
                    for rep in WORD_MUTATIONS[word]:
                        add(i, word, rep)
            i = m.end()
            continue

        m = NUMBER.match(src, i) if words_and_numbers else None
        if m:
            # A float like 1.5 or 0.8 must not be picked apart into an int. A
            # number at the very start or end of the file has no neighbour, and
            # the sentinel is what stands in for one: `"" in ".0123456789"` is
            # True, so an empty string here would reject the mutation instead.
            before = src[i - 1] if i else "\n"
            after = src[m.end()] if m.end() < n else "\n"
            if before not in ".0123456789" and after not in ".eE0123456789":
                value, suffix = int(m.group(1)), m.group(2)
                for rep_val in (value + 1, value - 1):
                    if rep_val >= 0:
                        add(i, m.group(0), "%d%s" % (rep_val, suffix))
            i = m.end()
            continue

        for op, reps in operator_table:
            if src.startswith(op, i):
                if is_comparison(src, i, op):
                    for rep in reps:
                        add(i, op, rep)
                i += len(op)
                break
        else:
            i += 1

    return sites


def bitwise_sites(src, mask, line_filter=None):
    """`^` and `|`, the two spellings the token table leaves alone.

    Named rather than left as two arguments at the call site, so that "bitwise means this
    table *and* no words or numbers" is written once. Passing only the table would quietly
    re-answer every `true`, every integer and every comparison in the file, which is the one
    thing asking for this operator alone is meant to avoid."""
    return mutation_sites(src, mask, line_filter,
                          operator_table=BITWISE_MUTATIONS, words_and_numbers=False)


OPERATORS = ("tokens", "bitwise", "deletions")


def parse_operators(text):
    """Which mutations to make, as a set. Named rather than a pair of flags so
    that asking for one is not spelled as turning the other off."""
    wanted = {part.strip() for part in text.split(",") if part.strip()}
    unknown = sorted(wanted - set(OPERATORS))
    if unknown or not wanted:
        raise argparse.ArgumentTypeError(
            "%s - the operators are %s" % ("no operators given" if not wanted
                                           else "unknown: " + ", ".join(unknown),
                                           ", ".join(OPERATORS)))
    return wanted


LINE_PART = re.compile(r"^(\d+)(?:-(\d+))?$")


def parse_lines(text):
    """Line numbers from `1290`, `1278-1290`, or any comma-separated mix.

    The list form is what makes a second pass over the first pass's survivors one
    run instead of one per line. Survivors land where they land - a handful of
    lines scattered over three thousand - and the alternatives are both bad: a
    range wide enough to cover them re-answers hundreds of mutants that were
    answered already, and one run per line pays for lanes and a baseline every
    time.

    Matched whole rather than split on the first dash, because every loose
    reading of a part is a silent one: `1-` would be line 1 and not "from 1
    onwards", `20-10` would be no lines at all, and a run that sweeps less than
    it was asked to reports a clean result for lines nobody looked at.
    """
    lines = set()
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        m = LINE_PART.match(part)
        if not m:
            raise argparse.ArgumentTypeError(
                "%r is not a line or a range - write it as 1290, 1278-1290, or "
                "a comma-separated mix" % part)
        first, last = int(m.group(1)), int(m.group(2) or m.group(1))
        if last < first:
            raise argparse.ArgumentTypeError(
                "%r ends before it starts" % part)
        lines.update(range(first, last + 1))
    return lines


# Lines that end in `;` without being a statement anyone can drop: a closing
# brace of an initializer, a declaration the rest of the scope needs, a label.
# Over-matching is not expensive - a deletion that does not compile is rejected
# by the pre-filter in half a second - but a deletion that cannot compile is
# also a mutant that tells you nothing, so the obvious ones are left out.
NOT_A_STATEMENT = re.compile(
    r"^(\}|\{|else\b|do\b|try\b|catch\b|template\b|using\b|typedef\b|namespace\b"
    r"|struct\b|class\b|enum\b|static_assert\b|public:|private:|protected:|friend\b)")


def deletion_sites(src, mask, line_filter=None):
    """Whole statements, taken out.

    The operator the token sweep cannot express, and the one the hand-written
    bugs kept turning out to be. Nearly every bug in a hand-written bug file is
    some form of "the code forgot to do this": the shift down that never
    happens, the pop_back that is skipped, the bucket that is never repointed.
    None of those is one token, and none of them is reachable by changing one.

    Only single-line statements, and only ones whose brackets balance on that
    line, so that what is removed is a whole statement rather than the middle of
    one. That leaves out the multi-line calls, which is a real gap and the price
    of not parsing C++.

    Plenty of these will not compile -- a declaration something below it uses, a
    return from a function that has to return something. That is the pre-filter's
    half second rather than a full rebuild, so the budget stands it; what it
    costs is a report with a larger `compiler` column.
    """
    sites = []
    starts = line_starts(src)
    for index, start in enumerate(starts):
        end = starts[index + 1] if index + 1 < len(starts) else len(src)
        lineno = index + 1
        # Asked first: in --diff mode the filter is a handful of lines, and everything below is
        # per-character work over the whole file.
        if line_filter is not None and lineno not in line_filter:
            continue
        line = src[start:end].rstrip("\n")
        # Read through the mask so that a trailing comment does not decide
        # whether this looks like a statement.
        code = "".join(c if mask[start + k] else " " for k, c in enumerate(line))
        stripped = code.strip()
        if not stripped.endswith(";") or NOT_A_STATEMENT.match(stripped):
            continue
        if code.count("(") != code.count(")") or code.count("{") != code.count("}"):
            continue
        text = line.lstrip()
        sites.append(dict(offset=start + (len(line) - len(text)), original=text, replacement="", line=lineno,
                          description="delete: %s" % (stripped[:52] + ("..." if len(stripped) > 52 else ""))))
    return sites


def changed_lines(repo, ref, path):
    """Line numbers of `path` touched since `ref`, for the fast daily mode.

    Against the merge base rather than the ref's tip, which is the difference
    between "what did I change" and "what does this file look like compared to
    over there". On a branch that has not caught up with the mainline the second
    answer includes every line it moved on without you -- lines you have never
    seen, swept as though they were yours. For a ref that is already an ancestor,
    which is what `HEAD` and `HEAD~1` are, the merge base is the ref itself and
    the two readings are the same.

    Falls back to the plain form where git is too old to know the flag: the
    answer is then merely too wide, which costs time rather than correctness.
    """
    argv = ["git", "diff", "--unified=0", "--merge-base", ref, "--", path]
    r = subprocess.run(argv, cwd=repo, capture_output=True, text=True)
    if r.returncode != 0:
        argv.remove("--merge-base")
        r = subprocess.run(argv, cwd=repo, capture_output=True, text=True, check=True)
    out = r.stdout
    lines = set()
    for hunk in re.finditer(r"^@@ -\S+ \+(\d+)(?:,(\d+))? @@", out, re.M):
        start = int(hunk.group(1))
        count = int(hunk.group(2) or 1)
        lines.update(range(start, start + count))
    return lines


# ---------------------------------------------------------------- running

def count_test_cases(stdout):
    """How many cases doctest actually ran, or None if it never got that far.

    The number this guards against is zero. doctest's suites are the ones a
    `TEST_SUITE` names and not the build system's, which typically calls the
    whole binary one test; ask for a suite that does not exist and doctest skips
    every case and exits 0. Every mutant then comes back `survived` and the
    report reads as a suite full of holes.
    """
    m = re.search(r"^\[doctest\] test cases:\s*(\d+)", stdout, re.M)
    return int(m.group(1)) if m else None


def parse_test_output(proc):
    """Which tests went red, given a finished run of the suite.

    Kept out of Lane so that spawning a process and interpreting doctest's
    output stay separable - the second is the part that changes when a mutant
    dies in a way doctest has no vocabulary for.
    """
    if proc.returncode == 0:
        return []

    # doctest prints a `TEST CASE:` banner for anything that produces output, a
    # passing MESSAGE included, so the banners alone are not failures - only one
    # with an assertion error under it counts.
    failing, current = [], None
    for line in proc.stdout.splitlines():
        banner = re.match(r"^TEST CASE:\s*(.+)$", line)
        if banner:
            current = banner.group(1).strip()
        elif ("ERROR" in line or "FAILED" in line) and current:
            if current not in failing:
                failing.append(current)
    if failing:
        return failing

    # Nonzero exit with no failed assertion: a sanitizer abort, a crash or a
    # signal. Naming it beats reporting "unknown" - under a sanitizer build the
    # thing that noticed is often not a test at all, and which runtime
    # complained is the whole answer.
    for stream in (proc.stderr, proc.stdout):
        for pattern in (r"^SUMMARY: (\w+Sanitizer): (.+)$", r"runtime error: (.+)$"):
            m = re.search(pattern, stream, re.M)
            if m:
                return [" ".join(m.groups())[:120]]
    return ["exit %d, no assertion failed" % proc.returncode]


def short_test_name(name, limit=52):
    """A doctest case name, shortened for a terminal.

    Where a suite is mostly `TEST_CASE_TEMPLATE`, a name arrives as
    `a_hash_survives_rehashing<ankerl::unordered_dense::v4_9_1::detail::table<
    std::__cxx11::basic_string<char>, int, ...>>` - 300 characters of which the
    first 25 are the part that says what broke. The instantiation is kept as a
    marker rather than dropped, because which one of a template's instantiations
    failed is occasionally the answer, and the full names stay in --json.
    """
    depth, out = 0, []
    for ch in name:
        if ch == "<":
            depth += 1
            if depth == 1:
                out.append("<...>")
        elif ch == ">":
            depth = max(0, depth - 1)
        elif depth == 0:
            out.append(ch)
    short = "".join(out)
    return short if len(short) <= limit else short[:limit - 3] + "..."


def render_caught(caught_by, most=3):
    """The tests that noticed, as much of them as is worth printing."""
    if not caught_by:
        return ""
    shown = ", ".join(short_test_name(t) for t in caught_by[:most])
    extra = len(caught_by) - most
    return shown + (" (+%d more)" % extra if extra > 0 else "")


class Lane:
    """One throwaway copy of the repo, configured once and reused per mutant."""

    def __init__(self, root, index, args, project):
        self.project = project
        self.dir = os.path.join(root, "lane%d" % index)
        self.build = os.path.join(self.dir, project.build_dir)
        self.target = os.path.join(self.dir, args.file)
        self.args = args
        self.jobs = args.jobs
        self.memory = args.memory_limit
        self.test_args = []
        if args.test_suite:
            self.test_args.append("-ts=" + args.test_suite)
        if args.exclude_suite:
            self.test_args.append("-tse=" + args.exclude_suite)
        if args.test_filter:
            self.test_args.append("-tc=" + args.test_filter)
        if args.exclude_filter:
            self.test_args.append("-tce=" + args.exclude_filter)
        self.syntax_cmd = None
        self.syntax_file = args.syntax_tu
        self.ignore = lane_ignore_for(project)

        self.env = dict(os.environ)
        for key, value in project.lane_env(self).items():
            self.env.setdefault(key, value)
        # ccache keys on the compiler command line, and every lane's -I is a
        # different absolute path - base_dir rewrites absolute paths beneath it
        # to relative ones so that every lane hashes the same as the developer's
        # own build. Read-only on purpose: the baseline build is then nearly
        # free, while the mutants - each a preprocessed source nothing will ever
        # see again - do not get to evict a real cache with a whole suite's
        # objects apiece.
        self.env["CCACHE_BASEDIR"] = os.path.abspath(self.dir)
        self.env["CCACHE_READONLY"] = "1"
        # Set unconditionally, exactly as the CI legs do: only a sanitizer
        # runtime reads these, so on an ordinary build they do nothing. Without
        # halt_on_error UBSan prints the error and the binary still exits 0 -
        # and a mutant that only UBSan can see is then reported as a survivor,
        # which is the one way this tool must never be wrong.
        #
        # A project's suppressions file is appended rather than handed the whole
        # variable, so that these two options reach every lane whatever a project
        # has to add - and so that adding a third one here does not silently miss
        # the project that had replaced the variable wholesale.
        ubsan = ["print_stacktrace=1", "halt_on_error=1"]
        suppressions = project.ubsan_suppressions(self)
        if suppressions:
            ubsan.append("suppressions=" + suppressions)
        self.env.setdefault("ASAN_OPTIONS", "detect_stack_use_after_return=1")
        self.env.setdefault("UBSAN_OPTIONS", ":".join(ubsan))

    def setup(self):
        if os.path.isdir(self.dir):
            sync_tree(self.project.repo, self.dir, self.ignore)
            # The mutated file is the one thing whose *content* can be right
            # while the build behind it is wrong: a run that ended with a mutant
            # applied, and was then restored, leaves a binary built from the
            # mutant and a source that matches the repo again. Nothing in a
            # content comparison can see that, so the file that every mutant
            # rewrites is stamped unconditionally. It costs the one rebuild that
            # was going to happen anyway.
            os.utime(self.target, None)
        else:
            shutil.copytree(self.project.repo, self.dir, ignore=self.ignore)
        # Always, even when reusing: it is a second, and it is what picks up a
        # new test file added to the build definition. Sources are listed there
        # by hand, so skipping this would build the previous run's set and score
        # mutants against tests that are not in it.
        configured = os.path.exists(os.path.join(self.build, "build.ninja"))
        cmd = self.project.backend.configure_argv(self.args, self.dir, self.build,
                                                  configured)
        r = subprocess.run(cmd, cwd=self.dir, capture_output=True, text=True,
                           env=self.env)
        if r.returncode != 0:
            raise RuntimeError("%s failed in lane:\n%s%s"
                               % (self.project.backend.name, r.stdout, r.stderr))
        self.syntax_cmd = self._syntax_command()

    def _syntax_command(self):
        """The compile line the build would use for one TU, turned into a syntax check.

        Taken from compile_commands.json rather than reassembled by hand: the
        flags that matter here are the ones the real build uses, -Werror very
        much included, and a hand-written approximation of them drifts the day
        someone adds a warning to the build definition.

        A unity build lists the chunks it generates and not the files they
        include, so the TU asked for is not in there at all. The flags are the
        same for every chunk, so one is borrowed and the source path swapped --
        which matters because the alternative is the pre-filter quietly turning
        itself off, and then every mutant that is not valid C++ costs a full
        rebuild instead of half a second.
        """
        if not self.syntax_file:
            return None
        path = os.path.join(self.build, "compile_commands.json")
        if not os.path.exists(path):
            return None
        with open(path, encoding="utf-8") as f:
            entries = json.load(f)
        wanted = os.path.normpath(os.path.join(self.dir, self.syntax_file))

        def resolved(entry):
            return os.path.normpath(os.path.join(entry["directory"], entry["file"]))

        entry = next((e for e in entries if resolved(e) == wanted), None)
        borrowed = entry is None
        if borrowed:
            # Unity: no entry names this file, so any chunk's flags will do. Prefer one from the
            # same directory tree, which is what keeps a build with several targets from handing
            # over a different target's -I and -D.
            entry = next((e for e in entries
                          if os.path.abspath(e["directory"]) == os.path.abspath(self.build)), None)
        if entry is None:
            return None
        argv, out, i = shlex.split(entry["command"]), [], 0
        swapped = False
        while i < len(argv):
            arg = argv[i]
            # -MF/-MQ name ninja's depfile for this object: writing it from
            # here would leave the real build's dependency information
            # describing a compile that never produced an object.
            if arg in ("-o", "-MF", "-MQ", "-MT"):
                i += 2
                continue
            if arg not in ("-c", "-MD", "-MMD"):
                if borrowed and arg == entry["file"]:
                    arg, swapped = wanted, True
                out.append(arg)
            i += 1
        # ccache refuses -fsyntax-only outright, and there is nothing to
        # cache in a compile that produces no object anyway.
        if out and os.path.basename(out[0]) == "ccache":
            out = out[1:]
        if borrowed and not swapped:
            # The source path in the command was spelled some other way, so the check would
            # have compiled the chunk it was borrowed from instead of the file asked for --
            # and passed, every time, filtering nothing. Better to have no pre-filter than one
            # that always says yes.
            return None
        out.insert(1, "-fsyntax-only")
        return dict(argv=out, cwd=entry["directory"])

    def write_target(self, text):
        with open(self.target, "w", encoding="utf-8") as f:
            f.write(text)

    def run_syntax_check(self, timeout):
        """Cheap reject. Plenty of operator mutants are simply not valid C++, and
        this answers that in half a second instead of a whole rebuild."""
        return self._run(self.syntax_cmd["argv"], cwd=self.syntax_cmd["cwd"],
                         timeout=timeout, env=self.env)

    def compiled_lines(self, timeout):
        """Line numbers of the mutated file that survive the preprocessor.

        A mutant in a branch this configuration does not compile cannot be
        caught by anything, and costs a full rebuild to say so. A function that
        picks between __uint128_t, an MSVC intrinsic and a long-hand multiply is
        the standing example: two thirds of it is never seen here, which is
        dozens of mutants of pure noise in one function.

        Asked of the compiler rather than by matching `#if` in the text, because
        the answer depends on the flags the build actually uses. `-E` emits a
        linemarker whenever the line it is about to print is not the next one,
        so following those and counting lines in between gives what was kept.

        None when there is nothing to ask - no pre-filter TU, or the run was
        told not to use one - which the caller reads as "do not filter".
        """
        if not self.syntax_cmd:
            return None
        argv = [a for a in self.syntax_cmd["argv"] if a != "-fsyntax-only"]
        argv.insert(1, "-E")
        r = self._run(argv, cwd=self.syntax_cmd["cwd"], timeout=timeout, env=self.env)
        if r is None or r.returncode != 0:
            return None
        wanted = os.path.realpath(self.target)
        lines, current, lineno = set(), None, 0
        for out in r.stdout.splitlines():
            marker = LINEMARKER.match(out)
            if marker:
                lineno = int(marker.group(1))
                # Resolved against the directory the compile runs in, not this
                # process's. The paths are the ones the -I flags produced, so
                # they are relative to the build directory; resolving them here
                # would name a file in whatever tree this script was started
                # from rather than the one in the lane.
                current = os.path.realpath(
                    os.path.join(self.syntax_cmd["cwd"], marker.group(2)))
                continue
            if current == wanted:
                lines.add(lineno)
            lineno += 1
        return lines or None

    def run_build(self, timeout, jobs=None):
        # jobs is overridden for the baseline, which is the one build that has
        # the machine to itself - and the cap follows it, so that build is not
        # measured against a share of the machine it is not sharing.
        jobs = jobs or self.jobs
        return self._run(["ninja", "-C", self.build, "-j", str(jobs)],
                         timeout=timeout, env=self.env,
                         memory=build_memory_limit(self.memory, jobs))

    def run_tests(self, timeout):
        cmd = [os.path.join(self.build, self.project.test_binary)] + self.test_args
        return self._run(cmd, cwd=self.project.test_cwd(self), timeout=timeout,
                         env=self.env)

    def _run(self, cmd, timeout, cwd=None, env=None, memory=None):
        # errors="replace" because a mutant's whole job is to make the library
        # misbehave, and a mutated size, width or divisor prints whatever bytes
        # it likes. Strict decoding would turn one such byte into a traceback
        # that kills the pool and loses the whole sweep.
        capped = memory_capped(cmd, self.memory if memory is None else memory)
        try:
            return subprocess.run(capped, cwd=cwd or self.dir, capture_output=True,
                                  text=True, errors="replace", timeout=timeout,
                                  env=env)
        except subprocess.TimeoutExpired:
            return None
        except FileNotFoundError as e:
            raise RuntimeError("missing tool: %s" % e)


def evaluate(lane, mutant, args, original):
    """Run one mutant. Returns (verdict, tests that caught it).

    The order the checks come in is the order they get cheaper to be wrong
    about: a mutant killed at the memory cap is asked about before the exit
    status is read, because a stopped build looks like a build that failed and
    "the compiler refused it" is the one thing it definitely does not mean.
    """
    lane.write_target(mutant_text(mutant, original))
    if args.quick_reject and lane.syntax_cmd:
        proc = lane.run_syntax_check(args.build_timeout)
        if killed_for_memory(proc):
            return "oom", []
        if proc is None or proc.returncode != 0:
            return "compiler", []
    proc = lane.run_build(args.build_timeout)
    if killed_for_memory(proc):
        return "oom", []
    if proc is None or proc.returncode != 0:
        return "compiler", []
    proc = lane.run_tests(args.test_timeout)
    if proc is None:
        return "hang", []
    if killed_for_memory(proc):
        return "oom", []
    failing = parse_test_output(proc)
    return ("caught", failing) if failing else ("survived", [])


# ------------------------------------------------------------- mutants

def parse_bug_file(path):
    """Bugs to reintroduce, one block each:

        # name of the bug
        <<<
        the code as it is today
        ===
        the code with the bug back
        >>>

    A block format rather than a diff so that multi-line bodies need no escaping
    and no leading-character rules - these are C++ fragments, and `-` is an
    operator.

    The name is the *first* line of a run of comments, so the ones after it are
    room to explain the bug without any of it ending up in the report. A blank
    line starts a new run, which is what lets a file open with a header of its
    own.

    The opening fence takes one flag, `<<< additive`, for the block whose new
    text is *meant* to contain its old text - an inserted call, an early return
    in front of code that stays. Without it such a block is refused; see
    bug_mutants for why that is the right default and what it cost before it
    existed. An unknown flag is an error rather than a shrug, because a
    misspelled `additive` would otherwise silently mean the opposite.
    """
    bugs, name, state, old, new, additive = [], None, None, [], [], False
    with open(path, encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            stripped = line.strip()
            if state is None:
                if not stripped:
                    name = None
                elif stripped.startswith("#"):
                    if name is None:
                        name = stripped.lstrip("#").strip()
                elif stripped.split()[:1] == ["<<<"]:
                    flags = stripped.split()[1:]
                    unknown = [f for f in flags if f != "additive"]
                    if unknown:
                        raise RuntimeError(
                            "%s:%d: unknown flag%s on the fence: %s. The only one "
                            "is 'additive'." % (path, lineno,
                                                "" if len(unknown) == 1 else "s",
                                                ", ".join(unknown)))
                    state, old, new = "old", [], []
                    additive = "additive" in flags
                else:
                    raise RuntimeError("%s:%d: expected '#' or '<<<'" % (path, lineno))
            elif stripped == "===" and state == "old":
                state = "new"
            elif stripped == ">>>" and state == "new":
                bugs.append(dict(name=name or "bug %d" % (len(bugs) + 1),
                                 old="\n".join(old), new="\n".join(new),
                                 additive=additive))
                name, state, additive = None, None, False
            elif state == "old":
                old.append(line)
            else:
                new.append(line)
    if state is not None:
        raise RuntimeError("%s: unterminated block, missing '>>>'" % path)
    return bugs


def bug_mutants(bugs, original):
    """Substitutions to mutated sources, refusing any that does not apply.

    A typo in the `old` text would otherwise substitute nothing, the suite would
    stay green, and the report would say the bug survived - a false alarm about
    the tests when the fault is in the bug list.
    """
    problems = []
    for bug in bugs:
        count = original.count(bug["old"])
        if count != 1:
            problems.append("  %-40s matches %d times, needs exactly 1"
                            % (bug["name"], count))
        elif bug["old"] == bug["new"]:
            problems.append("  %-40s is a no-op" % bug["name"])
        elif bug["old"] in bug["new"] and not bug.get("additive"):
            # The replacement still contains everything it replaced, so the code
            # the test is supposed to miss is still there and the run cannot mean
            # anything. Overwhelmingly a mistake when the intent was to *move*
            # something -- write the whole span in `old`, including what follows
            # it, or the original stays put underneath the edit. This is
            # woswoar's check, and it is here because it shipped three times in
            # one session over there before it existed: each cost a full run and
            # read as `caught` while nothing had changed.
            #
            # The exception is real and this repository has one: a bug that puts
            # an accepted `-` sign back in front of a digit check that stays.
            # That block says `<<< additive` and means it.
            problems.append(
                "  %-40s survives inside its own replacement, so the code does "
                "not change. If it is meant to insert in front of code that "
                "stays, write the fence as '<<< additive'." % bug["name"])
    if problems:
        raise RuntimeError("these bugs do not apply:\n" + "\n".join(problems))
    return [dict(name=bug["name"], text=original.replace(bug["old"], bug["new"]))
            for bug in bugs]


def site_mutants(sites):
    """Sweep mutants, as the edit rather than the edited file.

    A whole mutated copy of the file per site is ~130 KB for a big header, and a sweep of all three
    operators is ~1600 of them -- 200 MB of strings built before --limit or the uncompiled-line
    filter have had a chance to throw most of them away, and held live alongside a build subprocess
    per lane. The splice is a few microseconds at the point the lane writes it out."""
    return [dict(name=site["description"], line=site["line"],
                 offset=site["offset"], description=site["description"],
                 original=site["original"], replacement=site["replacement"])
            for site in sites]


def mutant_text(mutant, original):
    """The mutated file. A named bug carries its own, having been built by substitution over the
    whole text; a sweep mutant is one splice away from it."""
    if "text" in mutant:
        return mutant["text"]
    return (original[:mutant["offset"]] + mutant["replacement"]
            + original[mutant["offset"] + len(mutant["original"]):])


def reverse_commit_mutant(project, ref, original, target):
    """One bug, expressed as 'undo what this commit did to the file'.

    Applied to a scratch copy rather than a lane, so that every mode has produced
    its mutants before any lane exists - otherwise --dry-run has nothing to show
    and the lane count cannot be sized from the number of bugs.
    """
    # --format= drops the commit header, which `git show` prints even when the
    # path it was given is untouched - leaving output that is not empty but
    # holds no patch, so the failure surfaces from `git apply` instead of here.
    diff = subprocess.run(["git", "show", "--format=", ref, "--", target],
                          cwd=project.repo, capture_output=True, text=True,
                          check=True).stdout
    if "diff --git" not in diff:
        raise RuntimeError("%s does not touch %s" % (ref, target))
    with tempfile.TemporaryDirectory(prefix="%s-reverse-" % project.slug) as scratch:
        staged = os.path.join(scratch, target)
        os.makedirs(os.path.dirname(staged), exist_ok=True)
        with open(staged, "w", encoding="utf-8") as f:
            f.write(original)
        r = subprocess.run(["git", "apply", "-R", "--unsafe-paths",
                            "--directory", ".", "-p1", "-"],
                           cwd=scratch, input=diff, capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError("could not reverse %s:\n%s" % (ref, r.stderr))
        with open(staged, encoding="utf-8") as f:
            text = f.read()
    if text == original:
        raise RuntimeError("reversing %s changes nothing" % ref)
    return dict(name="reverse of %s" % ref, text=text)


# ------------------------------------------------------------- the run

def plan(args, wanted):
    """Lanes, jobs each, and memory each - settled before anything is copied.

    Together these are most of what a verdict depends on, so they are decided in
    one place and stated in the fingerprint rather than emerging from whichever
    function needed them first.
    """
    args.lanes = max(1, min(args.lanes, wanted))
    # One job per lane once the lanes fill the machine, but all of it for a
    # single named bug - which is the difference between a --replace answering
    # in 20 seconds and in three minutes, given that every mutant rebuilds
    # everything that includes the file.
    if args.jobs is None:
        args.jobs = max(1, (os.cpu_count() or 4) // args.lanes)
    if args.memory_limit is None:
        args.memory_limit = default_memory_limit(args.lanes, args.jobs)
    args.memory_note = memory_cap_reason(args.memory_limit) if args.memory_limit else ""
    if args.memory_note:
        args.memory_limit = 0


def fingerprint(project, args):
    """What this run could actually observe.

    A verdict is only meaningful for the build that produced it, and the way that
    goes wrong quietly is memory safety: an off-by-one in an index or a dangling
    reference is invisible to a plain build unless it happens to corrupt
    something a test then checks. Without a sanitizer those come back `survived`
    however good the tests are.
    """
    compiler = os.environ.get("CXX")
    if compiler is None:
        try:
            compiler = subprocess.run(["c++", "--version"], capture_output=True,
                                      text=True).stdout.splitlines()[0]
        except (OSError, IndexError):
            compiler = "unknown"
    setup_args = project.backend.setup_args(args)
    tests = " ".join(filter(None, [args.test_suite and "-ts=" + args.test_suite,
                                   args.exclude_suite and "-tse=" + args.exclude_suite,
                                   args.test_filter and "-tc=" + args.test_filter,
                                   args.exclude_filter and "-tce=" + args.exclude_filter]))
    facts = dict(compiler=compiler, sanitizer=project.sanitizer(setup_args),
                 cores=os.cpu_count(), backend=project.backend.name,
                 setup=setup_args, tests=tests or "all", file=args.file,
                 ccache=shutil.which("ccache") is not None,
                 lanes=args.lanes, jobs=args.jobs, memory=args.memory_limit,
                 memory_note=args.memory_note)
    facts.update(project.extra_facts(args))
    return facts


def render_fingerprint(project, facts):
    lines = ["compiler        %s" % facts["compiler"],
             "cores           %s" % (facts["cores"] or "?"),
             "lanes           %d, %d job%s each"
             % (facts["lanes"], facts["jobs"], "" if facts["jobs"] == 1 else "s"),
             "memory          %s per lane" % human(facts["memory"]),
             "%-15s %s" % (facts["backend"] + " setup", " ".join(facts["setup"])),
             "sanitizer       %s" % facts["sanitizer"],
             "mutating        %s" % facts["file"],
             "tests           %s" % facts["tests"]]
    rows, notes = project.extra_fingerprint(facts)
    notes = list(notes)  # appended to below; a project's own list is not ours to grow
    lines += rows
    if not facts["memory"]:
        notes.append(
            "NOTE: nothing caps what a mutant may allocate in this run%s.\n"
            "      A mutated growth policy can ask for more memory than\n"
            "      the machine has, and the kernel then picks the victim -\n"
            "      as likely another lane as the mutant that caused it, so\n"
            "      the verdicts around it are no longer trustworthy."
            % (": " + facts["memory_note"] if facts["memory_note"] else ""))
    if facts["sanitizer"] == "none":
        notes.append(
            "NOTE: no sanitizer in this build, so a mutant that reads one\n"
            "      slot too far or keeps a reference past the end is only\n"
            "      seen if it happens to corrupt something a test checks.\n"
            "      Re-run a survivor you cannot explain with a sanitizer\n"
            "      build before concluding the hole is in the tests.")
    if not facts["ccache"]:
        notes.append(
            "NOTE: no ccache on PATH. Only the baseline build would have\n"
            "      hit it - every mutant is a compile nothing has seen -\n"
            "      so this costs one full build, not a slower run.")
    for note in notes:
        lines += ["", note]
    return "\n".join(lines)


def baseline(lane, args, log):
    """Refuse to score anything until the suite is green repeatedly.

    A flaky test scored as a kill inflates the number in the flattering
    direction, which is the worst way for this tool to be wrong.

    Returns how long a green run takes, which is what the per-mutant timeouts
    are derived from - a fixed generous timeout makes every hung mutant cost
    many times what a real one does, and hangs are an expected verdict here.
    """
    log("baseline: building")
    proc = lane.run_build(args.build_timeout, jobs=os.cpu_count() or 4)
    if killed_for_memory(proc):
        raise RuntimeError(
            "the baseline build was killed at the memory cap, and that is this "
            "tree's own build rather than any mutant - raise --memory-limit, or "
            "pass 0 to turn the cap off.")
    if proc is None or proc.returncode != 0:
        raise RuntimeError("baseline build failed - fix the tree first")
    slowest = 0.0
    for attempt in range(args.baseline_runs):
        started = time.time()
        proc = lane.run_tests(args.test_timeout)
        slowest = max(slowest, time.time() - started)
        if proc is None:
            raise RuntimeError("baseline run %d timed out" % (attempt + 1))
        # The unmutated suite peaks at a few tens of megabytes, so this says the
        # cap is set below what an honest run needs - and every mutant under it
        # would come back `oom` no matter what the tests do.
        if killed_for_memory(proc):
            raise RuntimeError(
                "baseline run %d was killed at the memory cap (%s). That is the "
                "suite the mutants are measured against, so the cap is too low "
                "to tell anything apart - raise --memory-limit."
                % (attempt + 1, human(lane.memory)))
        failing, ran = parse_test_output(proc), count_test_cases(proc.stdout)
        if failing:
            raise RuntimeError(
                "baseline run %d failed (%s). The suite must be green and "
                "deterministic before mutants mean anything."
                % (attempt + 1, ", ".join(short_test_name(t) for t in failing)))
        # A filter that matches nothing is green, and every mutant under it
        # survives. Refusing here is the difference between "your filter names
        # no doctest suite" and a report claiming the suite covers nothing.
        if ran == 0:
            raise RuntimeError(
                "baseline run %d passed 0 test cases (%s). doctest's suites are "
                "the ones TEST_SUITE names, not the build system's."
                % (attempt + 1, " ".join(lane.test_args) or "no filter"))
        log("baseline: run %d/%d green, %s case%s"
            % (attempt + 1, args.baseline_runs, "?" if ran is None else ran,
               "" if ran == 1 else "s"))
    return slowest


def check_room_for(project, workdir, new_lanes, log):
    """Refuse before copying rather than half way through it.

    A lane is a copy of the tree and a build directory of its own, and the count
    comes off the core count without anyone having been asked. Running out part
    way leaves the configure step failing for a reason that does not mention
    disk - and where the workdir is a tmpfs, which is where /tmp usually is,
    running out means the machine running out of memory rather than of disk.
    """
    needed = new_lanes * project.lane_bytes
    if not needed:
        return
    free = shutil.disk_usage(workdir).free
    in_ram = memory_backed(workdir)
    log("%d new lane%s of about %s in %s, %s free%s"
        % (new_lanes, "" if new_lanes == 1 else "s", human(needed), workdir,
           human(free), " - and it is a tmpfs, so that is memory" if in_ram else ""))
    if needed > free:
        raise RuntimeError(
            "%s has %s free and %d lane%s want about %s%s. Use fewer --lanes, or "
            "--workdir somewhere with room."
            % (workdir, human(free), new_lanes, "" if new_lanes == 1 else "s",
               human(needed),
               " - and it is a tmpfs, so that room is memory" if in_ram else ""))


@contextlib.contextmanager
def lanes_for(project, args, wanted, log):
    """Copied trees, configured and baselined, cleaned up whatever happens."""
    if args.reuse:
        workdir = args.workdir or os.path.join(tempfile.gettempdir(),
                                               "%s-mutate-reuse" % project.slug)
        os.makedirs(workdir, exist_ok=True)
    else:
        workdir = args.workdir or tempfile.mkdtemp(prefix="%s-mutate-" % project.slug)
        if args.workdir:
            shutil.rmtree(workdir, ignore_errors=True)
            os.makedirs(workdir)
    try:
        count = args.lanes  # settled by plan(), along with jobs and memory
        lanes = [Lane(workdir, i, args, project) for i in range(count)]
        existing = sum(1 for lane in lanes if os.path.isdir(lane.build))
        log("preparing %d lane%s%s, %d job%s each"
            % (count, "" if count == 1 else "s",
               " (%d reused)" % existing if existing else "",
               args.jobs, "" if args.jobs == 1 else "s"))
        check_room_for(project, workdir, count - existing, log)
        started = time.time()
        with concurrent.futures.ThreadPoolExecutor(count) as pool:
            list(pool.map(lambda lane: lane.setup(), lanes))
        log("lanes ready in %.1fs" % (time.time() - started))
        started = time.time()
        green_seconds = baseline(lanes[0], args, log)
        log("baseline in %.1fs" % (time.time() - started))
        if args.test_timeout is None:
            args.test_timeout = max(20, int(6 * green_seconds))
            log("test timeout %ds, from a %.1fs green run"
                % (args.test_timeout, green_seconds))
        yield lanes
    finally:
        if not args.reuse:
            shutil.rmtree(workdir, ignore_errors=True)


def drop_uncompiled(lane, mutants, args, log):
    """Mutants in code this configuration does not compile, taken back out.

    Said out loud rather than done quietly: a run that silently swept less than
    it was asked to reads as "everything here is covered", and which lines were
    dropped is the interesting half - it names the branches this build cannot
    answer for, which is a coverage question rather than a test one.
    """
    compiled = lane.compiled_lines(args.build_timeout)
    if compiled is None:
        return mutants
    kept = [m for m in mutants if "line" not in m or m["line"] in compiled]
    dropped = len(mutants) - len(kept)
    if dropped:
        where = sorted({m["line"] for m in mutants if "line" in m and m["line"] not in compiled})
        log("%d mutant%s dropped on %d line%s the preprocessor removes in this "
            "build (%s%s) - nothing could catch them"
            % (dropped, "" if dropped == 1 else "s", len(where),
               "" if len(where) == 1 else "s",
               ", ".join(str(l) for l in where[:8]),
               ", ..." if len(where) > 8 else ""))
    return kept


def run_mutants(lanes, mutants, args, log, original):
    """Every mutant through a lane, reported in the order they were produced."""
    pending, results, lock = queue.Queue(), [], threading.Lock()
    for index, mutant in enumerate(mutants):
        pending.put((index, mutant))

    def worker(lane):
        while True:
            try:
                index, mutant = pending.get_nowait()
            except queue.Empty:
                return
            # One mutant that blows up must not take the sweep with it: at
            # several seconds each these runs are an hour long, and losing the
            # other verdicts to salvage nothing is the worst possible trade. An
            # 'error' of its own rather than a silent 'survived' - a swallowed
            # exception would flatter the tests, which is the one direction this
            # tool must never be wrong in.
            try:
                verdict, caught_by = evaluate(lane, mutant, args, original)
            except Exception as e:  # noqa: BLE001 - deliberately total
                verdict, caught_by = "error", ["%s: %s" % (type(e).__name__, e)]
            with lock:
                results.append(dict(mutant, index=index, verdict=verdict,
                                    caught_by=caught_by))
                log("[%d/%d] %-46s %s%s"
                    % (len(results), len(mutants), mutant["name"][:46], verdict,
                       " (%s)" % render_caught(caught_by) if caught_by else ""))

    with concurrent.futures.ThreadPoolExecutor(len(lanes)) as pool:
        list(pool.map(worker, lanes))

    results.sort(key=lambda r: r["index"])
    for r in results:
        # None of these belong in --json: a whole mutated header, or the splice that
        # would rebuild one.
        for key in ("text", "index", "offset", "original", "replacement"):
            r.pop(key, None)
    return results


def report(results, args, original):
    """One summary for both modes. Only the framing differs, not the verdicts."""
    counts = {v: sum(1 for r in results if r["verdict"] == v) for v in VERDICTS}
    survivors = [r for r in results if r["verdict"] == "survived"]

    print("\n" + "=" * 72)
    width = max(len(r["name"]) for r in results)
    if len(results) <= 40:
        for r in results:
            print("%-*s  %-9s  %s"
                  % (width, r["name"], r["verdict"], render_caught(r["caught_by"]) or "-"))
        print()

    tally = ["%d caught by a test" % counts["caught"],
             "%d by the compiler only" % counts["compiler"],
             "%d hung" % counts["hang"]]
    # Only when it happened: on a file where nothing controls an allocation
    # size this is always zero, and a permanent 0 reads as noise.
    if counts["oom"]:
        tally.append("%d killed at the memory cap" % counts["oom"])
    tally.append("%d SURVIVED" % counts["survived"])
    print(", ".join(tally))
    # Errors are not scored either way. Counting them as killed would inflate
    # the number, and as survived would invent holes that may not be there.
    scored = len(results) - counts["error"]
    if counts["error"]:
        print("%d could not be scored - the tool failed, not the tests:"
              % counts["error"])
        for r in results:
            if r["verdict"] == "error":
                print("  %-46s %s" % (r["name"][:46], ", ".join(r["caught_by"])))
    if scored > 1:
        print("score            %.0f%% killed, %.0f%% by a test"
              % (100.0 * (scored - counts["survived"]) / scored,
                 100.0 * counts["caught"] / scored))

    if survivors:
        print("\nnothing noticed these - whatever covers them is decoration:")
        source = original.splitlines()
        for r in survivors:
            if "line" in r:
                text = source[r["line"] - 1].strip() if r["line"] <= len(source) else ""
                print("  %s:%d  %s\n      %s" % (args.file, r["line"], r["name"],
                                                 text[:100]))
            else:
                print("  %s" % r["name"])
    return counts


def build_parser(project, doc):
    # The shared manual, then the project's own section. Composed rather than
    # each adapter carrying an excerpt: the excerpts were 37 identical lines and
    # both left out the verdicts and the memory cap, which is what a reader of
    # --help most needs and what only this file had.
    p = argparse.ArgumentParser(
        description=MANUAL + "\n" + (doc or ""),
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--replace", nargs=2, metavar=("OLD", "NEW"), action="append",
                   help="put a specific bug back: substitute OLD with NEW (must "
                        "match exactly once) and report which tests notice. "
                        "Repeatable - a batch runs across lanes in parallel")
    p.add_argument("--bugs", metavar="FILE",
                   help="a file of bugs to reintroduce, '# name' then a "
                        "<<< old === new >>> block each. Runs in parallel")
    p.add_argument("--additive", action="store_true",
                   help="allow a --replace whose NEW contains OLD. Refused "
                        "without this, because a replacement that keeps what it "
                        "replaced changes nothing and the verdict would be "
                        "about nothing. In a bug file the fence says it instead: "
                        "'<<< additive'")
    p.add_argument("--reverse", metavar="REF",
                   help="put one bug back by reverse-applying REF's changes to "
                        "the file, keeping today's tests")
    p.add_argument("--diff", metavar="REF", nargs="?", const="HEAD",
                   help="only mutate lines changed since REF, measured from the "
                        "merge base (the fast sweep). With no REF, whatever is "
                        "uncommitted. Adds to --bugs or --replace rather than "
                        "replacing them, so one run can ask both questions")
    p.add_argument("--lines", metavar="LINES", type=parse_lines,
                   help="only mutate these lines: 1290, 1278-1290, or a "
                        "comma-separated mix. Adds a sweep to whatever bugs "
                        "were named, the same way --diff does")
    p.add_argument("--file", metavar="PATH", default=project.target,
                   help="repo-relative file to mutate (default %(default)s)")
    p.add_argument("--lanes", type=int, default=os.cpu_count() or 4,
                   help="parallel build+test lanes (default: one per hardware "
                        "thread, %d here)" % (os.cpu_count() or 4))
    p.add_argument("--jobs", type=int, default=None,
                   help="build parallelism inside one lane (default: the cores "
                        "divided by the lanes, so one each when the lanes fill "
                        "the machine and all of them for a single bug)")
    p.add_argument("--buildtype", default=project.buildtype,
                   help="what the lanes are built as (default %(default)s)")
    p.add_argument("--memory-limit", type=parse_size, default=None, metavar="SIZE",
                   help="what one lane may allocate, as 4G, 512M or 0 for no "
                        "cap. The default is the smaller of a lane's share of "
                        "the machine and a gigabyte per job; a mutated growth "
                        "policy is what this exists for")
    p.add_argument("--operators", type=parse_operators, default=set(OPERATORS),
                   metavar="NAMES",
                   help="which mutations to make, comma-separated: %s. The "
                        "default is all of them" % ", ".join(OPERATORS))
    p.add_argument("--limit", type=int, help="stop after N mutants")
    p.add_argument("--shuffle-seed", type=int, default=0,
                   help="sample mutants deterministically when using --limit")
    p.add_argument("--build-timeout", type=int, default=900)
    p.add_argument("--test-timeout", type=int, default=None,
                   help="seconds before a mutant counts as hung (default: six "
                        "times the measured green run)")
    p.add_argument("--baseline-runs", type=int, default=2)
    p.add_argument("--no-quick-reject", dest="quick_reject",
                   action="store_false", default=True,
                   help="skip the -fsyntax-only pre-filter")
    p.add_argument(project.backend.arg_flag, metavar="ARG", action="append",
                   default=[], dest="configure_arg",
                   help="extra %s argument for every lane, repeatable. This is "
                        "how you ask a different question - whether a sanitizer "
                        "leg catches the bug, which the default build cannot "
                        "answer. Needs the '=' form when the value starts with "
                        "a dash" % project.backend.name)
    p.add_argument("--test-suite", metavar="NAME",
                   help="run only this doctest suite (-ts=)")
    p.add_argument("--exclude-suite", metavar="NAME",
                   help="skip this doctest suite (-tse=)")
    p.add_argument("--test-filter", metavar="PATTERN",
                   help="run only matching doctest cases (-tc=)")
    p.add_argument("--exclude-filter", metavar="PATTERN",
                   help="skip matching doctest cases (-tce=). What to reach for "
                        "when one case is flaky on this machine: the baseline "
                        "refuses to score against a suite that is not green "
                        "twice running, and excluding the flake by name is "
                        "honest in a way that lowering --baseline-runs is not")
    p.add_argument("--syntax-tu", metavar="PATH", default=None,
                   help="the TU the pre-filter compiles (default: derived from "
                        "--file)")
    p.add_argument("--dry-run", action="store_true",
                   help="list the mutants and the likely runtime, then stop")
    p.add_argument("--workdir", default=None, help="where lanes are copied")
    p.add_argument("--reuse", action="store_true",
                   help="keep the lanes afterwards and reuse them next time, "
                        "syncing only what changed. Uses a fixed workdir unless "
                        "--workdir says otherwise, so two concurrent runs need "
                        "different ones")
    p.add_argument("--json", metavar="FILE", help="write the full result set")
    # Not a flag: whether merged translation units are safe is the project's
    # answer, not a per-run choice, and `--meson-arg=--unity=off` is already the
    # way to override it for one run. Carried on `args` so a backend reads one
    # namespace rather than two.
    p.set_defaults(unity=project.unity)
    return p


def collect_mutants(project, args, original):
    """Every mutant this invocation asks for, named bugs first.

    The two kinds compose, and asking for both in one run is worth doing: they
    answer different questions over the same code, and everything a second
    invocation would repeat - copying the lanes, building the baseline, running
    the suite green twice - is paid once instead. The named bugs come first in
    the report, which is the order they are read in.

    An empty list is the honest answer to "there was nothing to ask" - a --diff
    over lines that hold no code, say - and the caller reads it as one.
    """
    mutants = []
    if args.bugs:
        mutants = bug_mutants(parse_bug_file(args.bugs), original)
    elif args.replace:
        mutants = bug_mutants(
            [dict(name="%s -> %s" % (old.strip()[:34], new.strip()[:34]),
                  old=old, new=new, additive=args.additive)
             for old, new in args.replace], original)
    elif args.reverse:
        mutants = [reverse_commit_mutant(project, args.reverse, original, args.file)]

    # A sweep is asked for by --diff or --lines, and with nothing named at all it
    # is the whole file - which is what this does when given no arguments.
    if args.diff or args.lines or not mutants:
        line_filter = None
        if args.diff:
            line_filter = changed_lines(project.repo, args.diff, args.file)
            if line_filter:
                print("%d line%s of %s changed since %s"
                      % (len(line_filter), "" if len(line_filter) == 1 else "s",
                         args.file, args.diff))
        elif args.lines:
            line_filter = args.lines
        if args.diff and not line_filter:
            # Nothing to sweep. Only an error when it was the whole request:
            # alongside a bug file it is a note, not a reason to run nothing.
            print("no changed lines in %s since %s" % (args.file, args.diff))
            if not mutants:
                return []
        else:
            mask = code_mask(original)
            sites, equivalent = [], []
            if "tokens" in args.operators:
                sites += mutation_sites(original, mask, line_filter, skipped=equivalent)
                if equivalent:
                    print("%d template parameter default%s skipped -- the value of a SFINAE dummy "
                          "is never read, so flipping it cannot be caught"
                          % (len(equivalent), "" if len(equivalent) == 1 else "s"))
            if "bitwise" in args.operators:
                sites += bitwise_sites(original, mask, line_filter)
            if "deletions" in args.operators:
                sites += deletion_sites(original, mask, line_filter)
            sites.sort(key=lambda s: s["offset"])
            sweep = site_mutants(sites)
            if args.limit and len(sweep) > args.limit:
                random.Random(args.shuffle_seed).shuffle(sweep)
                sweep = sweep[:args.limit]
                sweep.sort(key=lambda m: m["offset"])
            mutants += sweep

    if not mutants:
        # Reached by a sweep whose lines hold no code: a comment reflow, a
        # version bump, a `#define`. Saying which is the difference between a
        # clear answer and someone re-running it to find out why.
        print("nothing to mutate in %s - the lines asked for are comments, "
              "preprocessor or whitespace" % args.file)
    return mutants


def main(project, doc):
    p = build_parser(project, doc)
    args = p.parse_args()

    # Before anything is copied or built. Each of these is silent at runtime and
    # surfaces as a lane that builds nothing or a pre-filter that rejects
    # everything, so they are worth one stat call each at the top of every run
    # rather than only in whichever repository remembered to lint for them.
    problems = project.problems()
    if problems:
        raise RuntimeError("%s does not describe this repository:\n%s"
                           % (type(project).__name__,
                              "\n".join("  " + problem for problem in problems)))

    modes = [bool(args.replace), bool(args.bugs), bool(args.reverse)]
    if sum(modes) > 1:
        p.error("--replace, --bugs and --reverse are three ways to say the same "
                "thing; pick one")

    target_path = os.path.join(project.repo, args.file)
    if not os.path.exists(target_path):
        p.error("no such file: %s" % args.file)
    with open(target_path, encoding="utf-8") as f:
        original = f.read()

    if args.syntax_tu is None:
        args.syntax_tu = project.default_syntax_tu(args.file)
        if args.syntax_tu is None:
            args.quick_reject = False
    elif not os.path.exists(os.path.join(project.repo, args.syntax_tu)):
        # Named by hand and wrong. Left to itself the pre-filter would compile a
        # borrowed chunk's flags against a path that does not exist, fail for
        # every mutant, and report the run as one long wall of `compiler`.
        p.error("no such file to pre-filter with: %s" % args.syntax_tu)
    if not args.quick_reject:
        args.syntax_tu = None

    mutants = collect_mutants(project, args, original)
    if not mutants:
        return 0

    plan(args, len(mutants))

    if args.dry_run:
        for mutant in mutants:
            print("  %s%s" % ("line %d: " % mutant["line"] if "line" in mutant
                              else "", mutant["name"]))
        print("\n%d mutant%s over %d lane%s, roughly %s, %s"
              % (len(mutants), "" if len(mutants) == 1 else "s", args.lanes,
                 "" if args.lanes == 1 else "s",
                 estimate(project, len(mutants), args.lanes, os.cpu_count() or 4),
                 "%s of memory each" % human(args.memory_limit)
                 if args.memory_limit else "no memory cap available here"))
        return 0

    facts = fingerprint(project, args)
    print(render_fingerprint(project, facts) + "\n")

    # Before any lane exists, because it is the lanes the cores would land in.
    drop_core_dumps()

    print_lock = threading.Lock()

    def log(message):
        with print_lock:
            print(message, flush=True)

    started = time.time()
    with lanes_for(project, args, len(mutants), log) as lanes:
        mutants = drop_uncompiled(lanes[0], mutants, args, log)
        if not mutants:
            # Everything asked for was in a branch this build does not compile.
            # Not an error: the answer is "there is nothing here to ask", and
            # the line above already said which lines those were.
            print("\nnothing left to run - every mutant was in code this build "
                  "does not compile")
            return 0
        log("%d mutant%s over %d lane%s"
            % (len(mutants), "" if len(mutants) == 1 else "s",
               len(lanes), "" if len(lanes) == 1 else "s"))
        results = run_mutants(lanes, mutants, args, log, original)

    counts = report(results, args, original)
    print("\n%d mutant%s in %.0fs"
          % (len(results), "" if len(results) == 1 else "s", time.time() - started))

    if args.json:
        with open(args.json, "w") as f:
            json.dump(dict(environment=facts, counts=counts, results=results),
                      f, indent=2)
        print("wrote %s" % args.json)

    return 1 if counts["survived"] else 0


def run(project, doc):
    """What a project's `mutate.py` calls. The exception handling is here rather
    than in each copy of it, because every RuntimeError raised in this file is
    the tool refusing to answer - a bug block that does not apply, a baseline
    that is not green, a filter that matches no test. The message is the whole
    point of those, and a traceback down through the thread pool buries it. Exit
    2 keeps them apart from the 1 that means survivors were found."""
    try:
        sys.exit(main(project, doc))
    except RuntimeError as e:
        print("\n%s" % e, file=sys.stderr)
        sys.exit(2)
    except KeyboardInterrupt:
        print("\ninterrupted", file=sys.stderr)
        sys.exit(130)
