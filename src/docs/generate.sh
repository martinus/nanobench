#!/bin/sh
set -e

# Run from src/docs directory
#
# Install breathe (assuming there's a venv in ~/git/venv)
#   source ~/git/venv/bin/activate
#   pip install -U sphinx breathe sphinx_rtd_theme recommonmark

# rm should work, this should copy all files, but for safety reasons I'm not doing it...
#rm -Rvf ../../docs
mkdir -p _build/doxygen
doxygen
source ~/venv/bin/activate
python ~/venv/bin/sphinx-build -E . ../../docs
