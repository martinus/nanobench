* `{{whatever}}` will be replaced by the content of the identifier.
* `{{#benchmarks}}` opens a section, that will be rendered once for each benchmark. The section is closed by `{{/benchmarks}}`.
* `{{#results}}` opens a section for each result (epoch) within a benchmark.
* Within each section, you can use `{{#-last}}whatever{{/-last}}` to print `whatever` only for the last entry, `{{#-first}}` for only the first, `{{^-last}}` for anything *but* the last, `{{^-first}}` for anything *but* the first. In the JSON example, this is used to add a comma `, ` after each result except the last one.
