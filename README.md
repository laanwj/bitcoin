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

I've timed a few reindexes up to block 407838 on an 8-core AMD machine:

1) Using `-dbcache=8000`: total 03:10:44 (11444s)

2) Using dummydb, default `-dbcache=100`: total 04:26:06 (15966s)

3) Using leveldb, default `-dbcache=100`: total 09:04:34 (32674s)

So as expected, a full dbcache is fastest. This keeps all the unspent coin data
in memory in expanded form. The dummydb is somewhat slower: there is
serialization and deserialization overhead. Using on-disk leveldb is the
slowest option, due to I/O latency and database overhead.

other info
-----------

-  use`chainstate3` and `index3` directory names to avoid overlap with the leveldb or lmdb databases.

to do
------

- Memory usage could potentially be reduced even further by using a different map implementation.
  There are a few mentioned here: https://stackoverflow.com/questions/3300525/super-high-performance-c-c-hash-map-table-dictionary
  For example, Google sparsehash outperformed all the competing maps in memory usage at some point.

