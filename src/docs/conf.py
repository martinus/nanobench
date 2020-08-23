# Configuration file for the Sphinx documentation builder.
#
# This file only contains a selection of the most common options. For a full
# list see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Path setup --------------------------------------------------------------

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
#
# import os
# import sys
# sys.path.insert(0, os.path.abspath('.'))


# -- Project information -----------------------------------------------------

project = 'nanobench'
copyright = '2019-2020 Martin Ankerl <martin.ankerl@gmail.com>'
author = 'Martin Ankerl'
version = 'v4.2.0'

# -- General configuration ---------------------------------------------------

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.
extensions = [ "breathe", "sphinx.ext.mathjax", "recommonmark" ]
breathe_projects = { "nanobench": "./_build/doxygen/xml" }
breathe_default_project = "nanobench"

# Add any paths that contain templates here, relative to this directory.
templates_path = ['_templates']

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
# This pattern also affects html_static_path and html_extra_path.
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']


# -- Options for HTML output -------------------------------------------------

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
#
html_theme = 'sphinx_rtd_theme'

# see https://sphinx-rtd-theme.readthedocs.io/en/stable/configuring.html
html_theme_options = {
    'analytics_id': 'UA-36863101-2',
    'display_version': True,
    'sticky_navigation': True,
    'collapse_navigation': False,
    'navigation_depth': 4,
}

html_context = {
    'display_github': True,
    'github_user': 'martinus',
    'github_repo': 'nanobench',
    'github_version': 'master',
    'conf_py_path': '/src/docs/',
}

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ['_static']
html_show_sourcelink = True
html_favicon = 'favicon.ico'
html_logo = 'nanobench-logo.svg'

# hide ankerl::nanobench:: prefix, especially in index
cpp_index_common_prefix = ['ankerl::nanobench::']