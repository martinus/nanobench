#!/usr/bin/env python3
"""`src/scripts/mutate/mutate_core.py` is vendored, and this is what says so out loud.

That file is byte-identical in unordered_dense and in oans. unordered_dense is where it is
*tested*: its `scripts/test_mutate.py` is a hermetic suite over the half of the tool that decides
what a verdict means, and it covers the cmake backend this repository drives even though that
repository builds with meson -- as it covers oans's make backend and minunit harness, which
neither of the other two runs. Sharing the file is what buys that. A copy edited in place ends it
quietly: the other suite stays green while this repository runs something nothing tests.

So two checks, and neither can see the other repository:

- the core still hashes to what was recorded when it was last vendored. Changing it means copying
  the file back the other way, running that suite, and recording the new hash in all three -- after
  which "are these in sync?" is a question `diff` can answer on the `.sha256` files.
- the adapter beside it still fits the core it is vendored against, which is this repository's
  own half and is not covered over there. A renamed hook is silent otherwise: `test_cwd` misspelled
  leaves `nb` writing its artifacts into a lane's source tree, which nothing fails on and
  `sync_tree` then quietly deletes on the next reuse.
"""

import hashlib
import importlib.util
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
CORE = REPO / "src" / "scripts" / "mutate" / "mutate_core.py"
RECORDED = CORE.with_suffix(".sha256")
ADAPTER = CORE.parent / "mutate.py"


def load(path):
    spec = importlib.util.spec_from_file_location(path.stem, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check_vendored():
    if not CORE.exists():
        print("%s is missing" % CORE.relative_to(REPO))
        return 1
    digest = hashlib.sha256(CORE.read_bytes()).hexdigest()
    if not RECORDED.exists():
        print("%s is missing - write %s into it" % (RECORDED.relative_to(REPO), digest))
        return 1
    recorded = RECORDED.read_text(encoding="utf-8").split()[0]
    if digest != recorded:
        print("%s has changed since it was last vendored.\n"
              "  recorded  %s\n"
              "  actual    %s\n"
              "It is shared with unordered_dense and oans, and tested in the first of those, so "
              "a change here is only part of one. Make it in that repository, run its "
              "scripts/test_mutate.py, copy the file into all three, and record the new hash in "
              "each:\n"
              "  echo %s > %s"
              % (CORE.relative_to(REPO), recorded, digest, digest,
                 RECORDED.relative_to(REPO)))
        return 1
    print("mutate_core.py vendored at %s" % digest[:12])
    return 0


def check_adapter():
    """The project object, against the core it is vendored with.

    Every one of these is a way of being wrong that produces no error at all - a run that copies
    the wrong tree, builds nothing, or scores mutants against a suite that never ran.

    The generic half of the question is `Project.problems()`, which lives in the core and is
    covered by unordered_dense's scripts/test_mutate.py. Only what is nanobench's own is here: a
    check that lived in this file *and* nowhere else would sit in the one repository with no test
    suite, which is the arrangement this whole change exists to end.
    """
    adapter = load(ADAPTER)
    project = adapter.Nanobench()

    # The core the adapter *imported*, rather than a second module object loaded
    # from the same path: those compare unequal, and an isinstance against the
    # wrong one fails for a reason that has nothing to do with the code.
    core = adapter.mutate_core
    problems = list(project.problems())
    if Path(core.__file__).resolve() != CORE:
        problems.append("the adapter imported %s rather than the vendored core beside it"
                        % core.__file__)
    if not isinstance(project, core.Project):
        problems.append("the project does not derive from the vendored core's Project")
    if Path(project.repo) != REPO:
        problems.append("repo resolves to %s, not %s - every lane would copy the wrong tree"
                        % (project.repo, REPO))
    if not project.syntax_tu:
        problems.append("no syntax_tu, so every mutant that is not valid C++ costs a full "
                        "rebuild rather than half a second")

    # The suite runs from the build directory: `nb` writes its example artifacts
    # into the working directory, and unit_templates wants an absolute __FILE__.
    lane = type("Lane", (), dict(dir="/lane", build="/lane/build"))()
    if project.test_cwd(lane) != "/lane/build":
        problems.append("test_cwd is not the build directory, so nb's artifacts would land in "
                        "the lane's source tree")

    # Guarded, because `problems()` has already said if there is no backend at
    # all and a traceback on top of that message helps nobody read it. The flag
    # spelling is in CLAUDE.md and in the docstring; the sanitizer answer is what
    # the fingerprint's "no sanitizer in this build" NOTE is printed off.
    if project.backend is not None:
        if project.backend.arg_flag != "--cmake-arg":
            problems.append("the pass-through flag is %s, not --cmake-arg"
                            % project.backend.arg_flag)
        if project.sanitizer(["-DNB_sanitizer=ON"]) == "none":
            problems.append("a sanitizer build reports itself as unsanitized, and the "
                            "fingerprint would print the warning that belongs to a plain build")
        if project.sanitizer([]) != "none":
            problems.append("a plain build does not report itself as unsanitized")

    # ubsan.supp is named by the adapter and read by a sanitizer lane only, so a
    # rename would surface as a mutant nobody catches rather than as an error.
    if not (REPO / "ubsan.supp").exists():
        problems.append("ubsan_suppressions names ubsan.supp, which is not in the repository root")

    if problems:
        print("%s does not fit the core it is vendored with:" % ADAPTER.relative_to(REPO))
        for problem in problems:
            print("  %s" % problem)
        return 1
    return 0


def check_runs():
    """One real invocation, because everything above is static.

    `--dry-run` reaches the argument parsing, the project object, the lexer and the estimate, and
    stops before the first lane is copied - a second or so for the whole path a person takes.
    """
    r = subprocess.run([sys.executable, str(ADAPTER), "--dry-run", "--lines", "1-40"],
                       capture_output=True, text=True, cwd=str(REPO))
    if r.returncode != 0:
        print("%s --dry-run failed:\n%s%s" % (ADAPTER.relative_to(REPO), r.stdout, r.stderr))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(check_vendored() or check_adapter() or check_runs())
