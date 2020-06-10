#!/usr/bin/env bash
set -ex

rm -f CMakeCache.txt
rm -Rvf CMakeFiles
ccache -C

CXX=clang++ cmake -G Ninja -DCMAKE_CXX_FLAGS="-O2 -ftime-trace" -DCMAKE_BUILD_TYPE=Release ../..

~/git/ClangBuildAnalyzer/bin/ClangBuildAnalyzer --start .
time ninja
~/git/ClangBuildAnalyzer/bin/ClangBuildAnalyzer --stop . session.data
~/git/ClangBuildAnalyzer/bin/ClangBuildAnalyzer --analyze session.data
