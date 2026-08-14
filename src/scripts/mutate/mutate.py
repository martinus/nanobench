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

A bug file is a name and a block each. Old text must match exactly once, and a
block that does not apply stops the run - otherwise a typo substitutes nothing,
the suite stays green, and the report blames your tests for it:

    # IPC keyed off measured values rather than availability
    <<<
        if (hasIns && hasCyc) {
    ===
        if (rInsMedian > 0.0 && rCycMedian > 0.0) {
    >>>

**Or sweep for holes you have not thought of**, mutating one token at a time:

    mutate.py --diff HEAD~1                    # only what this change touched
    mutate.py --lines 2890-2960                # one function
    mutate.py                                  # the whole header
    mutate.py --diff HEAD~1 --dry-run          # how many, and how long

Nothing is scored until the suite is green repeatedly, because a flaky test
counted as a kill inflates the number in the flattering direction. The working
tree is never touched: every build happens in a throwaway copy, so a run that
dies half way cannot leave a mutated header behind, and two runs at once get
their own copies rather than deleting each other's.

Every mutant ends in one of four verdicts, and the difference matters.
`compiler` means the build refused it - real protection, but not your tests
doing the work. `caught` means an assertion failed, which is the number worth
moving. `hang` means it ran forever; this library grows iteration counts in a
loop, so a mutated bound does that rather than failing. `survived` means nothing
noticed, which is the answer you are looking for.
"""

import argparse
import bisect
import concurrent.futures
import contextlib
import json
import os
import queue
import random
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

# The library is one header, so that is the default target; --file overrides it.
HEADER = os.path.join("src", "include", "nanobench.h")

# The one TU that compiles the implementation block, used for the syntax
# pre-filter. Only meaningful when the mutated file is one it includes.
IMPL_TU = os.path.join("src", "test", "app", "nanobench.cpp")

# Only the *top level* `docs` is skipped - it is the gitignored sphinx preview.
# `src/docs` must come along: unit_templates compares the built-in templates
# against `src/docs/_generated`, and a lane without it fails the baseline for a
# reason that has nothing to do with any mutant.
LANE_IGNORE = shutil.ignore_patterns(".git", "__pycache__", "build*", "b", "bsan")

# Rough seconds per mutant, plus the fixed cost of copying and configuring a lane
# and taking a baseline. Calibrated on one machine, so --dry-run's estimate is an
# order of magnitude rather than a promise.
SECONDS_PER_MUTANT = 8.4
SECONDS_OF_SETUP = 14.0

VERDICTS = ("caught", "compiler", "hang", "survived")


def lane_ignore(directory, names):
    skip = list(LANE_IGNORE(directory, names))
    if os.path.abspath(directory) == REPO:
        skip.append("docs")
    return skip


def estimate(count, lanes):
    seconds = SECONDS_OF_SETUP + SECONDS_PER_MUTANT * count / max(1, lanes)
    return "%.0f min" % (seconds / 60) if seconds > 90 else "%.0f s" % seconds


# ---------------------------------------------------------------- lexing

def code_mask(src):
    """True for every byte that is real code.

    False inside comments, string/char/raw-string literals and preprocessor
    directives. Mutating those is the main way a token-level mutator wastes its
    budget: this header is roughly half prose, and `#define` lines hold the
    version macros that a lint job checks separately.
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
# a mutant that cannot build costs a rebuild to tell us nothing.
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

IDENT = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")
# integer literal, not part of an identifier or a float/exponent
NUMBER = re.compile(r"\b(\d+)([uUlL]*)\b")

WORD_MUTATIONS = {"true": ["false"], "false": ["true"]}


def is_comparison(src, offset, op):
    """Whether a bare `<`/`>` is a comparison rather than a template bracket.

    Nearly half of all sites are angle brackets - `std::conditional<`,
    `static_cast<Op*>`, `template <typename Op>` - and mutating one is a compile
    error every time. They are not free: each costs a syntax check, and they
    swamp the `compiler` bucket, which is the one the report explicitly calls
    not the number worth moving. The header is clang-formatted, so a comparison
    always has a space on both sides and a template bracket never does.
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


def mutation_sites(src, mask, line_filter=None):
    """Every single-token change worth trying, as {offset, original, ...} dicts."""
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

        m = IDENT.match(src, i)
        if m:
            for rep in WORD_MUTATIONS.get(m.group(0), ()):
                add(i, m.group(0), rep)
            i = m.end()
            continue

        m = NUMBER.match(src, i)
        if m:
            # A float like 1e-9 or 0.05 must not be picked apart into an int.
            before = src[i - 1] if i else ""
            after = src[m.end()] if m.end() < n else ""
            if before not in ".0123456789" and after not in ".eE0123456789":
                value, suffix = int(m.group(1)), m.group(2)
                for rep_val in (value + 1, value - 1):
                    if rep_val >= 0:
                        add(i, m.group(0), "%d%s" % (rep_val, suffix))
            i = m.end()
            continue

        for op, reps in OPERATOR_MUTATIONS:
            if src.startswith(op, i):
                if is_comparison(src, i, op):
                    for rep in reps:
                        add(i, op, rep)
                i += len(op)
                break
        else:
            i += 1

    return sites


def changed_lines(ref, path):
    """Line numbers of `path` touched since `ref`, for the fast daily mode."""
    out = subprocess.run(
        ["git", "diff", "--unified=0", ref, "--", path],
        cwd=REPO, capture_output=True, text=True, check=True).stdout
    lines = set()
    for hunk in re.finditer(r"^@@ -\S+ \+(\d+)(?:,(\d+))? @@", out, re.M):
        start = int(hunk.group(1))
        count = int(hunk.group(2) or 1)
        lines.update(range(start, start + count))
    return lines


# ---------------------------------------------------------------- running

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
    # signal. Naming it beats reporting "unknown" - under --cmake-arg the thing
    # that noticed is often not a test at all, and which runtime complained is
    # the whole answer.
    for stream in (proc.stderr, proc.stdout):
        for pattern in (r"^SUMMARY: (\w+Sanitizer): (.+)$", r"runtime error: (.+)$"):
            m = re.search(pattern, stream, re.M)
            if m:
                return [" ".join(m.groups())[:120]]
    return ["exit %d, no assertion failed" % proc.returncode]


class Lane:
    """One throwaway copy of the repo, configured once and reused per mutant."""

    def __init__(self, root, index, args):
        self.dir = os.path.join(root, "lane%d" % index)
        self.build = os.path.join(self.dir, "build")
        self.target = os.path.join(self.dir, args.file)
        self.jobs = args.jobs
        self.test_filter = args.test_filter
        self.cmake_args = list(args.cmake_arg)
        # Set unconditionally, exactly as the CI legs do: only a sanitizer
        # runtime reads these, so on an ordinary build they do nothing. Without
        # halt_on_error UBSan prints the error and the binary still exits 0 -
        # and a mutant that only UBSan can see is then reported as a survivor,
        # which is the one way this tool must never be wrong.
        self.env = dict(os.environ)
        self.env.setdefault("ASAN_OPTIONS", "detect_stack_use_after_return=1")
        self.env.setdefault(
            "UBSAN_OPTIONS",
            "print_stacktrace=1:halt_on_error=1:suppressions="
            + os.path.join(self.dir, "ubsan.supp"))

    def setup(self):
        shutil.copytree(REPO, self.dir, ignore=lane_ignore)
        r = subprocess.run(
            ["cmake", "-S", self.dir, "-B", self.build, "-GNinja",
             "-DCMAKE_BUILD_TYPE=Release"] + self.cmake_args,
            capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError("cmake failed in lane:\n" + r.stdout + r.stderr)

    def write_target(self, text):
        with open(self.target, "w", encoding="utf-8") as f:
            f.write(text)

    def syntax_ok(self, timeout):
        """Cheap reject. Most operator mutants are simply not valid C++, and this
        answers that in about half a second instead of a full rebuild."""
        r = self._run([
            "c++", "-fsyntax-only", "-std=c++11",
            "-I", os.path.join(self.dir, "src", "include"),
            "-isystem", os.path.join(self.dir, "src", "test"),
            os.path.join(self.dir, IMPL_TU)], timeout=timeout)
        return r is not None and r.returncode == 0

    def build_ok(self, timeout):
        r = self._run(["ninja", "-C", self.build, "-j", str(self.jobs)],
                      timeout=timeout)
        return r is not None and r.returncode == 0

    def failing_tests(self, timeout):
        """Tests that went red, or None if the run timed out."""
        cmd = [os.path.join(self.build, "nb")]
        if self.test_filter:
            cmd.append("-tc=" + self.test_filter)
        r = self._run(cmd, cwd=self.build, timeout=timeout, env=self.env)
        return None if r is None else parse_test_output(r)

    def _run(self, cmd, timeout, cwd=None, env=None):
        try:
            return subprocess.run(cmd, cwd=cwd or self.dir, capture_output=True,
                                  text=True, timeout=timeout, env=env)
        except subprocess.TimeoutExpired:
            return None
        except FileNotFoundError as e:
            raise RuntimeError("missing tool: %s" % e)


def evaluate(lane, mutant, args):
    """Run one mutant. Returns (verdict, tests that caught it)."""
    lane.write_target(mutant["text"])
    if args.quick_reject and not lane.syntax_ok(args.build_timeout):
        return "compiler", []
    if not lane.build_ok(args.build_timeout):
        return "compiler", []
    failing = lane.failing_tests(args.test_timeout)
    if failing is None:
        return "hang", []
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
    """
    bugs, name, state, old, new = [], None, None, [], []
    with open(path, encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            stripped = line.strip()
            if state is None:
                if stripped.startswith("#"):
                    name = stripped.lstrip("#").strip()
                elif stripped == "<<<":
                    state, old, new = "old", [], []
                elif stripped:
                    raise RuntimeError("%s:%d: expected '#' or '<<<'" % (path, lineno))
            elif stripped == "===" and state == "old":
                state = "new"
            elif stripped == ">>>" and state == "new":
                bugs.append(dict(name=name or "bug %d" % (len(bugs) + 1),
                                 old="\n".join(old), new="\n".join(new)))
                name, state = None, None
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
    if problems:
        raise RuntimeError("these bugs do not apply:\n" + "\n".join(problems))
    return [dict(name=bug["name"], text=original.replace(bug["old"], bug["new"]))
            for bug in bugs]


def site_mutants(sites, original):
    return [dict(name=site["description"], line=site["line"],
                 offset=site["offset"], description=site["description"],
                 text=(original[:site["offset"]] + site["replacement"]
                       + original[site["offset"] + len(site["original"]):]))
            for site in sites]


def reverse_commit_mutant(ref, original, target):
    """One bug, expressed as 'undo what this commit did to the file'.

    Applied to a scratch copy rather than a lane, so that every mode has produced
    its mutants before any lane exists - otherwise --dry-run has nothing to show
    and the lane count cannot be sized from the number of bugs.
    """
    # --format= drops the commit header, which `git show` prints even when the
    # path it was given is untouched - leaving output that is not empty but
    # holds no patch, so the failure surfaces from `git apply` instead of here.
    diff = subprocess.run(["git", "show", "--format=", ref, "--", target],
                          cwd=REPO, capture_output=True, text=True,
                          check=True).stdout
    if "diff --git" not in diff:
        raise RuntimeError("%s does not touch %s" % (ref, target))
    with tempfile.TemporaryDirectory(prefix="nanobench-reverse-") as scratch:
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

def fingerprint(args):
    """What this run could actually observe.

    A verdict is only meaningful for the machine that produced it, and the way
    that goes wrong is silent: where `perf_event_open` is refused - most VMs and
    containers - nanobench records no counters, so every bug in the counter
    columns comes back survived no matter how good the tests are. Someone
    reading that later would conclude the suite is worse than it is, which is
    the one direction this tool must not be wrong in.
    """
    counters = None
    paranoid = "/proc/sys/kernel/perf_event_paranoid"
    if os.path.exists(paranoid):
        try:
            with open(paranoid) as f:
                counters = int(f.read().strip()) <= 2
        except (OSError, ValueError):
            counters = None
    else:
        counters = False

    compiler = None
    for arg in args.cmake_arg:
        if arg.startswith("-DCMAKE_CXX_COMPILER="):
            compiler = arg.split("=", 1)[1]
    if compiler is None:
        try:
            compiler = subprocess.run(["c++", "--version"], capture_output=True,
                                      text=True).stdout.splitlines()[0]
        except (OSError, IndexError):
            compiler = "unknown"

    return dict(compiler=compiler, perf_counters=counters,
                cores=os.cpu_count(), cmake=list(args.cmake_arg),
                tests=args.test_filter or "all", file=args.file)


def render_fingerprint(facts):
    counters = {True: "yes", False: "no", None: "unknown"}[facts["perf_counters"]]
    lines = ["compiler        %s" % facts["compiler"],
             "perf counters   %s" % counters,
             "cores           %s" % (facts["cores"] or "?"),
             "cmake           %s" % (" ".join(facts["cmake"]) or "(defaults)"),
             "mutating        %s" % facts["file"],
             "tests           %s" % facts["tests"]]
    if facts["perf_counters"] is not True:
        lines += ["",
                  "NOTE: no performance counters here, so nanobench records none",
                  "      and every bug in the ins/cyc/IPC/bra/miss columns will",
                  "      look survived whatever the tests do. Those verdicts",
                  "      mean nothing on this machine."]
    return "\n".join(lines)


def baseline(lane, args, log):
    """Refuse to score anything until the suite is green repeatedly.

    A flaky test scored as a kill inflates the number in the flattering
    direction, which is the worst way for this tool to be wrong. Parts of this
    suite are timing dependent, so this is not hypothetical.

    Returns how long a green run takes, which is what the per-mutant timeouts
    are derived from - a fixed generous timeout makes every hung mutant cost
    many times what a real one does, and hangs are an expected verdict here.
    """
    log("baseline: building")
    if not lane.build_ok(args.build_timeout):
        raise RuntimeError("baseline build failed - fix the tree first")
    slowest = 0.0
    for attempt in range(args.baseline_runs):
        started = time.time()
        failing = lane.failing_tests(args.test_timeout)
        slowest = max(slowest, time.time() - started)
        if failing is None:
            raise RuntimeError("baseline run %d timed out" % (attempt + 1))
        if failing:
            raise RuntimeError(
                "baseline run %d failed (%s). The suite must be green and "
                "deterministic before mutants mean anything."
                % (attempt + 1, ", ".join(failing)))
        log("baseline: run %d/%d green" % (attempt + 1, args.baseline_runs))
    return slowest


@contextlib.contextmanager
def lanes_for(args, wanted, log):
    """Copied trees, configured and baselined, cleaned up whatever happens."""
    workdir = args.workdir or tempfile.mkdtemp(prefix="nanobench-mutate-")
    if args.workdir:
        shutil.rmtree(workdir, ignore_errors=True)
        os.makedirs(workdir)
    try:
        count = max(1, min(args.lanes, wanted))
        lanes = [Lane(workdir, i, args) for i in range(count)]
        log("preparing %d lane%s" % (count, "" if count == 1 else "s"))
        with concurrent.futures.ThreadPoolExecutor(count) as pool:
            list(pool.map(lambda lane: lane.setup(), lanes))
        green_seconds = baseline(lanes[0], args, log)
        if args.test_timeout is None:
            args.test_timeout = max(20, int(6 * green_seconds))
            log("test timeout %ds, from a %.1fs green run"
                % (args.test_timeout, green_seconds))
        yield lanes
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


def run_mutants(lanes, mutants, args, log):
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
            verdict, caught_by = evaluate(lane, mutant, args)
            with lock:
                results.append(dict(mutant, index=index, verdict=verdict,
                                    caught_by=caught_by))
                log("[%d/%d] %-46s %s%s"
                    % (len(results), len(mutants), mutant["name"][:46], verdict,
                       " (%s)" % ", ".join(caught_by) if caught_by else ""))

    with concurrent.futures.ThreadPoolExecutor(len(lanes)) as pool:
        list(pool.map(worker, lanes))

    results.sort(key=lambda r: r["index"])
    for r in results:
        r.pop("text", None)  # a whole mutated header per result helps nobody
        r.pop("index", None)
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
                  % (width, r["name"], r["verdict"], ", ".join(r["caught_by"]) or "-"))
        print()

    print("%d caught by a test, %d by the compiler only, %d hung, %d SURVIVED"
          % (counts["caught"], counts["compiler"], counts["hang"],
             counts["survived"]))
    if len(results) > 1:
        print("score            %.0f%% killed, %.0f%% by a test"
              % (100.0 * (len(results) - counts["survived"]) / len(results),
                 100.0 * counts["caught"] / len(results)))

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


def main():
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--replace", nargs=2, metavar=("OLD", "NEW"), action="append",
                   help="put a specific bug back: substitute OLD with NEW (must "
                        "match exactly once) and report which tests notice. "
                        "Repeatable - a batch runs across lanes in parallel")
    p.add_argument("--bugs", metavar="FILE",
                   help="a file of bugs to reintroduce, '# name' then a "
                        "<<< old === new >>> block each. Runs in parallel")
    p.add_argument("--reverse", metavar="REF",
                   help="put one bug back by reverse-applying REF's changes to "
                        "the file, keeping today's tests")
    p.add_argument("--diff", metavar="REF",
                   help="only mutate lines changed since REF (the fast sweep)")
    p.add_argument("--lines", metavar="A-B", help="only mutate lines A..B")
    p.add_argument("--file", metavar="PATH", default=HEADER,
                   help="repo-relative file to mutate (default the header)")
    p.add_argument("--lanes", type=int, default=4,
                   help="parallel build+test lanes (default 4; past about four "
                        "they only re-divide the same cores)")
    p.add_argument("--jobs", type=int, default=8,
                   help="ninja parallelism inside one lane (default 8)")
    p.add_argument("--limit", type=int, help="stop after N mutants")
    p.add_argument("--shuffle-seed", type=int, default=0,
                   help="sample mutants deterministically when using --limit")
    p.add_argument("--build-timeout", type=int, default=300)
    p.add_argument("--test-timeout", type=int, default=None,
                   help="seconds before a mutant counts as hung (default: six "
                        "times the measured green run)")
    p.add_argument("--baseline-runs", type=int, default=2)
    p.add_argument("--no-quick-reject", dest="quick_reject",
                   action="store_false", default=True,
                   help="skip the -fsyntax-only pre-filter")
    p.add_argument("--cmake-arg", metavar="ARG", action="append", default=[],
                   help="extra cmake argument for every lane, repeatable. This "
                        "is how you ask a different question: whether a leg "
                        "catches the bug, which the default Release build "
                        "cannot answer. Needs the '=' form, because the value "
                        "starts with a dash: "
                        "--cmake-arg=-DNB_sanitizer=ON "
                        "--cmake-arg=-DCMAKE_CXX_COMPILER=clang++")
    p.add_argument("--test-filter", metavar="PATTERN",
                   help="run only matching doctest cases. 'unit_*' roughly "
                        "halves a run, because the example and tutorial cases "
                        "are benchmarks that assert nothing - but they can "
                        "still die by crashing, so a kill only they would have "
                        "seen is reported as a survivor")
    p.add_argument("--dry-run", action="store_true",
                   help="list the mutants and the likely runtime, then stop")
    p.add_argument("--workdir", default=None, help="where lanes are copied")
    p.add_argument("--json", metavar="FILE", help="write the full result set")
    args = p.parse_args()

    modes = [bool(args.replace), bool(args.bugs), bool(args.reverse)]
    if sum(modes) > 1:
        p.error("--replace, --bugs and --reverse are three ways to say the same "
                "thing; pick one")

    target_path = os.path.join(REPO, args.file)
    if not os.path.exists(target_path):
        p.error("no such file: %s" % args.file)
    with open(target_path, encoding="utf-8") as f:
        original = f.read()

    # The syntax pre-filter compiles one fixed TU. Against any other file it
    # would compile something the mutation cannot reach, pass every time, and
    # quietly stop filtering.
    if args.file != HEADER and args.quick_reject:
        args.quick_reject = False

    if any(modes):
        if args.bugs:
            mutants = bug_mutants(parse_bug_file(args.bugs), original)
        elif args.replace:
            mutants = bug_mutants(
                [dict(name="%s -> %s" % (old.strip()[:34], new.strip()[:34]),
                      old=old, new=new) for old, new in args.replace], original)
        else:
            mutants = [reverse_commit_mutant(args.reverse, original, args.file)]
    else:
        line_filter = None
        if args.diff:
            line_filter = changed_lines(args.diff, args.file)
            if not line_filter:
                print("no changed lines in %s since %s" % (args.file, args.diff))
                return 0
        elif args.lines:
            a, _, b = args.lines.partition("-")
            line_filter = set(range(int(a), int(b or a) + 1))
        mutants = site_mutants(
            mutation_sites(original, code_mask(original), line_filter), original)
        if args.limit and len(mutants) > args.limit:
            random.Random(args.shuffle_seed).shuffle(mutants)
            mutants = mutants[:args.limit]
            mutants.sort(key=lambda m: m["offset"])

    if not mutants:
        print("nothing to mutate")
        return 0

    if args.dry_run:
        for mutant in mutants:
            print("  %s%s" % ("line %d: " % mutant["line"] if "line" in mutant
                              else "", mutant["name"]))
        print("\n%d mutant%s over %d lanes, roughly %s"
              % (len(mutants), "" if len(mutants) == 1 else "s", args.lanes,
                 estimate(len(mutants), args.lanes)))
        return 0

    facts = fingerprint(args)
    print(render_fingerprint(facts) + "\n")

    print_lock = threading.Lock()

    def log(message):
        with print_lock:
            print(message, flush=True)

    started = time.time()
    with lanes_for(args, len(mutants), log) as lanes:
        log("%d mutant%s over %d lane%s"
            % (len(mutants), "" if len(mutants) == 1 else "s",
               len(lanes), "" if len(lanes) == 1 else "s"))
        results = run_mutants(lanes, mutants, args, log)

    counts = report(results, args, original)
    print("\n%d mutant%s in %.0fs"
          % (len(results), "" if len(results) == 1 else "s", time.time() - started))

    if args.json:
        with open(args.json, "w") as f:
            json.dump(dict(environment=facts, counts=counts, results=results),
                      f, indent=2)
        print("wrote %s" % args.json)

    return 1 if counts["survived"] else 0


if __name__ == "__main__":
    sys.exit(main())
