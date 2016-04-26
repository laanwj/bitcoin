Dummy database experiment
=====================================

disclaimer
----------

This is part of a blue-skies experiment, it is by no means recommended as a
replacement for the current leveldb usage.

Not only is this software PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND as
specified in `COPYING`, this software has *negative* warranty. If you manage to
suffer financial or other losses due to this code I demand compensation for
feelings of misplaced guilt.

what is this?
-------------

This replaces the block index and UTXO database with an in-memory data structure
which is read from disk at start, and written to disk at shutdown. There are no
intermediate flushes.

what is this for?
-----------------

Accomplish the same as `-dbcache=8000` (e.g. do everything in memory) but with
less memory usage. The backend database stores the coins data in serialized
format just like an actual database would.

This is also useful for benchmarking and profiling, as it answers the question
what performance could be achieved with the minimum possible database overhead
and latency: no I/O at all except at startup and shutdown.

This is more realistic than using a huge dbcache because this does measure
serialization overhead.

some results
-------------

to do
------

- Memory usage could potentially be reduced even further by using a different map implementation.
  There are a few mentioned here: https://stackoverflow.com/questions/3300525/super-high-performance-c-c-hash-map-table-dictionary
  For example, Google sparsehash outperformed all the competing maps in memory usage at some point.

