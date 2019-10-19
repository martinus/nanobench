<a id="top"></a>
# Reference

<!--ts-->
   * [Reference](#reference)
   * [Configuring Benchmarks](#configuring-benchmarks)
<!--te-->

# Configuring Benchmarks

All configuration is done with `ankerl::nanobench::Config`. The default configuration should be quite good for most usecases, but there are lots of ways to configure it for the best results.

* `batch` Set the batch size, e.g. number of processed bytes, or some other metric for the size of the processed data in each iteration. Best used in combination with `unit`. Any argument is cast to double.
* `relative` Marks the next run as the baseline. The following runs will be compared to this run. 100% will mean it is exactly as fast as the baseline, >100% means it is faster than the baseline. It is calculated by `100% * runtime_baseline / runtime`. So e.g. 200% means the current run is twice as fast as the baseline.
* `unit` Operation unit. Defaults to "op", could be e.g. "byte" for string processing. This is used for the table header, e.g. to show `ns/byte`. Use singular (byte, not bytes).