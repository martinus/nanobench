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
# Note that requirements.txt pins Sphinx 6.1.3, which needs python <= 3.11. The build also
# works with current Sphinx (tested with 8.2.3 / breathe 4.36.0 / rtd-theme 3.1.0), but
# rtd-theme 3.x restyles every page, so that regenerates all of docs/.

# rm should work, this should copy all files, but for safety reasons I'm not doing it...
#rm -Rvf ../../docs
mkdir -p _build/doxygen
doxygen
sphinx-build -E . ../../docs
