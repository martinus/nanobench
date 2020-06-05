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

#. Compile & link with ``g++ -O3 -DNDEBUG -I../include nanobench.o full_example.cpp -o full_example``. This takes just 0.5 seconds on my machine.

#. Run ``./full_example``, which gives an output like this:

   .. code:: txt

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

Something Fast
==============

Let’s benchmarks how fast we can do ``x += x`` for ``uint64_t``:

.. literalinclude:: ../src/test/tutorial_fast_v1.cpp
   :language: c++
   :linenos:
   :caption: tutorial_fast_v1.cpp

After 0.2ms we get this output:

.. code-block:: txt

   |               ns/op |                op/s |   MdAPE |         ins/op |         cyc/op |    IPC |      branch/op | missed% | benchmark
   |--------------------:|--------------------:|--------:|---------------:|---------------:|-------:|---------------:|--------:|:----------------------------------------------
   |                   - |                   - |       - |              - |              - |      - |              - |       - | :boom: iterations overflow. Maybe your code got optimized away? `x += x`

The compiler could optimize ``x += x`` away because we never used the output. Let's fix this:

.. literalinclude:: ../src/test/tutorial_fast_v2.cpp
   :language: c++
   :linenos:
   :caption: tutorial_fast_v2.cpp

This time the benchmark runs for 2.2ms and gives us a good result:

.. code-block:: txt

   |               ns/op |                op/s |   MdAPE |         ins/op |         cyc/op |    IPC |      branch/op | missed% | benchmark
   |--------------------:|--------------------:|--------:|---------------:|---------------:|-------:|---------------:|--------:|:----------------------------------------------
   |                0.32 |    3,170,869,554.81 |    0.2% |           1.00 |           1.01 |  0.993 |           0.00 |    0.0% | `x += x`

It's a very stable result. One run the op/s is 3,170 million/sec, the next time I execute it I get 3,168 million/sec. It always takes 
1.00 instructions per operation on my machine, and can do this in ~1 cycle.


Something Slow
==============

Let's benchmark if sleeping for 100ms really takes 100ms.

.. literalinclude:: ../src/test/tutorial_slow_v1.cpp
   :language: c++
   :linenos:
   :caption: tutorial_slow_v1.cpp

After 1.1 seconds I get

.. code-block:: txt

   |               ns/op |                op/s |    err% |          ins/op |          cyc/op |    IPC |         bra/op |   miss% |     total | benchmark
   |--------------------:|--------------------:|--------:|----------------:|----------------:|-------:|---------------:|--------:|----------:|:---------------------
   |      100,125,753.00 |                9.99 |    0.0% |           51.00 |        7,714.00 |  0.007 |          11.00 |   90.9% |      1.10 | `sleep 100ms, auto`


So we actually take 100.125ms instead of 100ms. Next time I run it, I get 100.141. Also a very stable result. Interestingly, sleep takes 51 instructions but 7,714 cycles - so we only got 0.007 instructions per cycle. That's extremely low, but expected of ``sleep``. It also required 11 branches, of which 90.9% were misspredicted on average.

If the extremely slow 1.1 second is too much for you, you can manually configure the number of evaluations (epochs):

.. literalinclude:: ../src/test/tutorial_slow_v2.cpp
   :language: c++
   :linenos:
   :caption: tutorial_slow_v2.cpp

.. code-block:: txt

   |               ns/op |                op/s |    err% |          ins/op |          cyc/op |    IPC |         bra/op |   miss% |     total | benchmark
   |--------------------:|--------------------:|--------:|----------------:|----------------:|-------:|---------------:|--------:|----------:|:----------
   |      100,099,096.00 |                9.99 |    0.0% |           51.00 |        7,182.00 |  0.007 |          11.00 |   90.9% |      0.30 | `sleep 100ms`

This time it took only 0.3 seconds, but with only 3 evaluations instead of 11. The err% will be less meaningfull, but since the benchmark is so stable it doesn't really matter.
