#!/usr/bin/env python3

import os
import pathlib
import re


root = os.path.abspath(pathlib.Path(__file__).parent.parent.parent.parent)

# filename, pattern, number of occurrences
file_pattern_count = [
    (
        f"{root}/src/docs/conf.py",
        r"version = 'v(\d+)\.(\d+)\.(\d+)'",
        1),
    (f"{root}/docs/CODE_OF_CONDUCT.html", r"^\s*v(\d+)\.(\d+)\.(\d+)$", 1),
    (f"{root}/docs/comparison.html", r"^\s*v(\d+)\.(\d+)\.(\d+)$", 1),
    (f"{root}/docs/genindex.html", r"^\s*v(\d+)\.(\d+)\.(\d+)$", 1),
    (f"{root}/docs/index.html", r"^\s*v(\d+)\.(\d+)\.(\d+)$", 1),
    (f"{root}/docs/license.html", r"^\s*v(\d+)\.(\d+)\.(\d+)$", 1),
    (f"{root}/docs/reference.html", r"^\s*v(\d+)\.(\d+)\.(\d+)$", 1),
    (f"{root}/docs/search.html", r"^\s*v(\d+)\.(\d+)\.(\d+)$", 1),
    (f"{root}/docs/tutorial.html", r"^\s*v(\d+)\.(\d+)\.(\d+)$", 1),
]

# let's parse the reference from svector.h
major = "??"
minor = "??"
patch = "??"
with open(f"{root}/src/include/nanobench.h", "r") as f:
    for line in f:
        r = re.search(
            r"#define ANKERL_NANOBENCH_VERSION_([A-Z]+) (\d+)", line)
        if not r:
            continue

        if "MAJOR" == r.group(1):
            major = r.group(2)
        elif "MINOR" == r.group(1):
            minor = r.group(2)
        elif "PATCH" == r.group(1):
            patch = r.group(2)
        else:
            "match but with something else!"
            exit(1)

is_ok = True
for (filename, pattern, count) in file_pattern_count:
    num_found = 0
    with open(filename, "r") as f:
        for line in f:
            r = re.search(pattern, line)
            if r:
                num_found += 1
                if major != r.group(1) or minor != r.group(2) or patch != r.group(3):
                    is_ok = False
                    print(
                        f"ERROR in {filename}: got '{line.strip()}' but version should be '{major}.{minor}.{patch}'")
    if num_found != count:
        is_ok = False
        print(
            f"ERROR in {filename}: expected {count} occurrences but found it {num_found} times")

if not is_ok:
    exit(1)
