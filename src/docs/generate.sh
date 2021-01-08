#!/bin/sh

# Run from src/docs directory
#
# Install breathe (assuming there's a venv in ~/git/venv)
#   source ~/git/venv/bin/activate
#   pip install breathe
#   pip install sphinx_rtd_theme
#   pip install recommonmark
doxygen
source ~/git/venv/bin/activate
python ~/git/venv/bin/sphinx-build -E . ../../docs
