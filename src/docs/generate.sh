#!/bin/sh
set -e

# Run from src/docs directory
#
# On Fedora everything is packaged:
#   sudo dnf install doxygen python3-sphinx python3-breathe python3-sphinx_rtd_theme \
#                    python3-recommonmark
#
# Otherwise use a venv:
#   python3 -m venv $HOME/venv && source $HOME/venv/bin/activate
#   pip install -r requirements.txt
#
# requirements.txt matches what Fedora 44 packages, so both routes produce the same output.

# rm should work, this should copy all files, but for safety reasons I'm not doing it...
#rm -Rvf ../../docs
mkdir -p _build/doxygen
doxygen
sphinx-build -E . ../../docs
