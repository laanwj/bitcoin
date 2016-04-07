Bitcoin Core LMDB experiment
==============================

disclaimer
-----------

This is part of a blue-skies experiment, it is by no means recommended as a
replacement for the current leveldb usage.

Not only is this software PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND as
specified in `COPYING`, this software has *negative* warranty. If you manage to
suffer financial or other losses due to this code I demand compensation for
feelings of misplaced guilt.

conversion
-----------

A script `leveldb2lmdb.py` to convert from leveldb to lmdb format can be found here:
https://github.com/laanwj/blockdb-troubleshoot

    git clone https://github.com/laanwj/blockdb-troubleshoot
    cd blockdb-troubleshoot
    sudo apt-get install python3-setuptools python3-dev
    ./compile_leveldb.sh
    ./comile_lmdb.sh
    ./leveldb2lmdb.py ${DATADIR}/chainstate ${DATADIR}/chainstate2 $((1024*1024*1024*8))
    ./leveldb2lmdb.py ${DATADIR}/blocks/index ${DATADIR}/blocks/index2 $((1024*1024*1024*8)) 

(or just `-reindex` from scratch)
(there is no script to go the other way yet, but that'd be easy to write)

what is nice
--------------

- minimalism: lmdb consists of just two implementation files and two headers. These are currently hacked
  into the build system.

  - Also fixes #7466 (out-of-tree builds) in one go.

- lmdb database is stored in `chainstate2` and `index2` directories as to not overlap with leveldb
  databases.

issues
---------

Some (potential) issues are discussed below.

### mapsize

lmdb forces setting a 'map size' which is essentially the maximum size of the database.

Currently this is set to a fixed 8GB - which is both overkill - and could be too little at some point.
On OSes with sparse files only a sparse file will be created, but on OSes without them
the file will be that large.

In production it's not accepable to set a fixed number here.
It could be increased on demand by handling [MDB_MAP_FULL](http://symas.com/mdb/doc/group__errors.html#ga0a83370402a060c9175100d4bbfb9f25)
errors and increasing with [mdb_env_set_mapsize](http://symas.com/mdb/doc/group__mdb.html#gaa2506ec8dab3d969b0e609cd82e619e5).
However this is tricky: changing the map size is only possible when there are no transactions
(either read or write) in progress.

This means that write batches need to be restartable, or that batches must give an estimate
of their increase of the database. The latter could be risky if underestimation happens.

### crashes

It is unknown how well lmdb copes with sudden crashes and power loss.
Leveldb handles these fairly well in most cases, at least with the current patching
for windows.

### integrity

leveldb adds a CRC32 to each key and value, lmdb doesn't.
We could do this (or something similar, some fast hash) manually to detect corruption.

### 64-bit

Only 64-bit. Sorry, life is not fair.

todo
------

- Compare perfomance in various ways on at least ARM64 and x86_64.
