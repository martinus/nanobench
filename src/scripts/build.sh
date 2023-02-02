#!/usr/bin/env bash

ROOT_SRC_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd )"
CXX=clang++ cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ${ROOT_SRC_DIR}
