========
Tutorial
========


Installation
============

1. Download ``nanobench.h`` from the releases and make it available
   in your project.

2. Create a .cpp file, e.g. ``nanobench.cpp``, where the bulk of
   nanobench is compiled:

   .. literalinclude:: ../src/test/app/nanobench.cpp
      :language: c++
      :linenos:

3. Wherever you want to use nanobench’s functionality, simply
   ``#include <nanobench.h>``. All functionality resides within namespace
   ``ankerl::nanobench``.


Full Example
============

#. Create `nanobench.cpp`:

   .. literalinclude:: ../src/test/app/nanobench.cpp
      :language: c++
      :linenos:
      :caption: nanobench.cpp

#. Compile with ``g++ -O3 -DNDEBUG -I../include -c nanobench.cpp``. This compiles
   the bulk of nanobench, and took 3.37 seconds on my machine. It's done only once.

#. Create the actual benchmark code, in `full_example.cpp`:

   .. literalinclude:: ../src/scripts/full_example.cpp
      :language: c++
      :linenos:
      :caption: full_example.cpp

   The most important entry point is :cpp:class:`ankerl::nanobench::Bench`. It creates a benchmarking object,
   optionally configures it, and then runs the code to benchmark with :cpp:func:`run() <ankerl::nanobench::Bench::run()>`.

#. Compile & link with ``g++ -O3 -DNDEBUG -I../include nanobench.o full_example.cpp -o full_example``. This takes just 0.5 seconds on my machine.

#. Run ``./full_example``, which gives an output like this:

   .. code:: text

      |               ns/op |                op/s |    err% |          ins/op |          cyc/op |    IPC |         bra/op |   miss% |     total | benchmark
      |--------------------:|--------------------:|--------:|----------------:|----------------:|-------:|---------------:|--------:|----------:|:----------
      |                5.63 |      177,595,338.98 |    0.0% |            3.00 |           17.98 |  0.167 |           1.00 |    0.1% |      0.00 | `compare_exchange_strong`

   Which renders as

   ==================== ===================== ========= ================= ================= ======== ================ ========= =========== ==================
                  ns/op                  op/s      err%            ins/op            cyc/op      IPC           bra/op     miss%       total   benchmark
   ==================== ===================== ========= ================= ================= ======== ================ ========= =========== ==================
                   5.63        177,595,338.98      0.0%              3.00             17.98    0.167             1.00      0.1%        0.00   ``compare_exchange_strong``
   ==================== ===================== ========= ================= ================= ======== ================ ========= =========== ==================

   Which means that one ``x.compare_exchange_strong(y, 0);`` call takes
   5.63ns on my machine, or ~178 million operations per second. Runtime
   fluctuates by around 0.0%, so the results are very stable. Each call
   required 3 instructions, which took ~18 CPU cycles. There was a single branch per call,
   with only 0.1% misspredicted. 

In the remaining examples, I'm using `doctest <https://github.com/onqtam/doctest>`_ as a unit test framework, which is like `Catch2 <https://github.com/catchorg/Catch2>`_ - but
compiles much faster. It pairs well with nanobench.

Benchmarking Something Fast
===========================

Let’s benchmarks how fast we can do ``x += x`` for ``uint64_t``:

.. literalinclude:: ../src/test/tutorial_fast_v1.cpp
   :language: c++
   :linenos:
   :caption: tutorial_fast_v1.cpp

After 0.2ms we get this output:

.. code-block:: text

   |               ns/op |                op/s |    err% |     total | benchmark
   |--------------------:|--------------------:|--------:|----------:|:----------
   |                   - |                   - |       - |         - | :boom: `++x` (iterations overflow. Maybe your code got optimized away?)

No data there! we only get ``:boom: iterations overflow.``.  The compiler could optimize ``x += x``
away because we never used the output. Thanks to ``doNotOptimizeAway``, this is easy to fix:

.. literalinclude:: ../src/test/tutorial_fast_v2.cpp
   :language: c++
   :linenos:
   :caption: tutorial_fast_v2.cpp
   :emphasize-lines: 7

This time the benchmark runs for 2.2ms and we actually get reasonable data:

.. code-block:: text

   |               ns/op |                op/s |    err% |          ins/op |          cyc/op |    IPC |         bra/op |   miss% |     total | benchmark
   |--------------------:|--------------------:|--------:|----------------:|----------------:|-------:|---------------:|--------:|----------:|:----------
   |                0.31 |    3,192,444,232.50 |    0.0% |            1.00 |            1.00 |  0.998 |           0.00 |    0.0% |      0.00 | `++x`

It's a very stable result. One run the op/s is 3,192 million/sec, the next time I execute it I get 3,168 million/sec. It always takes 
1.00 instructions per operation on my machine, and can do this in ~1 cycle.


Benchmarking Something Slow
===========================

Let's benchmark if sleeping for 100ms really takes 100ms.

.. literalinclude:: ../src/test/tutorial_slow_v1.cpp
   :language: c++
   :linenos:
   :caption: tutorial_slow_v1.cpp

After 1.1 seconds I get

.. code-block:: text

   |               ns/op |                op/s |    err% |          ins/op |          cyc/op |    IPC |         bra/op |   miss% |     total | benchmark
   |--------------------:|--------------------:|--------:|----------------:|----------------:|-------:|---------------:|--------:|----------:|:---------------------
   |      100,125,753.00 |                9.99 |    0.0% |           51.00 |        7,714.00 |  0.007 |          11.00 |   90.9% |      1.10 | `sleep 100ms, auto`


So we actually take 100.125ms instead of 100ms. Next time I run it, I get 100.141. Also a very stable result. Interestingly, sleep takes 51 instructions but 7,714 cycles - so we only got 0.007 instructions per cycle. That's extremely low, but expected of ``sleep``. It also required 11 branches, of which 90.9% were misspredicted on average.

If the extremely slow 1.1 second is too much for you, you can manually configure the number of evaluations (epochs):

.. literalinclude:: ../src/test/tutorial_slow_v2.cpp
   :language: c++
   :linenos:
   :caption: tutorial_slow_v2.cpp

.. code-block:: text

   |               ns/op |                op/s |    err% |          ins/op |          cyc/op |    IPC |         bra/op |   miss% |     total | benchmark
   |--------------------:|--------------------:|--------:|----------------:|----------------:|-------:|---------------:|--------:|----------:|:----------
   |      100,099,096.00 |                9.99 |    0.0% |           51.00 |        7,182.00 |  0.007 |          11.00 |   90.9% |      0.30 | `sleep 100ms`

This time it took only 0.3 seconds, but with only 3 evaluations instead of 11. The err% will be less meaningfull, but since the benchmark is so stable it doesn't really matter.


Benchmarking Something Unstable
===============================

Lets create an extreme artifical test that's hard to benchmark, because runtime fluctuates randomly: Each iteration
randomly skip between 0-254 random numbers:

.. literalinclude:: ../src/test/tutorial_fluctuating_v1.cpp
   :language: c++
   :linenos:
   :caption: tutorial_fluctuating_v1.cpp

After 2.3ms, I get this result:

.. code-block:: text

   |               ns/op |                op/s |    err% |          ins/op |          cyc/op |    IPC |         bra/op |   miss% |     total | benchmark
   |--------------------:|--------------------:|--------:|----------------:|----------------:|-------:|---------------:|--------:|----------:|:----------
   |              334.12 |        2,992,911.53 |    6.3% |        3,486.44 |        1,068.67 |  3.262 |         287.86 |    0.7% |      0.00 | :wavy_dash: `random fluctuations` (Unstable with ~56.7 iters. Increase `minEpochIterations` to e.g. 567)

So on average each loop takes about 334.12ns, but we get a warning that the results are unstable. The median percentage error is 6.3% which is quite high, 

Let's use the suggestion and set the minimum number of iterations to 5000, and try again:

.. literalinclude:: ../src/test/tutorial_fluctuating_v2.cpp
   :language: c++
   :linenos:
   :caption: tutorial_fluctuating_v2.cpp
   :emphasize-lines: 9


The fluctuations are much better:

.. code-block:: text

   |               ns/op |                op/s |    err% |          ins/op |          cyc/op |    IPC |         bra/op |   miss% |     total | benchmark
   |--------------------:|--------------------:|--------:|----------------:|----------------:|-------:|---------------:|--------:|----------:|:----------
   |              277.31 |        3,606,106.48 |    0.7% |        3,531.75 |          885.18 |  3.990 |         291.59 |    0.7% |      0.00 | `random fluctuations`


The results are  more stable, with only 0.7% error.

.. _Tutorial Comparing Results:


Comparing Results
=================

I have implemented a comparison of multiple random number generators.
Here several RNGs are compared to a baseline calculated from `std::default_random_engine`.
I factored out the general benchmarking code so it's easy to use for each of the random number generators:


.. literalinclude:: ../src/test/example_random_number_generators.cpp
   :language: c++
   :linenos:
   :caption: example_random_number_generators.cpp (excerpt)
   :lines: 274-



Runs for 60ms and prints this table:

.. code-block:: text

   | relative |         ns/uint64_t |          uint64_t/s |    err% |    ins/uint64_t |    cyc/uint64_t |    IPC |   bra/uint64_t |   miss% |     total | Random Number Generators
   |---------:|--------------------:|--------------------:|--------:|----------------:|----------------:|-------:|---------------:|--------:|----------:|:-------------------------
   |   100.0% |               35.87 |       27,881,924.28 |    2.3% |          127.80 |          114.61 |  1.115 |           9.77 |    3.7% |      0.00 | `std::default_random_engine`
   |   490.3% |                7.32 |      136,699,693.21 |    0.6% |           89.55 |           23.49 |  3.812 |           9.51 |    0.1% |      0.00 | `std::mt19937`
   | 1,767.4% |                2.03 |      492,786,582.33 |    0.6% |           24.38 |            6.48 |  3.761 |           1.26 |    0.6% |      0.00 | `std::mt19937_64`
   |    85.2% |               42.08 |       23,764,853.03 |    0.7% |          157.07 |          134.62 |  1.167 |          19.51 |    7.6% |      0.00 | `std::ranlux24_base`
   |   121.3% |               29.56 |       33,824,759.51 |    0.5% |           91.03 |           94.35 |  0.965 |          10.00 |    8.1% |      0.00 | `std::ranlux48_base`
   |    17.4% |              205.67 |        4,862,080.59 |    1.2% |          709.83 |          657.10 |  1.080 |         101.79 |   16.1% |      0.00 | `std::ranlux24_base`
   |     8.7% |              412.46 |        2,424,497.97 |    1.8% |        1,514.70 |        1,318.43 |  1.149 |         219.09 |   16.7% |      0.00 | `std::ranlux48`
   |    59.2% |               60.60 |       16,502,276.18 |    1.9% |          253.77 |          193.39 |  1.312 |          24.93 |    1.5% |      0.00 | `std::knuth_b`
   | 5,187.1% |                0.69 |    1,446,254,071.66 |    0.1% |            6.00 |            2.21 |  2.714 |           0.00 |    0.0% |      0.00 | `WyRng`
   | 1,431.7% |                2.51 |      399,177,833.54 |    0.0% |           21.00 |            8.01 |  2.621 |           0.00 |    0.0% |      0.00 | `NasamRng`
   | 2,629.9% |                1.36 |      733,279,957.30 |    0.1% |           13.00 |            4.36 |  2.982 |           0.00 |    0.0% |      0.00 | `Sfc4`
   | 3,815.7% |                0.94 |    1,063,889,655.17 |    0.0% |           11.00 |            3.01 |  3.661 |           0.00 |    0.0% |      0.00 | `RomuTrio`
   | 3,529.5% |                1.02 |      984,102,081.37 |    0.3% |            9.00 |            3.25 |  2.768 |           0.00 |    0.0% |      0.00 | `RomuDuo`
   | 4,580.4% |                0.78 |    1,277,113,402.06 |    0.0% |            7.00 |            2.50 |  2.797 |           0.00 |    0.0% |      0.00 | `RomuDuoJr`
   | 2,291.2% |                1.57 |      638,820,992.09 |    0.0% |           11.00 |            5.00 |  2.200 |           0.00 |    0.0% |      0.00 | `ankerl::nanobench::Rng`

It shows that ``ankerl::nanobench::Rng`` is one of the fastest RNG, and has the least amount of
fluctuation. It takes only 1.57ns to generate a random ``uint64_t``, so ~638 million calls per
seconds are possible. To the left we show relative performance compared to ``std::default_random_engine``. 

.. note::

   Here pure runtime performance is not necessarily the best benchmark.
   Especially the fastest RNG's can be inlined and use instruction level parallelism
   to their advantage: they immediately return an old state, and while user code can
   already use that value, the next value is calculated in parallel. See the excellent paper
   at `romu-random <http://www.romu-random.org>`_ for details.

