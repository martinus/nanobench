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
#
# The output is a local preview: ../../docs is gitignored, and the published site is built by
# .github/workflows/docs.yml. Deleting it first is safe for that reason, and it is what keeps the
# preview honest - while docs/ was committed, files the theme had stopped referencing survived
# every regeneration and accumulated for years.
rm -Rf ../../docs
mkdir -p _build/doxygen
doxygen
sphinx-build -E . ../../docs
