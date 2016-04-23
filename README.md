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

- passes the tests as well as seems to work normally on x86_64 as well as ARM64.

- minimalism: lmdb consists of just two implementation files and two headers. These are currently hacked
  into the build system.

  - Also fixes #7466 (out-of-tree builds) in one go.

- lmdb database is stored in `chainstate2` and `index2` directories as to not overlap with leveldb
  databases.

- initial results show that it is faster in some cases but slower in other - more research is needed
  into when and why - see performance results below.

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
(either read or write) in progress, and the error will happen during a batch write.

This means that either

- write batches need to be restartable, or that 
- batches must give an estimate of their increase of the database

The latter could be risky if underestimation happens.

### crashes

It is unknown how well lmdb copes with sudden crashes and power loss.
Leveldb handles these fairly well in most cases, at least with the current patching
for windows.

### integrity

leveldb adds a CRC32 to each key and value, lmdb doesn't.
We could do this (or something similar, some fast hash) manually to detect corruption.

### 64-bit

Only 64-bit. Sorry, life is not fair.

performance results
--------------------

### x86_64

Gregory Maxwell did a few benchmarks with this code on a fast x86_64 machine (thanks!).

Initial verification at startup:
```
LevelDB:
2016-04-09 10:20:20 LoadBlockIndexDB: hashBestChain=000000000000000000b128ced925df0b8f7c12162927fc735ec34e03f26a7e22 height=406435 date=2016-04-09 10:09:36 progress=0.999995
2016-04-09 10:20:20 init message: Verifying blocks...
2016-04-09 10:20:20 Verifying last 288 blocks at level 3
2016-04-09 10:21:26 No coin database inconsistencies in last 289 blocks (431683 transactions)
2016-04-09 10:21:27  block index           70665ms

LMDB:
2016-04-09 10:17:18 LoadBlockIndexDB: hashBestChain=000000000000000000b128ced925df0b8f7c12162927fc735ec34e03f26a7e22 height=406435 date=2016-04-09 10:09:36 progress=0.999997
2016-04-09 10:17:18 init message: Verifying blocks...
2016-04-09 10:17:18 Verifying last 288 blocks at level 3
2016-04-09 10:17:28 No coin database inconsistencies in last 289 blocks (431683 transactions)
2016-04-09 10:17:29  block index           15445ms
```
About a 4.6x speedup.

Sync performance:

- 5 hours, 5 minutes for LMDB with default dbcache.
- 8 hours, 27 minutes for LevelDB with default dbcache.

Something like 40% faster.

Jonasschnelli's reindex benchmark (SSD, 4-core, non-VM):

    ##LMDB / -reindex (default dbcache)
    jonasschnelli@bitcoinsrv:~$ cat ~/.bitcoinlmdb/debug.log | grep 'v0.12.99\|progress=1'
    2016-04-13 19:58:08 Bitcoin version v0.12.99.0-f1d92bd (2016-04-11 11:22:13 +0200)
    2016-04-14 02:40:40 UpdateTip: new best=0000000000000000046f0b14fa01c286f8f77294243d2eec8393f35d8fb679a8 height=407191 version=0x00000004 log2_work=84.481762 tx=122265305 
    ==== RESULT: 6.7088888889 h

    ##LEVELDB / -reindex (default dbcache)
    jonasschnelli@bitcoinsrv:~$ cat ~/.bitcoin/debug.log | grep 'v0.12.99\|height=407191'
    2016-04-14 07:20:21 Bitcoin version v0.12.99.0-934f2b5 (2016-04-11 12:57:31 +0200)
    2016-04-14 12:33:47 UpdateTip: new best=0000000000000000046f0b14fa01c286f8f77294243d2eec8393f35d8fb679a8 height=407191 version=0x00000004 log2_work=84.481762 tx=122265305 date='2016-04-14 02:39:54' progress=0.999734 cache=8.0MiB(5917tx)
    ==== RESULT: 5.2238888889 h

leveldb was 1.2842709773× faster.

### Potential causes of slowness

- Profiling shows bitcoind with lmdb spends a lot of time in `fdatasync`. As a sync if done after every write transaction,
  and lmdb databases (unlike leveldb databases) are a single r/w file, everything blocks on this.

  - `fdatasync` seems especially slow in VMs, hanging the whole system for the duration of the operation.

Pieter Wuille's theory:
```
<sipa> here is a theory: lmdb uses mmap, and we use bulk writes often. this means that the OS can do a better job of scheduling the necessary writes to disk than the single-threaded, linear, synchronous approach used by leveldb
<sipa> if your latency for disk writes is low, that perhaps means that the benefit from lmdb is smaller
<sipa> as on an ssd a random write isnas fast as a linear
```

todo
------

- Compare perfomance in various other ways on at least ARM64 and x86_64.
- Reliability checks.
- See mapsize concerns above.
