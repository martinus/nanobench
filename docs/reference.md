<a id="top"></a>
# Reference

<!--ts-->
   * [Reference](#reference)
   * [Configuring Benchmarks](#configuring-benchmarks)
<!--te-->

# ankerl::nanobench::Config

All configuration and running the benchmarks is done with `ankerl::nanobench::Config`.

## Controlling Output

The following setters only have an effect to how data is printed on the screen:

| **Configuration** | **Effect** |
|---|---|
| `batch` | Set the batch size, e.g. number of processed bytes, or some other metric for the size of the processed data in each iteration. Best used in combination with `unit`. Any argument is cast to double. |
| `relative` | Marks the next run as the baseline. The following runs will be compared to this run. 100% will mean it is exactly as fast as the baseline, >100% means it is faster than the baseline. It is calculated by `100% * runtime_baseline / runtime`. So e.g. 200% means the current run is twice as fast as the baseline. |
| `unit` | Operation unit. Defaults to "op", could be e.g. "byte" for string processing. This is used for the table header, e.g. to show `ns/byte`. Use singular (byte, not bytes). |
| `title` | Title of the benchmark, will be shown in the table header. |
| `output` | Set the output stream where the resulting markdown table will be printed to. The default is `&std::cout`. You can disable all output by setting `nullptr`.

## Controlling Measurement

These setters control the measurment process. The default configuration should be quite good for most usecases, but there are lots of ways to configure it for the best results.

| **Configuration** | **Effect** |
|---|---|
| `epochs` | Number of epochs to evaluate. The reported result will be the median of evaluation of each epoch. Defaults to 51. The higher you choose this, the more deterministic will the result be and outliers will be more easily removed. The default is already quite high to be able to filter most outliers. For slow benchmarks you might want to reduce this number. |
| `clockResolutionMultiple` | Modern processors have a very accurate clock, being able to measure as low as 20 nanoseconds. This allows nanobech to be so fast: we only run the benchmark sufficiently often so that the clock's accuracy is good enough. The default is to run one epoch for 2000 times the clock resolution. So for 20ns resolution and 51 epochs, this gives a total runtime of `20ns * 2000 * 51 ~ 2ms` for a benchmark to get accurate results. |
| `maxEpochTime` | As a safety precausion if the clock is not very accurate, we can set an upper limit for the maximum evaluation time per epoch. Default is 100ms. |
| `minEpochTime` | Sets the minimum time each epoch should take. Default is zero, so clockResolutionMultiple() can do it's best guess. You can increase this if you have the time and results are not accurate enough. |
| `minEpochIterations` | Sets the minimum number of iterations each epoch should take. Default is 1. For high median average percentage error (MdAPE), which happens when your benchmark is unstable, you might want to increase the minimum number to get more accurate reslts. |
| `warmup` | Set a number of iterations that are initially performed without any measurements, to warmup caches / database / whatever. Normally this is not needed, since we show the median result so initial outliers will be filtered away automatically. |

## Running Benchmarks

After you've done the configuration, you `run` one or more benchmarks with these settings. E.g:

```cpp
uint64_t x = 1;
ankerl::nanobench::Config().run("x += x", [&] {
    x += x;
}).doNotOptimizeAway(x);
```

> **_NOTE:_** Each call to your lambda must have a side effect that the compiler can't possibly optimize it away. E.g. add a result to an externally defined number (like `x` in the above example), and finally call `doNotOptimizeAway` on the variables the compiler must not remove. You can also use `ankerl::nanobench::doNotOptimize(...)` directly in the lambda, but be aware that this has a small overhead.

## Processing Results

The `Config` class comes with a powerful [mustache](https://mustache.github.io/)-like template mechanism to process the benchmark results into all kinds of formats. After all benchmarks have been run, you can e.g. create a nice boxplot of all results with:

```cpp
std::ofstream fout("example_random_number_generators.json");
cfg.render(ankerl::nanobench::templates::json(), fout);
```

![html boxplot example](docs/htmlBoxplot_example.png)

Namespace `ankerl::nanobench::templates` comes with several predefined templates:

| **template** | **Result** |
|---|---|
| `csv` | Produces comma-separated value (CSV) content |
| `json` | All available data will be generated into one JSON file. Use this as an example for your own templates. |
| `htmlBoxplot` | Generates a HTML page that uses [plotly.js](https://plot.ly/javascript/) with a boxplot graph of all the results. This gives a very nice visual representation of all the data |