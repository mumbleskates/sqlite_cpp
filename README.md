# Ergonomic sqlite in C++17

These libraries include RAII for prepared statements that can have parameters
quickly bound by templates, can read results with the desired types and with
convenient range iteration of the rows, and can even provide output iterators
for tuple-like types.

Included is `sqlite_blocking`, a C++ implementation of the drop-in
concurrency-tolerant versions of `sqlite3_prepare_v2`, `sqlite3_step`, and
`sqlite3_exec` from the
[unlock_notify documentation](https://www.sqlite.org/unlock_notify.html).
`sqlite_cpp` is written to use these by default but it doesn't have to be.

`sqlite_cpp` is most useful in C++17 which has structured binding for unpacking
the returned tuples of rows that are read, and which already has the headers for
`<optional>` and `<string_view>` that don't need to be backfilled with
non-stdlib equivalents.
