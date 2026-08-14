Installation
============

Direct Inclusion
----------------

#. Download ``nanobench.h`` from the :download:`release <https://github.com/martinus/nanobench/releases/latest>`
   and make it available in your project. 

#. Create a .cpp file, e.g. ``nanobench.cpp``, where the bulk of nanobench is compiled.

   .. literalinclude:: ../test/app/nanobench.cpp
      :language: c++
      :linenos:
      :caption: nanobench.cpp

#. Compile e.g. with ``g++ -O3 -I../include -c nanobench.cpp``. This compiles
   the bulk of nanobench, and took 2.4 seconds on my machine. It needs to be compiled only once whenever you upgrade nanobench.


CMake Integration
-----------------

``nanobench`` can be integrated with CMake's `FetchContent <https://cmake.org/cmake/help/latest/module/FetchContent.html>`_ or as
a `git submodule <https://git-scm.com/book/en/v2/Git-Tools-Submodules>`_. Here is a full example how to this can be done:

.. literalinclude:: code/CMakeLists.txt
   :language: CMake
   :linenos:
   :caption: CMakeLists.txt

Usage
=====


#. Create the actual benchmark code, in ``full_example.cpp``:

   .. literalinclude:: code/full_example.cpp
      :language: c++
      :linenos:
      :caption: full_example.cpp

   The most important entry entry point is :cpp:class:`ankerl::nanobench::Bench`. It creates a benchmarking object,
   optionally configures it, and then runs the code to benchmark with :cpp:func:`run() <ankerl::nanobench::Bench::run()>`.

#. Compile and link the example with 

   .. code:: sh
   
      g++ -O3 -I../include nanobench.o full_example.cpp -o full_example
      
   This takes just 0.28 seconds on my machine.

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
   5.63ns on my machine (wall-clock time), or ~178 million operations per second. Runtime
   fluctuates by around 0.0%, so the results are very stable. Each call
   required 3 instructions, which took ~18 CPU cycles. There was a single branch per call,
   with only 0.1% mispredicted. 

Nanobench does not come with a test runner, so you can easily use it with any framework you like.  In the remaining examples, I'm
using `doctest <https://github.com/onqtam/doctest>`_ as a unit test framework.

.. important::

   **The five columns you always get are** ``ns/op``, ``op/s``, ``err%``, ``total`` and ``benchmark``.
   Everything between ``err%`` and ``total`` above - ``ins/op``, ``cyc/op``, ``IPC``, ``bra/op`` and ``miss%`` -
   comes from CPU performance counters, which nanobench can only read **on Linux**, through
   `perf events <http://web.eece.maine.edu/~vweaver/projects/perf_events/>`_. Elsewhere - and inside most
   containers and virtual machines, even Linux ones - those columns are silently left out, and your table
   looks like this instead:

   .. code:: text

      |               ns/op |                op/s |    err% |     total | benchmark
      |--------------------:|--------------------:|--------:|----------:|:----------
      |                5.63 |      177,595,338.98 |    0.0% |      0.00 | `compare_exchange_strong`

   That is not a misconfiguration of your benchmark. On Linux you may be able to get the extra columns by
   `changing permissions <https://www.kernel.org/doc/html/latest/admin-guide/perf-security.html#unprivileged-users>`_
   through ``perf_event_paranoid`` or an ACL.


Output Columns
==============

Each row is one benchmark. All measurements are the **median over the epochs** (11 by default), never a
single reading, and ``op`` refers to whatever you set with :cpp:func:`unit() <ankerl::nanobench::Bench::unit()>`
and :cpp:func:`batch() <ankerl::nanobench::Bench::batch()>` - by default a single call of your lambda.

================ ========== =================================================================================
Column           Platform   Meaning
================ ========== =================================================================================
``ns/op``        all        Wall-clock time for one operation. The header follows
                            :cpp:func:`timeUnit() <ankerl::nanobench::Bench::timeUnit()>`, so it can also read
                            ``ms/op``, ``us/op`` or ``ps/op``.
``op/s``         all        Operations per second - simply the reciprocal of the time per operation.
``err%``         all        `Median absolute percentage error <https://en.wikipedia.org/wiki/Mean_absolute_percentage_error>`_
                            of the per-epoch timings: how much the individual epochs disagreed with each
                            other. It is a stability measure of *this* run, not a confidence interval, and
                            not a comparison against any other benchmark. Above 5% the row is flagged as
                            unstable with a ``:wavy_dash:`` marker.
``ins/op``       Linux only Retired CPU instructions per operation.
``cyc/op``       Linux only CPU cycles per operation.
``IPC``          Linux only Instructions per cycle, i.e. ``ins/op`` divided by ``cyc/op``. Higher is better;
                            it says how well the operation keeps the pipeline busy.
``bra/op``       Linux only Retired branch instructions per operation.
``miss%``        Linux only Percentage of those branches that were mispredicted.
``total``        all        **Total wall-clock time in seconds** that this row cost to measure - the sum over
                            all epochs of iterations times elapsed time. It is the price you paid for the
                            measurement, not a property of the code being benchmarked. Use it to find
                            benchmarks that make your suite slow.
``benchmark``    all        The name passed to :cpp:func:`run() <ankerl::nanobench::Bench::run()>`, or the
                            title when :cpp:func:`relative() <ankerl::nanobench::Bench::relative()>` is used.
================ ========== =================================================================================

When :cpp:func:`relative() <ankerl::nanobench::Bench::relative()>` is enabled, a leading ``relative`` column
is added that compares each row against the first one.

.. note::

   Nanobench measures **time** and, where available, the CPU performance counters listed above. It does not
   measure memory usage, allocations, or peak RSS - use a heap profiler such as
   `heaptrack <https://github.com/KDE/heaptrack>`_ or ``valgrind --tool=massif`` for that, or count
   allocations yourself and report them with :cpp:func:`context() <ankerl::nanobench::Bench::context()>`.


Choosing the Columns
--------------------

The full table is over 150 characters wide and wraps in most terminals. Hide what you are not reading
with :cpp:func:`hideColumn() <ankerl::nanobench::Bench::hideColumn()>`, which takes an
:cpp:enum:`ankerl::nanobench::Column`:

.. code-block:: c++

   ankerl::nanobench::Bench()
       .hideColumn(ankerl::nanobench::Column::instructions)
       .hideColumn(ankerl::nanobench::Column::cycles)
       .hideColumn(ankerl::nanobench::Column::ipc)
       .run("narrow", [] { /* ... */ });

Hiding only changes what is printed - the measurement still happens, and
:cpp:func:`results() <ankerl::nanobench::Bench::results()>` and the render templates are unaffected.
:cpp:func:`showColumn() <ankerl::nanobench::Bench::showColumn()>` puts one back.

Columns that are redundant for your benchmark are worth dropping too. With
:cpp:func:`unit() <ankerl::nanobench::Bench::unit()>` set to something like ``MFlop``, ``MFlop/s`` is the
number you read and ``ns/MFlop`` is the same information inverted:

.. code-block:: c++

   bench.unit("MFlop").hideColumn(ankerl::nanobench::Column::timePerUnit);

.. tip::

   :cpp:func:`performanceCounters(false) <ankerl::nanobench::Bench::performanceCounters()>` hides all five
   counter columns at once *and* stops measuring them, which is usually what you want if you never look
   at them.


Showing Context as Columns
--------------------------

Context variables set with :cpp:func:`context() <ankerl::nanobench::Bench::context()>` are otherwise only
reachable from a render template, which makes parameterised benchmarks hard to read on the console.
:cpp:func:`contextColumn() <ankerl::nanobench::Bench::contextColumn()>` puts one in the table:

.. code-block:: c++

   ankerl::nanobench::Bench bench;
   bench.performanceCounters(false).contextColumn("threads");

   for (int threads : {1, 4, 16}) {
       bench.context("threads", std::to_string(threads))
            .run("parallel sum", [&] { /* ... */ });
   }

.. code-block:: text

   |  threads |               ns/op |                op/s |    err% |     total | benchmark
   |---------:|--------------------:|--------------------:|--------:|----------:|:----------
   |        1 |               95.32 |       10,490,357.51 |    0.4% |      0.00 | `parallel sum`
   |        4 |               27.11 |       36,886,742.02 |    0.7% |      0.00 | `parallel sum`
   |       16 |               11.80 |       84,745,762.71 |    1.2% |      0.00 | `parallel sum`

Context columns come before the measurements, in the order you name them. A row whose context does not
have the variable gets an empty cell rather than a missing column, so the table stays rectangular.


Examples
========


Something Fast
--------------

Let’s benchmarks how fast we can do ``x += x`` for ``uint64_t``:

.. literalinclude:: ../test/tutorial_fast_v1.cpp
   :language: c++
   :linenos:
   :caption: tutorial_fast_v1.cpp

After 0.2ms we get this output:

.. code-block:: text

   |               ns/op |                op/s |    err% |     total | benchmark
   |--------------------:|--------------------:|--------:|----------:|:----------
   |                   - |                   - |       - |         - | :boom: `++x` (iterations overflow. Maybe your code got optimized away?)

No data there! We only get ``:boom: iterations overflow.``.  The compiler could optimize ``x += x``
away because we never used the output. Thanks to ``doNotOptimizeAway``, this is easy to fix:

.. literalinclude:: ../test/tutorial_fast_v2.cpp
   :language: c++
   :linenos:
   :caption: tutorial_fast_v2.cpp
   :emphasize-lines: 8

This time the benchmark runs for 2.2ms and we actually get reasonable data:

.. code-block:: text

   |               ns/op |                op/s |    err% |          ins/op |          cyc/op |    IPC |         bra/op |   miss% |     total | benchmark
   |--------------------:|--------------------:|--------:|----------------:|----------------:|-------:|---------------:|--------:|----------:|:----------
   |                0.31 |    3,192,444,232.50 |    0.0% |            1.00 |            1.00 |  0.998 |           0.00 |    0.0% |      0.00 | `++x`

It's a very stable result. One run the op/s is 3,192 million/sec, the next time I execute it I get 3,168 million/sec. It always takes 
1.00 instructions per operation on my machine, and can do this in ~1 cycle.


Something Slow
--------------

Let's benchmark if sleeping for 100ms really takes 100ms.

.. literalinclude:: ../test/tutorial_slow_v1.cpp
   :language: c++
   :linenos:
   :caption: tutorial_slow_v1.cpp

After 1.1 seconds I get

.. code-block:: text

   |               ns/op |                op/s |    err% |          ins/op |          cyc/op |    IPC |         bra/op |   miss% |     total | benchmark
   |--------------------:|--------------------:|--------:|----------------:|----------------:|-------:|---------------:|--------:|----------:|:---------------------
   |      100,125,753.00 |                9.99 |    0.0% |           51.00 |        7,714.00 |  0.007 |          11.00 |   90.9% |      1.10 | `sleep 100ms, auto`


So we actually take 100.125ms instead of 100ms. Next time I run it, I get 100.141. Also a very stable result. Interestingly, sleep takes 51 instructions but 7,714 cycles - so we only got 0.007 instructions per cycle. That's extremely low, but expected of ``sleep``. It also required 11 branches, of which 90.9% were mispredicted on average.

If the extremely slow 1.1 second is too much for you, you can manually configure the number of evaluations (epochs):

.. literalinclude:: ../test/tutorial_slow_v2.cpp
   :language: c++
   :linenos:
   :caption: tutorial_slow_v2.cpp

.. code-block:: text

   |               ns/op |                op/s |    err% |          ins/op |          cyc/op |    IPC |         bra/op |   miss% |     total | benchmark
   |--------------------:|--------------------:|--------:|----------------:|----------------:|-------:|---------------:|--------:|----------:|:----------
   |      100,099,096.00 |                9.99 |    0.0% |           51.00 |        7,182.00 |  0.007 |          11.00 |   90.9% |      0.30 | `sleep 100ms`

This time it took only 0.3 seconds, but with only 3 evaluations instead of 11. The err% will be less meaningful, but since the benchmark is so stable it doesn't really matter.


Something Unstable
------------------

Let's create an extreme artificial test that's hard to benchmark, because runtime fluctuates randomly: Each iteration
randomly skip between 0-254 random numbers:

.. literalinclude:: ../test/tutorial_fluctuating_v1.cpp
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

.. literalinclude:: ../test/tutorial_fluctuating_v2.cpp
   :language: c++
   :linenos:
   :caption: tutorial_fluctuating_v2.cpp
   :emphasize-lines: 10


The fluctuations are much better:

.. code-block:: text

   |               ns/op |                op/s |    err% |          ins/op |          cyc/op |    IPC |         bra/op |   miss% |     total | benchmark
   |--------------------:|--------------------:|--------:|----------------:|----------------:|-------:|---------------:|--------:|----------:|:----------
   |              277.31 |        3,606,106.48 |    0.7% |        3,531.75 |          885.18 |  3.990 |         291.59 |    0.7% |      0.00 | `random fluctuations`


The results are  more stable, with only 0.7% error.

.. _Tutorial Comparing Results:


Untimed Setup
=============

Some benchmarks consume the thing they operate on: sorting a vector leaves it sorted, so the second
iteration measures sorting an already-sorted vector. :cpp:func:`setup() <ankerl::nanobench::Bench::setup()>`
runs a lambda that is *not* measured, so the state can be restored without polluting the result:

.. code-block:: c++

   std::vector<uint64_t> data = makeRandomData();
   std::vector<uint64_t> const pristine = data;

   ankerl::nanobench::Bench().setup([&] { data = pristine; })
                             .run("sort", [&] {
                                 std::sort(data.begin(), data.end());
                                 ankerl::nanobench::doNotOptimizeAway(data.data());
                             });

.. important::

   **The setup runs once per epoch, not once per iteration.** An epoch calls your lambda many times in
   a row, and the setup does not run again in between. The example above is therefore *not* fixed by
   ``setup()`` alone: the first call in an epoch sorts random data, and every call after it re-sorts
   already-sorted data.

   ``setup()`` is the right tool when the operation can be repeated as-is and only the starting state
   has to be established once - allocating a buffer, opening a file, warming a cache, restoring a
   value that the operation reads but does not destroy.

When every single call really does destroy the state, you have two honest options:

#. **One iteration per epoch.** :cpp:func:`epochIterations(1) <ankerl::nanobench::Bench::epochIterations()>`
   makes an epoch a single call, so the setup effectively runs per iteration:

   .. code-block:: c++

      bench.epochIterations(1).epochs(1000)
           .setup([&] { data = pristine; })
           .run("sort", [&] { std::sort(data.begin(), data.end()); });

   The cost is accuracy: a single call is now timed against the clock's resolution, so this only
   gives useful numbers when one call takes appreciably longer than that - roughly microseconds and
   up. Expect a much larger ``err%``, and use many epochs.

#. **Measure the setup separately and subtract it.** Benchmark just the restoration, then benchmark
   restoration plus operation, and take the difference. More work, but it keeps the tight measurement
   loop tight, and for fast operations it is the more accurate answer.

.. note::

   Nanobench deliberately does not offer a ``PauseTiming()``/``ResumeTiming()`` pair inside the
   measurement loop. Starting and stopping the clock - and the Linux performance counters - around
   every iteration costs more than most operations worth benchmarking, which quietly destroys exactly
   the measurements it is meant to enable.


Comparing Results
=================
To compare results, keep the `ankerl::nanobench::Bench` object around, enable `.relative(true)`, and `.run(...)` your benchmarks. All benchmarks will be automatically compared to the first one.


As an example, I have implemented a comparison of multiple random number generators.
Here several RNGs are compared to a baseline calculated from `std::default_random_engine`.
I factored out the general benchmarking code so it's easy to use for each of the random number generators:


.. literalinclude:: ../test/example_random_number_generators.cpp
   :language: c++
   :linenos:
   :caption: example_random_number_generators.cpp (excerpt)
   :lines: 309-



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

It shows that :cpp:class:`ankerl::nanobench::Rng` is one of the fastest RNG, and has the least amount of
fluctuation. It takes only 1.57ns to generate a random ``uint64_t``, so ~638 million calls per
seconds are possible. To the left we show relative performance compared to ``std::default_random_engine``. 

.. note::

   Here pure runtime performance is not necessarily the best benchmark.
   Especially the fastest RNG's can be inlined and use instruction level parallelism
   to their advantage: they immediately return an old state, and while user code can
   already use that value, the next value is calculated in parallel. See the excellent paper
   at `romu-random <http://www.romu-random.org>`_ for details.


.. _ab-comparison:

A/B Comparison
==============

:cpp:func:`relative() <ankerl::nanobench::Bench::relative()>` runs one benchmark to completion, then
the next, and divides the two medians. That measures the machine as much as the code. Nanobench's own
test suite records the failure: two **identical** workloads came out 38% apart on a CI runner, while
each reported an ``err%`` of 0.5. ``err%`` is the spread *within* one benchmark; the comparison
depends on the spread *between* them, and nothing in that table tells you anything about it.

:cpp:func:`ab() <ankerl::nanobench::Bench::ab()>` compares two alternatives against each other inside
the same slice of time. A frequency ramp, a noisy neighbour or thermal throttling then hits both and
cancels out of the ratio, and what comes back is a ratio with a confidence interval:

.. literalinclude:: ../test/tutorial_ab.cpp
   :language: c++
   :linenos:
   :caption: tutorial_ab.cpp

Takes about 200ms and prints:

.. code-block:: text

   A/B: `cheap` is 2.40x faster than `murmurhash3`
        95% CI 2.39x .. 2.41x, 52 paired rounds, ABBA interleaved

   A/B: no difference resolved between `murmurhash3` and `splitmix64`
        ratio 1.00x, 95% CI 1.00x .. 1.00x, 52 paired rounds, ABBA interleaved

Reading the output
------------------

**The interval, not the ratio, is the result.** ``2.40x`` alone is a number; ``2.40x, 95% CI 2.39 ..
2.41`` is a claim you can defend in a code review. If the interval were ``0.9 .. 1.8`` the point
estimate would still say 1.3x, and it would still mean nothing.

**"No difference resolved" is not "the same speed."** It says this experiment did not separate them,
which is usually a reason to raise :cpp:func:`epochs() <ankerl::nanobench::Bench::epochs()>` rather
than a conclusion. :cpp:func:`isSignificant() <ankerl::nanobench::AbResult::isSignificant()>` is
exactly the question of whether the interval excludes 1.

**Watch for tied rounds.** When the verdict says ``(38 tied at the clock's resolution)``, both sides
measured the same time to the last tick the clock can report. That is not evidence they are equally
fast, it is the clock running out of resolution, and the fix is a longer epoch via
:cpp:func:`minEpochTime() <ankerl::nanobench::Bench::minEpochTime()>` - not more rounds.

**Use more rounds than the default.** An epoch is about a millisecond, so ``epochs(51)`` costs a tenth
of a second and buys an interval narrow enough to act on. Fewer than six rounds cannot support a 95%
statement at all, so ``ab()`` always runs at least eight.

How it works
------------

Everything below is a choice, and each one is there because leaving it out changes the answer. They
fall into three groups: what gets run, how a round is reduced to one number, and how those numbers
become an interval.

Designing the experiment
~~~~~~~~~~~~~~~~~~~~~~~~

#. **A fixed iteration count, calibrated once up front.** :cpp:func:`run() <ankerl::nanobench::Bench::run()>`
   adapts the count as it goes; ``ab()`` does not. An iteration count that drifted between rounds
   would be a second thing changing while the comparison is being made.

#. **The same count for both sides.** An epoch carries a fixed overhead - two clock reads and the
   performance counter ioctls - and what gets compared is time *per iteration*, so that overhead is
   divided by the count. Calibrating each side separately gives them slightly different counts and
   amortizes the overhead differently between them. That is a systematic bias in the ratio, which no
   amount of pairing removes: it measured 1.2% on 200µs epochs. The smaller of the two counts is
   used, so neither side runs an epoch longer than it was asked for.

#. **Interleaving.** One epoch of each per round, adjacent in time, rather than all of A and then all
   of B. Anything that affects both - a frequency ramp, a noisy neighbour, thermal throttling - is
   then common to the pair and cancels out of the difference.

#. **ABBA order within each block of four rounds.** The two A positions and the two B positions then
   have the same mean time, so a drift that is *linear* over the block cancels exactly. Simply
   alternating would leave one side always first.

#. **The block orientation randomized.** ABBA or BAAB, chosen per block. A fixed pattern can line up
   with a periodic disturbance; randomizing the phase stops that without giving up the balance.

#. **Rounds rounded up to whole blocks, and never fewer than eight.** A partial block hands one side
   the first position more often than the other, which is the imbalance ABBA exists to remove. Eight
   is the floor because fewer than six rounds cannot support a 95% statement at all - see the sign
   test below - and reporting one anyway would be inventing confidence rather than measuring it.

Reducing a round to one number
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#. **The log ratio,** ``ln(tA) - ln(tB)``. A speedup is multiplicative, and logs turn that into a
   difference, which is what every statistic below assumes. It also makes the scale symmetric: twice
   as fast and half as fast are the same distance from zero, where the raw ratios 2.0 and 0.5 are
   not. Because ``ln`` is monotonic the median commutes with it, so exponentiating at the end gives
   back *exactly* the median of the per-round ratios - the transform costs nothing in
   interpretation.

#. **Rounds where either side measured zero are dropped.** The logarithm of zero is not a large
   number, it is negative infinity, and a single one of those makes every statistic downstream
   meaningless. A round with no measurable time carries no ratio, so it carries no information.

Estimating and reporting
~~~~~~~~~~~~~~~~~~~~~~~~

#. **The median as the point estimate.** Its breakdown point is 50%: half the rounds can be
   arbitrarily corrupted before it moves at all. A mean has a breakdown point of zero - one
   descheduled round is enough to shift it - and benchmark timings are exactly the kind of data that
   produces the occasional wild value. This is the same reasoning behind nanobench reporting a median
   and an ``err%`` rather than a mean and a standard deviation.

#. **The sign test for the interval.** The interval is a pair of order statistics: with *n* rounds,
   the k-th smallest and k-th largest log ratios, where *k* is the largest one whose binomial tail
   still fits in 2.5%. It assumes the rounds are independent and nothing else - no distribution
   shape, no symmetry, no finite variance, no asymptotics - and it is exact at every *n* rather than
   approximately right for large ones. It is also deterministic: there is no resampling anywhere, so
   the same measurements always give the same interval.

#. **Significance is the interval excluding 1.** :cpp:func:`isSignificant() <ankerl::nanobench::AbResult::isSignificant()>`
   asks only that, which is the same thing as a two-sided test at 5% - and it is reported as an
   interval rather than a p-value because the interval says how big the difference is as well as
   whether there is one.

#. **Tied rounds are counted and reported.** When both sides land on the same tick, that round says
   the clock could not tell them apart, which is different information from the two being equally
   fast. Any median-based interval collapses to zero width when most rounds tie, so the count is what
   distinguishes a real ``1.00x .. 1.00x`` from a measurement that never had the resolution.

What was rejected
~~~~~~~~~~~~~~~~~

The interval was the hard choice. Measured on right-skewed differences whose true median is exactly
zero - which is what paired timings look like when one side has the heavier tail, since an operation
can be arbitrarily slower but never faster than its floor:

.. list-table::
   :header-rows: 1
   :widths: 22 14 64

   * - Method
     - Coverage
     - 
   * - **sign test**
     - **96.7%**
     - Used. Assumes independence and nothing else. Slightly conservative, and about 10% wider than
       the bootstrap - which is the right direction to err for a number that ends up in a pull
       request.
   * - percentile bootstrap
     - 95.0%
     - Wants its own asymptotics, converges slowly for a median in particular, and collapses to zero
       width once a majority of rounds tie. Needs a seed, so the reported number depends on it.
   * - Wilcoxon / Hodges-Lehmann
     - 91.5%
     - Narrowest, and wrong here: it wants the differences symmetric about their median, which is
       precisely what skewed timings do not give. It also estimates the *pseudomedian*, so its
       interval would not be an interval for the number being reported.
   * - t-interval
     - n/a
     - Wants normality and a finite variance, and has a breakdown point of zero.

Serial correlation, and why warmup is not the answer
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The one assumption every method above shares is that the rounds are independent. Benchmark rounds
have every reason not to be: frequency and thermal state persist across them, so a slow round makes
the next one more likely to be slow too. Positive autocorrelation would make any of these intervals
narrower than they should be.

Measured over 200 rounds of two identical operations, lag-1 autocorrelation:

================================== ==================
Series                             lag-1 correlation
================================== ==================
raw per-round times                +0.10
paired log ratios                  -0.08 .. -0.01
paired, first 20 rounds discarded  -0.07 .. -0.01
================================== ==================

The correlation is real, and it is in the **raw** times. It is not in the paired differences,
because that is what pairing is for: the drift is common to both sides of a round and subtracts out.
What is left is slightly *negative*, which makes the interval conservative rather than too narrow -
consistent with the false-positive rate measured below the nominal 5% rather than above it.

.. note::

   This is also why :cpp:func:`warmup() <ankerl::nanobench::Bench::warmup()>` is not the fix it looks
   like. Discarding the first twenty rounds - a warmup by another name - moves none of the numbers
   above, because calibration has already run each side for about a full epoch before the first round
   starts. ``ab()`` does honor ``warmup()`` if you set it, but do not expect it to buy an honest
   interval that pairing has not already bought.

   The measurements above are one machine and one workload. A laptop that thermally throttles under
   sustained load could look different, and a block bootstrap would be the principled answer if it
   ever does.

.. warning::

   **Interleaving is a different measurement from running either side alone.** Each alternative runs
   with the other's cache and branch predictor state. That is usually the more honest number for
   "which should I ship", and it is the wrong number for "how fast is this in isolation" - use
   :cpp:func:`run() <ankerl::nanobench::Bench::run()>` for that.

.. note::

   This resolves differences down to about 0.1%, which means it also resolves differences caused by
   where the compiler happened to put the code. Two *distinct* functions doing identical arithmetic
   report a difference about 10% of the time. That is a real difference - just not the one you meant
   to measure - so treat a sub-percent result as a question about code layout rather than about the
   algorithm.

.. note::

   The interval assumes the rounds are independent, and strictly they are not: thermal and frequency
   state persist across them. Interleaving removes drift from each paired difference but does not make
   the differences independent, and positive autocorrelation makes any such interval narrower than it
   should be. In practice the measured error rate lands slightly *below* the nominal 5% rather than
   above it, but the assumption is worth knowing about before trusting a very tight interval.


.. _asymptotic-complexity:

Asymptotic Complexity
=====================

It is possible to calculate asymptotic complexity (Big O) from multiple runs of a benchmark. Run the
benchmark with different complexity N, then nanobench can calculate the best fitting curve. 

The following example finds out the asymptotic complexity of ``std::set``'s ``find()``.

.. literalinclude:: ../test/tutorial_complexity_set.cpp
   :language: c++
   :linenos:
   :caption: tutorial_complexity_set.cpp

The loop runs the benchmark 10 times, with different set sizes from 10 to 10k.

.. note::
 
   Each of the 10 benchmark runs automatically scales the number of iterations so results are still
   fast and accurate. In total the whole test takes about 90ms.

The :cpp:class:`Bench <ankerl::nanobench::Bench>` object holds the benchmark results of the 10 benchmark runs. Each benchmark is recorded with a
different setting for :cpp:func:`complexityN <ankerl::nanobench::Bench::complexityN>`.

After the benchmark prints the benchmark results, we calculate & print the Big O of the most important complexity functions.
``std::cout << bench.complexityBigO() << std::endl;`` prints e.g. this markdown table:

.. code-block:: text

   |   coefficient |   err% | complexity
   |--------------:|-------:|------------
   |   6.66562e-09 |  29.1% | O(log n)
   |   1.47588e-11 |  58.3% | O(n)
   |   1.10742e-12 |  62.6% | O(n log n)
   |   5.15683e-08 |  63.8% | O(1)
   |   1.40387e-15 |  78.7% | O(n^2)
   |   1.32792e-19 |  85.7% | O(n^3)

The table is sorted, best fitting complexity function first. So
:math:`\mathcal{O}(\log{}n)` provides the best approximation for the complexity. Interestingly, in that case error compared to
:math:`\mathcal{O}(n)` is not very large, which can be an indication that even though the red-black tree should theoretically have
logarithmic complexity, in practices that is not perfectly the case.


.. _templating:

Rendering Mustache-like Templates
=================================

Nanobench comes with a powerful `Mustache <https://mustache.github.io/>`_-like template mechanism to process the benchmark
results into all kinds of formats. You can find a full description of all possible tags at :cpp:func:`ankerl::nanobench::render()`.

Several preconfigured format exist in the namespace ``ankerl::nanobench::templates``. Rendering these templates can be done
with either :cpp:func:`ankerl::nanobench::render()`, or directly with :cpp:func:`ankerl::nanobench::Bench::render()`.

The following example shows how to use the `CSV - Comma-Separated Values`_ template, without writing the standard output.

.. literalinclude:: ../test/tutorial_render_simple.cpp
   :language: c++
   :linenos:
   :emphasize-lines: 12,17
   :caption: tutorial_render_simple.cpp


In line 11 we call :cpp:func:`Bench::output() <ankerl::nanobench::Bench::output()>` with ``nullptr``, thus disabling the standard output.

After the benchmark we directly call :cpp:func:`Bench::render() <ankerl::nanobench::Bench::render()>` in line 16. Here we use the 
CSV template, and write the rendered output to ``std::cout``. When running, we get just the CSV output to the console which looks like this:

.. literalinclude:: _generated/tutorial_render_simple.txt
   :language: text

Nanobench comes with a few preconfigured templates, residing in the namespace ``ankerl::nanobench::templates``. To demonstrate what these templates can do,
here is a simple example that benchmarks two random generators ``std::mt19937_64`` and ``std::knuth_b`` and prints both the template and the rendered
output:

.. literalinclude:: ../test/tutorial_mustache.cpp
   :language: c++
   :linenos:

Nanobench allows to specify further context information, which may be accessed using ``{{context(name)}}`` where ``name`` names a variable defined via :cpp:func:`Bench::context() <ankerl::nanobench::Bench::context()>`.

.. literalinclude:: ../test/tutorial_context.cpp
   :language: c++
   :linenos:


Time Units in Templates
-----------------------

``elapsed`` is in **seconds**, which is rarely what a report should contain, and the template language has
no arithmetic to rescale it. So the time measure also comes in ``elapsedms``, ``elapsedus`` and
``elapsedns``:

.. code-block:: text

   "name";"min_ms";"median_ns"
   {{#result}}"{{name}}";{{minimum(elapsedms)}};{{median(elapsedns)}}
   {{/result}}

All of them are per iteration, exactly like ``elapsed``. ``{{medianAbsolutePercentError(...)}}`` is a
relative error, so it is the same number whichever you ask for.

.. note::

   Two things that are easy to mix up here:

   * ``{{unit}}`` renders :cpp:func:`Bench::unit() <ankerl::nanobench::Bench::unit()>`, which is what a
     *batch* counts - ``op`` by default, or ``byte``, ``MFlop`` and so on. It is not a time unit, which is
     why it prints ``op`` next to a value in seconds.
   * :cpp:func:`Bench::timeUnit() <ankerl::nanobench::Bench::timeUnit()>` only changes the ``ns/op``
     column of the console table. It does not affect what templates render; use the suffixed measures
     above for that.

.. _tutorial-template-csv:

CSV - Comma-Separated Values
----------------------------

The function :cpp:func:`ankerl::nanobench::templates::csv` provides this template:

.. literalinclude:: _generated/mustache.template.csv
   :language: text
   :linenos:

This generates a compact CSV file, where entries are separated by a semicolon `;`. Run with the example, I get this output:

.. literalinclude:: _generated/mustache.render.csv
   :language: text
   :linenos:

Rendered as CSV table:

.. csv-table::
   :file: _generated/mustache.render.csv
   :header-rows: 1
   :delim: ;

Note that the CSV template doesn't provide all the data that is available.



.. _tutorial-template-html:

HTML Box Plots
--------------

With the template :cpp:func:`ankerl::nanobench::templates::htmlBoxplot()` you get a `plotly <https://plotly.com/javascript/>`_ based HTML output which generates
a boxplot of the runtime. The template is rather simple.

.. literalinclude:: _generated/mustache.template.html
   :language: xml
   :linenos:

This generates a nice interactive boxplot, which gives a nice visual showcase of the runtime performance of the evaluated benchmarks. Each epoch is visualized as a dot,
and the boxplot itself shows median, percentiles, and outliers. You'll might want to increase the default number of epochs for an even better visualization result.

.. raw:: html
   :file: _generated/mustache.render.html


.. _tutorial-template-json:

JSON - JavaScript Object Notation
---------------------------------

The :cpp:func:`ankerl::nanobench::templates::json` template gives everything, all data that is available, from all runs. The template is therefore quite complex:

.. literalinclude:: _generated/mustache.template.json
   :language: text
   :linenos:

This also gives the data from each separate :cpp:func:`ankerl::nanobench::Bench::epochs()`, not just the accumulated data as in the CSV template.

.. literalinclude:: _generated/mustache.render.json
   :language: json
   :linenos:

.. _tutorial-template-pyperf:

pyperf - Python pyperf module Output
------------------------------------

`Pyperf <https://pyperf.readthedocs.io/en/latest/>`_ is a powerful tool for benchmarking and system tuning, and it can also analyze 
benchmark results. This template allows generation of output so it can be used for further analysis with pyperf.

.. note::

   Pyperf supports only a single benchmark result per generated output, so it is best to create a new
   ``Bench`` object for each benchmark.

The template looks like this. Note that it directly makes use of ``{{#measurement}}``, which is only possible when there is a single result in the benchmark.

.. literalinclude:: _generated/mustache.template.pyperf
   :language: text
   :linenos:

Here is an example that generates pyperf compatible output for a benchmark that shuffles a vector:

.. literalinclude:: ../test/example_pyperf.cpp
   :language: cpp
   :linenos:
   :caption: example_pyperf.cpp

This benchmark run creates the two files ``pyperf_shuffle_std.json`` and ``pyperf_shuffle_nanobench.json``.
Here are some of the analysis you can do:

Show Benchmark Statistics
~~~~~~~~~~~~~~~~~~~~~~~~~

Output from ``python3 -m pyperf stats pyperf_shuffle_std.json``:

.. literalinclude:: code/pyperf_stats.txt
   :language: text

Show a Histogram
~~~~~~~~~~~~~~~~

It's often interesting to see a histogram, especially to visually find out if there are outliers involved. 
Run ``python3 -m pyperf hist pyperf_shuffle_std.json`` produces this output

.. literalinclude:: code/pyperf_hist.txt
   :language: text


Compare Results
~~~~~~~~~~~~~~~

We have generated two results in the above examples, and we can compare them easily with ``python3 -m pyperf compare_to a.json b.json``:

.. literalinclude:: code/pyperf_compare_to.txt
   :language: text

For more information of pyperfs analysis capability, please see `pyperf - Analyze benchmark results <https://pyperf.readthedocs.io/en/latest/analyze.html>`_.
