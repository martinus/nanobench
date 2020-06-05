Tutorial
========


Installation
------------

1. Download ``nanobench.h`` from the releases and make it available
   in your project.

2. Create a .cpp file, e.g. ``nanobench.cpp``, where the bulk of
   nanobench is compiled:

   .. code-block:: c++
      :linenos:

      #define ANKERL_NANOBENCH_IMPLEMENT
      #include <nanobench.h>

3. Wherever you want to use nanobench’s functionality, simply
   ``#include <nanobench.h>``. All functionality resides within namespace
   ``ankerl::nanobench``.


Simple Example
--------------

.. literalinclude:: ../src/scripts/full_example.cpp
   :language: c++
   :linenos:

Compiled with
``g++ -O2 -DNDEBUG full_example.cpp -I../include -o full_example`` runs
for 5ms and then prints this markdown table:

.. code:: txt

    |               ns/op |                op/s |   MdAPE |         ins/op |         cyc/op |    IPC |      branch/op | missed% | benchmark
    |--------------------:|--------------------:|--------:|---------------:|---------------:|-------:|---------------:|--------:|:----------------------------------------------
    |                7.81 |      128,092,931.19 |    0.0% |           4.00 |          24.93 |  0.161 |           0.00 |    0.0% | `compare_exchange_strong`
    

Which means that one ``x.compare_exchange_strong(y, 0);`` call takes
7.81ns on my machine, or ~128 million operations per second. Runtime
fluctuates by around 0.0%, so the results are very stable. Each call
required 4 instructions, which took ~25 CPU cycles. There were no
branches in this code, so we also got no branch misspredictions.

In the remaining examples, I compile nanobench’s implementation once in
a separate cpp file `nanobench.cpp`_. This compiles most of nanobench,
and is relatively slow - but only needs to be done once. The usage of
nanobench compiles very fast.

.. code-block:: c++
   :linenos:

   #define ANKERL_NANOBENCH_IMPLEMENT
   #include <nanobench.h>

I use `doctest <https://github.com/onqtam/doctest>`_ as a unit test framework, which is like `Catch2 <https://github.com/catchorg/Catch2>`_ - but
compiles much faster. It pairs well with nanobench.

Something Fast
--------------

Let’s benchmarks how fast we can do ``x += x`` for ``uint64_t``:

.. code-block:: c++
   :linenos:

   TEST_CASE("comparison_fast_v1") {
       uint64_t x = 1;
       ankerl::nanobench::Bench().run("x += x", [&] { x += x; });
   }


.. _Tutorial: #tutorial
.. _Installation: #installation
.. _Examples: #examples
.. _Simple Example: #simple-example
.. _Something Fast: #something-fast
.. _Something Slow: #something-slow
.. _Something Unstable: #something-unstable
.. _Comparing Results: #comparing-results
.. _releases: https://github.com/martinus/nanobench/releases
.. _src/test: https://github.com/martinus/nanobench/tree/master/src/test
.. _full_example.cpp: https://github.com/martinus/nanobench/tree/master/src/scripts/full_example.cpp
.. _nanobench.cpp: https://github.com/martinus/nanobench/tree/master/src/test/app/nanobench.cpp
.. _doctest: https://github.com/onqtam/doctest
.. _Catch2: https://github.com/catchorg/Catch2
