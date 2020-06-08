#!/bin/sh

doxygen
/usr/bin/sphinx-build -E . ../../docs
