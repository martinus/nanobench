#!/bin/sh
set -e

# Run from src/docs directory
#
# Use a venv - requirements.txt is the same pinned set that docs.yml installs, so this is the
# only route that previews what will actually be published:
#   python3 -m venv $HOME/venv && source $HOME/venv/bin/activate
#   pip install -r requirements.txt
#
# On Fedora everything is also packaged, and it used to be equivalent:
#   sudo dnf install doxygen python3-sphinx python3-breathe python3-sphinx_rtd_theme \
#                    python3-recommonmark
#
# It is not equivalent any more. Fedora 44 ships Sphinx 8.2.3 and requirements.txt pins 9.1.0,
# which rewrote the search machinery - the pages render the same, but _static and searchindex.js
# do not. Fine for a quick look at prose, misleading for anything about the generated assets.
#
# The output is a local preview: ../../docs is gitignored, and the published site is built by
# .github/workflows/docs.yml. Deleting it first is safe for that reason, and it is what keeps the
# preview honest - while docs/ was committed, files the theme had stopped referencing survived
# every regeneration and accumulated for years.
rm -Rf ../../docs
mkdir -p _build/doxygen
doxygen
sphinx-build -E . ../../docs
