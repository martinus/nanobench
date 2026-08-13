#!/usr/bin/env python3

# Branch protection requires exactly one status check, the "ci-green" job of main.yml, which
# succeeds only when every other job in that workflow did. That indirection is what keeps a renamed
# or newly added leg from silently dropping out of the required set - but only for as long as the
# gate actually depends on everything. Adding a job and forgetting to list it in `needs` would give
# back exactly the silent gap the gate exists to close, and nothing would go red.
#
# So: check it here, where the failure is a lint error naming the missing job instead of a green
# merge that was never tested.

import os
import pathlib
import sys

try:
    import yaml
except ImportError:
    print("lint-ci-gate: PyYAML is not installed, skipping")
    sys.exit(0)

root = os.path.abspath(pathlib.Path(__file__).parent.parent.parent.parent)
workflow = f"{root}/.github/workflows/main.yml"

GATE = "ci-green"

with open(workflow, encoding="utf-8") as f:
    jobs = yaml.safe_load(f)["jobs"]

exit_code = 0

if GATE not in jobs:
    print(f"{workflow}: no '{GATE}' job - branch protection requires it as its status check")
    sys.exit(1)

needs = jobs[GATE].get("needs", [])
if isinstance(needs, str):
    needs = [needs]

expected = [name for name in jobs if name != GATE]

missing = [name for name in expected if name not in needs]
if missing:
    print(f"{workflow}: '{GATE}' does not depend on {', '.join(missing)}")
    print(f"  add them to its `needs:`, otherwise those jobs can fail without blocking a merge")
    exit_code = 1

stale = [name for name in needs if name not in jobs]
if stale:
    print(f"{workflow}: '{GATE}' depends on {', '.join(stale)}, which no longer exist")
    exit_code = 1

# `if: always()` is what makes the gate run even when a dependency failed. Without it the gate is
# skipped, and GitHub treats a skipped required check as satisfied.
if jobs[GATE].get("if") != "always()":
    print(f"{workflow}: '{GATE}' needs `if: always()`, or it is skipped when a job fails")
    print(f"  a skipped required check counts as satisfied, so the merge would not be blocked")
    exit_code = 1

if exit_code == 0:
    print(f"lint-ci-gate: '{GATE}' covers all {len(expected)} jobs")

sys.exit(exit_code)
