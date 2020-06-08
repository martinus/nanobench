#!/bin/sh

doxygen
sphinx-build -E . ../../docs