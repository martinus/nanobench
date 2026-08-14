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
    mutate.py                                  # the whole header, ~80 minutes
    mutate.py --diff HEAD~1 --dry-run          # how many, and how long

Nothing is scored until the suite is green repeatedly, because a flaky test
counted as a kill inflates the number in the flattering direction. The working
tree is never touched: every build happens in a throwaway copy, so a run that
dies half way cannot leave a mutated header behind, and two runs at once get
their own copies rather than deleting each other's.

How a mutant dies matters. `build` means the compiler refused it - real
protection, but not your tests doing the work. `test` means an assertion failed,
which is the number worth moving. `hang` means it ran forever; this library grows
iteration counts in a loop, so a mutated bound does that rather than failing.
"""

import argparse
import concurrent.futures
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

# The library is one header, so that is the default target. Set by --file, because
# a repo-relative path is the only thing tying this to nanobench.
HEADER = os.path.join("src", "include", "nanobench.h")

# Copied into each lane. Only the *top level* `docs` is skipped - it is the
# gitignored sphinx preview. `src/docs` must come along: unit_templates compares
# the built-in templates against `src/docs/_generated`, and a lane without it
# fails the baseline for a reason that has nothing to do with any mutant.
COPY_SKIP = {".git", "__pycache__"}


def lane_ignore(directory, names):
    skip = [n for n in names
            if n in COPY_SKIP or n.startswith("build") or n in ("b", "bsan")]
    if os.path.abspath(directory) == REPO:
        skip.append("docs")
    return skip


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
        for i in range(start, min(end, len(src))):
            mask[i] = 0

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
# a mutant that cannot build costs a full rebuild to tell us nothing.
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


def mutation_sites(src, mask, line_filter=None):
    """Every (offset, original, replacement, description) worth trying."""
    sites = []
    n = len(src)

    starts = line_starts(src)

    def line_of(off):
        lo, hi = 0, len(starts) - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if starts[mid] <= off:
                lo = mid
            else:
                hi = mid - 1
        return lo + 1

    def keep(off):
        return line_filter is None or line_of(off) in line_filter

    i = 0
    while i < n:
        if not mask[i]:
            i += 1
            continue

        m = IDENT.match(src, i)
        if m:
            word = m.group(0)
            if word in WORD_MUTATIONS and keep(i):
                for rep in WORD_MUTATIONS[word]:
                    sites.append((i, word, rep, "%s -> %s" % (word, rep)))
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
                    if rep_val < 0:
                        continue
                    if keep(i):
                        rep = "%d%s" % (rep_val, suffix)
                        sites.append((i, m.group(0), rep,
                                      "%s -> %s" % (m.group(0), rep)))
            i = m.end()
            continue

        for op, reps in OPERATOR_MUTATIONS:
            if src.startswith(op, i):
                if keep(i):
                    for rep in reps:
                        sites.append((i, op, rep, "%s -> %s" % (op, rep)))
                i += len(op)
                break
        else:
            i += 1

    return [dict(offset=o, original=orig, replacement=rep, description=desc,
                 line=line_of(o)) for (o, orig, rep, desc) in sites]


def line_starts(src):
    starts = [0]
    for i, c in enumerate(src):
        if c == "\n":
            starts.append(i + 1)
    return starts


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

class Lane:
    """One throwaway copy of the repo, configured once and reused per mutant."""

    def __init__(self, root, index, jobs, test_filter=None, cmake_args=None):
        self.dir = os.path.join(root, "lane%d" % index)
        self.build = os.path.join(self.dir, "build")
        self.header = os.path.join(self.dir, HEADER)
        self.jobs = jobs
        self.test_filter = test_filter
        self.cmake_args = list(cmake_args or [])
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

    def setup(self, log):
        log("lane %s: copying tree" % os.path.basename(self.dir))
        shutil.copytree(REPO, self.dir, ignore=lane_ignore)
        r = subprocess.run(
            ["cmake", "-S", self.dir, "-B", self.build, "-GNinja",
             "-DCMAKE_BUILD_TYPE=Release"] + self.cmake_args,
            capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError("cmake failed in lane:\n" + r.stdout + r.stderr)

    def write_header(self, text):
        with open(self.header, "w", encoding="utf-8") as f:
            f.write(text)

    def syntax_ok(self, timeout):
        """Cheap reject. Most operator mutants are simply not valid C++, and this
        answers that in about a second instead of a full rebuild."""
        r = self._run([
            "c++", "-fsyntax-only", "-std=c++11",
            "-I", os.path.join(self.dir, "src", "include"),
            "-isystem", os.path.join(self.dir, "src", "test"),
            os.path.join(self.dir, "src", "test", "app", "nanobench.cpp")],
            cwd=self.dir, timeout=timeout)
        return r is not None and r.returncode == 0

    def build_ok(self, timeout):
        r = self._run(["ninja", "-C", self.build, "-j", str(self.jobs)],
                      cwd=self.dir, timeout=timeout)
        return r is not None and r.returncode == 0

    def run_tests(self, timeout):
        """(passed, first failing test name or None). None passed == timeout."""
        failing = self.failing_tests(timeout)
        if failing is None:
            return None, None
        return (True, None) if not failing else (False, failing[0])

    def failing_tests(self, timeout):
        """Every test that went red, in order. None means the run timed out.

        doctest prints a `TEST CASE:` banner for anything that produces output,
        a passing MESSAGE included, so the banners alone are not failures - only
        one with an assertion error under it counts.
        """
        cmd = [os.path.join(self.build, "nb")]
        if self.test_filter:
            cmd.append("-tc=" + self.test_filter)
        r = self._run(cmd, cwd=self.build, timeout=timeout, env=self.env)
        if r is None:
            return None
        if r.returncode == 0:
            return []
        failing, current = [], None
        for line in r.stdout.splitlines():
            banner = re.match(r"^TEST CASE:\s*(.+)$", line)
            if banner:
                current = banner.group(1).strip()
            elif ("ERROR" in line or "FAILED" in line) and current:
                if current not in failing:
                    failing.append(current)
        if failing:
            return failing
        # Nonzero exit with no failed assertion: a sanitizer abort, a crash or a
        # signal. Naming it beats reporting "unknown" - under --cmake-arg the
        # thing that noticed is often not a test at all, and which runtime
        # complained is the whole answer.
        for stream in (r.stderr, r.stdout):
            for pattern in (r"^SUMMARY: (\w+Sanitizer): (.+)$",
                            r"runtime error: (.+)$"):
                m = re.search(pattern, stream, re.M)
                if m:
                    return [" ".join(m.groups())[:120]]
        return ["exit %d, no assertion failed" % r.returncode]

    @staticmethod
    def _run(cmd, cwd, timeout, env=None):
        try:
            return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                                  timeout=timeout, env=env)
        except subprocess.TimeoutExpired:
            return None
        except FileNotFoundError as e:
            raise RuntimeError("missing tool: %s" % e)


def evaluate(lane, original, site, args):
    """Run one mutant. Returns its verdict."""
    mutated = (original[:site["offset"]] + site["replacement"]
               + original[site["offset"] + len(site["original"]):])
    lane.write_header(mutated)

    if args.quick_reject and not lane.syntax_ok(args.build_timeout):
        return "build", None
    if not lane.build_ok(args.build_timeout):
        return "build", None
    passed, failing = lane.run_tests(args.test_timeout)
    if passed is None:
        return "timeout", None
    return ("survived", None) if passed else ("test", failing)


def fingerprint(args):
    """What this run could actually observe, printed with every report.

    A verdict is only meaningful for the machine that produced it, and the way
    that goes wrong is silent: where `perf_event_open` is refused - most VMs and
    containers - nanobench records no counters, so every bug in the counter
    columns comes back UNCAUGHT no matter how good the tests are. Someone
    reading that later would conclude the suite is worse than it is, which is
    the one direction this tool must not be wrong in.
    """
    counters = "no"
    paranoid = "/proc/sys/kernel/perf_event_paranoid"
    if os.path.exists(paranoid):
        try:
            with open(paranoid) as f:
                level = int(f.read().strip())
            counters = "yes (perf_event_paranoid=%d)" % level if level <= 2 \
                else "no (perf_event_paranoid=%d)" % level
        except (OSError, ValueError):
            counters = "unknown"

    compiler = "unknown"
    for arg in args.cmake_arg:
        if arg.startswith("-DCMAKE_CXX_COMPILER="):
            compiler = arg.split("=", 1)[1]
    if compiler == "unknown":
        try:
            compiler = subprocess.run(["c++", "--version"], capture_output=True,
                                      text=True).stdout.splitlines()[0]
        except (OSError, IndexError):
            pass

    lines = ["compiler        %s" % compiler,
             "perf counters   %s" % counters,
             "cores           %s" % (os.cpu_count() or "?"),
             "cmake           %s" % (" ".join(args.cmake_arg) or "(defaults)"),
             "tests           %s" % (args.test_filter or "all")]
    if counters.startswith("no"):
        lines.append("")
        lines.append("NOTE: no performance counters here, so nanobench records "
                     "none and every")
        lines.append("      bug in the ins/cyc/IPC/bra/miss columns will look "
                     "UNCAUGHT whatever")
        lines.append("      the tests do. Those verdicts mean nothing on this "
                     "machine.")
    return "\n".join(lines)


def baseline(lane, args, log):
    """Refuse to score anything until the suite is green repeatedly.

    A flaky test scored as a kill inflates the number in the flattering
    direction, which is the worst way for this tool to be wrong. Parts of this
    suite are timing dependent, so this is not hypothetical.
    """
    log("baseline: building")
    if not lane.build_ok(args.build_timeout):
        raise RuntimeError("baseline build failed - fix the tree first")
    for attempt in range(args.baseline_runs):
        passed, failing = lane.run_tests(args.test_timeout)
        if passed is None:
            raise RuntimeError("baseline run %d timed out" % (attempt + 1))
        if not passed:
            raise RuntimeError(
                "baseline run %d failed (%s). The suite must be green and "
                "deterministic before mutants mean anything." %
                (attempt + 1, failing))
        log("baseline: run %d/%d green" % (attempt + 1, args.baseline_runs))


def prepare_workdir(args):
    """A fresh directory per run, unique unless --workdir says otherwise.

    A fixed path would be tidier to look at and quietly wrong: two runs on one
    machine - a sweep in one terminal, a quick --replace in another - would
    delete each other's lanes mid-build, and the victim fails with a missing
    header rather than anything that points at the cause.
    """
    if args.workdir:
        shutil.rmtree(args.workdir, ignore_errors=True)
        os.makedirs(args.workdir)
        return args.workdir
    return tempfile.mkdtemp(prefix="nanobench-mutate-",
                            dir=os.environ.get("TMPDIR", "/tmp"))


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


def validate_bugs(bugs, original):
    """Fail before running anything if a bug does not apply.

    A typo in the `old` text would otherwise substitute nothing, the suite would
    stay green, and the report would say 'nothing caught it' - a false alarm
    about the tests when the fault is in the bug list.
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


def evaluate_bug(lane, original, bug, args):
    """(verdict, [tests that caught it])."""
    lane.write_header(original.replace(bug["old"], bug["new"]))
    if not lane.build_ok(args.build_timeout):
        return "compiler", []
    failing = lane.failing_tests(args.test_timeout)
    if failing is None:
        return "hang", []
    return ("caught", failing) if failing else ("UNCAUGHT", [])


def run_bugs(lanes, original, bugs, args, log):
    """Reintroduce every bug, spread across lanes.

    Doing this by hand is serial by construction - one header, one build tree -
    which is why a batch of ten was an afternoon. They are independent, so they
    parallelise exactly like mutants do.
    """
    validate_bugs(bugs, original)
    log("%d bug%s over %d lane%s"
        % (len(bugs), "" if len(bugs) == 1 else "s",
           len(lanes), "" if len(lanes) == 1 else "s"))

    pending, results, lock = queue.Queue(), [], threading.Lock()
    for bug in bugs:
        pending.put(bug)

    def worker(lane):
        while True:
            try:
                bug = pending.get_nowait()
            except queue.Empty:
                return
            verdict, tests = evaluate_bug(lane, original, bug, args)
            with lock:
                results.append(dict(bug, verdict=verdict, caught_by=tests))
                print("[%d/%d] %-44s %s" % (len(results), len(bugs),
                                            bug["name"][:44], verdict),
                      flush=True)

    with concurrent.futures.ThreadPoolExecutor(len(lanes)) as pool:
        list(pool.map(worker, lanes))

    order = {b["name"]: i for i, b in enumerate(bugs)}
    results.sort(key=lambda r: order[r["name"]])

    width = max(len(r["name"]) for r in results)
    print("\n" + "=" * 72)
    for r in results:
        tests = ", ".join(r["caught_by"]) if r["caught_by"] else "-"
        print("%-*s  %-9s  %s" % (width, r["name"], r["verdict"], tests))

    uncaught = [r for r in results if r["verdict"] == "UNCAUGHT"]
    compiler = [r for r in results if r["verdict"] == "compiler"]
    print("\n%d caught by a test, %d by the compiler only, %d UNCAUGHT"
          % (len(results) - len(uncaught) - len(compiler), len(compiler),
             len(uncaught)))
    if uncaught:
        print("\nnothing noticed these - whatever covers them is decoration:")
        for r in uncaught:
            print("  %s" % r["name"])
    return results


def reverse_commit_bug(lane, ref):
    """One bug, expressed as 'undo what this commit did to the header'."""
    diff = subprocess.run(["git", "show", ref, "--", HEADER],
                          cwd=REPO, capture_output=True, text=True,
                          check=True).stdout
    if not diff.strip():
        raise RuntimeError("%s does not touch %s" % (ref, HEADER))
    r = subprocess.run(["git", "apply", "-R", "-"], cwd=lane.dir,
                       input=diff, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("could not reverse %s onto the lane:\n%s"
                           % (ref, r.stderr))
    with open(lane.header, encoding="utf-8") as f:
        return f.read()


def main():
    global HEADER
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--diff", metavar="REF",
                   help="only mutate lines changed since REF (the fast mode)")
    p.add_argument("--lines", metavar="A-B", help="only mutate lines A..B")
    p.add_argument("--lanes", type=int, default=4,
                   help="parallel build+test lanes (default 4)")
    p.add_argument("--jobs", type=int, default=8,
                   help="ninja parallelism inside one lane (default 8)")
    p.add_argument("--limit", type=int, help="stop after N mutants")
    p.add_argument("--shuffle-seed", type=int, default=0,
                   help="sample mutants deterministically when using --limit")
    p.add_argument("--build-timeout", type=int, default=300)
    p.add_argument("--test-timeout", type=int, default=120)
    p.add_argument("--baseline-runs", type=int, default=2)
    p.add_argument("--no-quick-reject", dest="quick_reject",
                   action="store_false", default=True,
                   help="skip the -fsyntax-only pre-filter")
    p.add_argument("--replace", nargs=2, metavar=("OLD", "NEW"), action="append",
                   help="put a specific bug back: substitute OLD with NEW (must "
                        "match exactly once) and report which tests notice. "
                        "Repeatable - a batch runs across lanes in parallel")
    p.add_argument("--bugs", metavar="FILE",
                   help="a file of bugs to reintroduce, '# name' then a "
                        "<<< old === new >>> block each. Runs in parallel")
    p.add_argument("--reverse", metavar="REF",
                   help="put one specific bug back by reverse-applying REF's "
                        "changes to the header, keeping today's tests")
    p.add_argument("--file", metavar="PATH", default=HEADER,
                   help="repo-relative file to mutate (default the header)")
    p.add_argument("--cmake-arg", metavar="ARG", action="append", default=[],
                   help="extra cmake argument for every lane, repeatable. This "
                        "is how you ask a different question: whether a leg "
                        "catches the bug, which the default Release build "
                        "cannot answer. Needs the '=' form, because the value "
                        "starts with a dash: "
                        "--cmake-arg=-DNB_sanitizer=ON "
                        "--cmake-arg=-DCMAKE_CXX_COMPILER=clang++")
    p.add_argument("--test-filter", metavar="PATTERN",
                   help="run only matching doctest cases. 'unit_*' cuts the "
                        "test phase from 4.7s to 0.8s here, because the example "
                        "and tutorial cases are benchmarks that assert nothing "
                        "- but they can still die by crashing, so a kill only "
                        "they would have seen is reported as a survivor")
    p.add_argument("--dry-run", action="store_true",
                   help="list the mutants and the likely runtime, then stop")
    p.add_argument("--workdir", default=None, help="where lanes are copied")
    p.add_argument("--json", metavar="FILE", help="write the full result set")
    args = p.parse_args()

    HEADER = args.file
    header_path = os.path.join(REPO, HEADER)
    if not os.path.exists(header_path):
        p.error("no such file: %s" % HEADER)
    with open(header_path, encoding="utf-8") as f:
        original = f.read()

    if sum(bool(x) for x in (args.replace, args.bugs, args.reverse)) > 1:
        p.error("--replace, --bugs and --reverse are three ways to say the same "
                "thing; pick one")

    if args.replace or args.bugs or args.reverse:
        bugs = []
        if args.bugs:
            bugs = parse_bug_file(args.bugs)
        elif args.replace:
            bugs = [dict(name="%s -> %s" % (old.strip()[:34], new.strip()[:34]),
                         old=old, new=new) for old, new in args.replace]
        if args.dry_run:
            for bug in bugs:
                print("  %s" % bug["name"])
            print("\n%d bugs, roughly %.0f s over %d lanes"
                  % (len(bugs), 8.4 * len(bugs) / max(1, args.lanes), args.lanes))
            return 0

        print(fingerprint(args) + "\n")
        workdir = prepare_workdir(args)
        # one lane is plenty for a single bug, and copying four trees to run one
        # of them is most of that job's wall clock
        count = 1 if args.reverse else min(max(1, args.lanes), max(1, len(bugs)))
        lanes = [Lane(workdir, i, args.jobs, args.test_filter, args.cmake_arg)
                 for i in range(count)]
        try:
            with concurrent.futures.ThreadPoolExecutor(count) as pool:
                list(pool.map(lambda ln: ln.setup(print), lanes))
            baseline(lanes[0], args, print)
            if args.reverse:
                mutated = reverse_commit_bug(lanes[0], args.reverse)
                bugs = [dict(name="reverse of %s" % args.reverse,
                             old=original, new=mutated)]
            results = run_bugs(lanes, original, bugs, args, print)
            if args.json:
                with open(args.json, "w") as f:
                    json.dump(results, f, indent=2)
                print("\nwrote %s" % args.json)
            return 1 if any(r["verdict"] == "UNCAUGHT" for r in results) else 0
        finally:
            shutil.rmtree(workdir, ignore_errors=True)

    line_filter = None
    if args.diff:
        line_filter = changed_lines(args.diff, HEADER)
        if not line_filter:
            print("no changed lines in %s since %s" % (HEADER, args.diff))
            return 0
    elif args.lines:
        a, _, b = args.lines.partition("-")
        line_filter = set(range(int(a), int(b or a) + 1))

    sites = mutation_sites(original, code_mask(original), line_filter)
    if args.limit and len(sites) > args.limit:
        import random
        random.Random(args.shuffle_seed).shuffle(sites)
        sites = sites[:args.limit]
    sites.sort(key=lambda s: s["offset"])

    if not sites:
        print("no mutation sites")
        return 0

    if args.dry_run:
        by_line = {}
        for s in sites:
            by_line.setdefault(s["line"], []).append(s["description"])
        for line in sorted(by_line):
            print("  line %d: %s" % (line, ", ".join(by_line[line])))
        # ~8s per mutant measured on this tree, most of it the rebuild every TU
        # needs because they all include the header.
        seconds = 8.4 * len(sites) / max(1, args.lanes)
        print("\n%d mutants over %d lanes, roughly %s"
              % (len(sites), args.lanes,
                 "%.0f min" % (seconds / 60) if seconds > 90
                 else "%.0f s" % seconds))
        return 0

    print(fingerprint(args) + "\n")
    workdir = prepare_workdir(args)

    print_lock = threading.Lock()

    def log(msg):
        with print_lock:
            print(msg, flush=True)

    lanes = [Lane(workdir, i, args.jobs, args.test_filter, args.cmake_arg)
             for i in range(max(1, args.lanes))]
    started = time.time()
    for lane in lanes:
        lane.setup(log)
    baseline(lanes[0], args, log)

    log("%d mutants over %d lanes" % (len(sites), len(lanes)))
    results = []
    pending = queue.Queue()
    for idx, site in enumerate(sites):
        pending.put((idx, site))

    def worker(lane):
        while True:
            try:
                idx, site = pending.get_nowait()
            except queue.Empty:
                return
            verdict, failing = evaluate(lane, original, site, args)
            record = dict(site, verdict=verdict, killed_by=failing)
            with print_lock:
                results.append(record)
                done = len(results)
                print("[%d/%d] line %d  %-22s %s%s" %
                      (done, len(sites), site["line"], site["description"],
                       verdict, " (%s)" % failing if failing else ""),
                      flush=True)

    with concurrent.futures.ThreadPoolExecutor(len(lanes)) as pool:
        list(pool.map(worker, lanes))

    results.sort(key=lambda r: r["offset"])
    counts = {k: sum(1 for r in results if r["verdict"] == k)
              for k in ("test", "build", "timeout", "survived")}
    killed = counts["test"] + counts["build"] + counts["timeout"]

    print("\n" + "=" * 72)
    print("mutants          %d in %.0fs" % (len(results), time.time() - started))
    print("killed by test   %d   <- the number worth moving" % counts["test"])
    print("killed by build  %d   <- the compiler, not your tests" % counts["build"])
    print("killed by hang   %d" % counts["timeout"])
    print("SURVIVED         %d" % counts["survived"])
    if results:
        print("score            %.0f%% killed, %.0f%% by a test"
              % (100.0 * killed / len(results),
                 100.0 * counts["test"] / len(results)))

    survivors = [r for r in results if r["verdict"] == "survived"]
    if survivors:
        print("\nsurvivors - nothing noticed these:")
        src_lines = original.splitlines()
        for r in survivors:
            text = src_lines[r["line"] - 1].strip() if r["line"] <= len(src_lines) else ""
            print("  %s:%d  %s" % (HEADER, r["line"], r["description"]))
            print("      %s" % text[:100])

    if args.json:
        with open(args.json, "w") as f:
            json.dump(dict(counts=counts, results=results), f, indent=2)
        print("\nwrote %s" % args.json)

    shutil.rmtree(workdir, ignore_errors=True)
    return 1 if survivors else 0


if __name__ == "__main__":
    sys.exit(main())
