#!/usr/bin/env bash
ROOT_SRC_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd )"

find ${ROOT_SRC_DIR} -name "*.md" -exec ./gh-md-toc --insert \{\} \;
find ${ROOT_SRC_DIR} -name "*.md.orig.*" -exec rm \{\} \;
find ${ROOT_SRC_DIR} -name "*.md.toc.*" -exec rm \{\} \;
