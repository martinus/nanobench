===============================
``ankerl::nanobench`` Reference
===============================

.. How to link: https://breathe.readthedocs.io/en/latest/domains.html
   E.g. :cpp:class:`ankerl::nanobench::Bench`

----------------------------------------------------------------
:cpp:class:`Bench <ankerl::nanobench::Bench>` - Main Entry Point
----------------------------------------------------------------


.. doxygenclass:: ankerl::nanobench::Bench
    :members:



------------------------------------------------------------------
:cpp:enum:`Column <ankerl::nanobench::Column>` - Table Columns
------------------------------------------------------------------

.. doxygenenum:: ankerl::nanobench::Column



---------------------------------------------------------------
:cpp:class:`Rng <ankerl::nanobench::Rng>` - Extremely fast PRNG
---------------------------------------------------------------

.. doxygenclass:: ankerl::nanobench::Rng
    :members:



-------------------------------------------------------------------
:cpp:class:`Result <ankerl::nanobench::Result>` - Benchmark Results
-------------------------------------------------------------------

.. doxygenclass:: ankerl::nanobench::Result
    :members:



---------------------------------------------------------------------------------------
:cpp:class:`CompareResult <ankerl::nanobench::CompareResult>` - Comparison Outcome
---------------------------------------------------------------------------------------

Returned by :cpp:func:`Bench::compare() <ankerl::nanobench::Bench::compare()>`. See the tutorial at
:ref:`ab-comparison` for a worked example and how to read the interval.

.. doxygenclass:: ankerl::nanobench::CompareResult
    :members:



----------------------------------------------------------------------
:cpp:func:`doNotOptimizeAway() <ankerl::nanobench::doNotOptimizeAway>`
----------------------------------------------------------------------

.. doxygenfunction:: ankerl::nanobench::doNotOptimizeAway(Arg&& arg)



--------------------------------------------------------------------------
:cpp:func:`render() <ankerl::nanobench::render>` - Mustache-like Templates
--------------------------------------------------------------------------


.. doxygenfunction:: ankerl::nanobench::render(char const *mustacheTemplate, Bench const &bench, std::ostream &out)


:cpp:func:`templates::csv <ankerl::nanobench::templates::csv>`
--------------------------------------------------------------

.. doxygenfunction:: ankerl::nanobench::templates::csv



:cpp:func:`templates::htmlBoxplot <ankerl::nanobench::templates::htmlBoxplot>`
------------------------------------------------------------------------------

.. doxygenfunction:: ankerl::nanobench::templates::htmlBoxplot



:cpp:func:`templates::json <ankerl::nanobench::templates::json>`
----------------------------------------------------------------

.. doxygenfunction:: ankerl::nanobench::templates::json


:cpp:func:`templates::pyperf <ankerl::nanobench::templates::pyperf>`
------------------------------------------------------------------------------

.. doxygenfunction:: ankerl::nanobench::templates::pyperf


---------------------
Environment Variables
---------------------

``NANOBENCH_ENDLESS`` - Run a Specific Test Endlessly
-----------------------------------------------------

Sometimes it helps to run a benchmark for a very long time, so that it's possible to attach with a profiler like
`perf <https://perf.wiki.kernel.org/index.php/Main_Page>`_ and get meaningful statistics. This can be done with the environment variable
``NANOBENCH_ENDLESS``. E.g. to run the benchmark with the name ``x += x`` endlessly, call the app this way:

.. code-block:: sh

   NANOBENCH_ENDLESS="x += x" ./yourapp

When your app runs it will run all benchmark normally, but when it encounters a benchmarked named ``x += x``, it will run this one endlessly.
It will print in nice friendly letters 

.. code-block:: text

   NANOBENCH_ENDLESS set: running 'x += x' endlessly
   
once it reaches that state.


.. warning::

    For optimal profiling with ``perf``, you shouldn't use ``pyperf system tune`` in the endless mode. PyPerf dramatically reduces the
    number of events that can be captured per second. This is a good to get accurate benchmark numbers from nanobench, but a bad when
    you actually want to use perf to analyze hotspots.



``NANOBENCH_SUPPRESS_WARNINGS`` - No Stability Warnings
-------------------------------------------------------

In environments where it is clear that the results will not be stable, e.g. in CI where benchmarks are merely run to check if they don't cause a crash,
the environment variable ``NANOBENCH_SUPPRESS_WARNINGS`` can be used to suppress any warnings. This includes the header warnings like for frequency scaling,
and the ``:wavy_dash:`` warnings for the individual tests.

Set ``NANOBENCH_SUPPRESS_WARNINGS=1`` to disable all warnings, or set it to 0 to enable warnings (the default mode).

.. code-block:: sh

   NANOBENCH_SUPPRESS_WARNINGS=1 ./yourapp


``NANOBENCH_CONFIG`` - Change Settings Without Recompiling
----------------------------------------------------------

Trying a benchmark with more epochs, or with longer ones, usually means editing the source and building it again.
``NANOBENCH_CONFIG`` changes the defaults for every benchmark in the process instead:

.. code-block:: sh

   NANOBENCH_CONFIG="epochs=51,minEpochTime=5ms" ./yourapp

It holds comma separated ``key=value`` pairs. Whitespace around a key or a value is ignored, and so is an empty entry, so a
trailing comma is harmless. The keys are the :cpp:class:`Bench <ankerl::nanobench::Bench>` setter names and are case sensitive:

.. list-table::
   :header-rows: 1
   :widths: 30 15 55

   * - Key
     - Value
     - Changes
   * - ``epochs``
     - count
     - :cpp:func:`Bench::epochs <ankerl::nanobench::Bench::epochs>`
   * - ``warmup``
     - count
     - :cpp:func:`Bench::warmup <ankerl::nanobench::Bench::warmup>`
   * - ``minEpochIterations``
     - count
     - :cpp:func:`Bench::minEpochIterations <ankerl::nanobench::Bench::minEpochIterations>`
   * - ``epochIterations``
     - count
     - :cpp:func:`Bench::epochIterations <ankerl::nanobench::Bench::epochIterations>`
   * - ``clockResolutionMultiple``
     - count
     - :cpp:func:`Bench::clockResolutionMultiple <ankerl::nanobench::Bench::clockResolutionMultiple>`
   * - ``minEpochTime``
     - duration
     - :cpp:func:`Bench::minEpochTime <ankerl::nanobench::Bench::minEpochTime>`
   * - ``maxEpochTime``
     - duration
     - :cpp:func:`Bench::maxEpochTime <ankerl::nanobench::Bench::maxEpochTime>`

Each entry is applied by calling the setter its key names, so anything that setter enforces holds for a value that arrived from the
environment too - ``minEpochIterations=0`` becomes 1, exactly as :cpp:func:`Bench::minEpochIterations <ankerl::nanobench::Bench::minEpochIterations>` does.

A **count** is a whole number. A **duration** is a number followed by one of ``ns``, ``us``, ``ms`` or ``s``, and the number may be
fractional - so a duration can be written at whatever scale reads best:

.. code-block:: sh

   NANOBENCH_CONFIG="maxEpochTime=1.5s"     # the same as 1500ms
   NANOBENCH_CONFIG="minEpochTime=0.5ms"    # the same as 500us

The unit is not optional. A bare ``minEpochTime=5`` is rejected rather than taken as 5 nanoseconds, because a number without a unit
reads as whatever the person writing it had in mind.

Whatever a benchmark sets explicitly still wins - the environment variable moves the default, it does not overrule a benchmark that
needs a particular setting to mean anything:

.. code-block:: cpp

   // 2ms, whatever NANOBENCH_CONFIG says
   ankerl::nanobench::Bench().minEpochTime(std::chrono::milliseconds(2)).run("x += x", [&] {
       x += x;
   });

An unknown key, or a value that does not parse, prints one line on ``stderr`` and is ignored; the other entries still apply. Nothing
is thrown, so a typo cannot bring down a run:

.. code-block:: text

   NANOBENCH_CONFIG: unknown key 'epoch' - valid keys are clockResolutionMultiple, epochIterations, epochs, maxEpochTime, minEpochIterations, minEpochTime, warmup
   NANOBENCH_CONFIG: 'minEpochTime=5' is missing a time unit - use ns, us, ms or s

