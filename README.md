Laanwj's Bitcoin Core repository
==================================

In progress
-------------

### Security

- [2017_03_cabi_fs](https://github.com/laanwj/bitcoin/tree/2017_03_cabi_fs)
  Sandboxed CloudABI port.

### Hashing performance

- [2016_05_sha256_accel](https://github.com/laanwj/bitcoin/tree/2016_05_sha256_accel)
  experiment with optimized SHA256 variants. 

### Database experiments

- [2016_04_mdb](https://github.com/laanwj/bitcoin/tree/2016_04_mdb)
  LMDB experiment - change UTXO and block index DB to use LMDB (aka "Symas
  Lightning Memory-Mapped Database") instead of LevelDB.

- [2016_04_dummy_db](https://github.com/laanwj/bitcoin/tree/2016_04_dummy_db)
  Dummy database experiment - This replaces the block index and UTXO
  database with an in-memory data structure which is read from disk at start,
  and written to disk at shutdown. There are no intermediate flushes.

- [2016_04_leveldb_sse42_crc32c_test](https://github.com/laanwj/bitcoin/tree/2016_04_leveldb_sse42_crc32c_test)
  Use SSE4.2 CRC32C instructions in LevelDB. LevelDB uses this cyclic redundancy check for integrity
  verification. See also [crcbench](https://github.com/laanwj/crcbench), to see the difference
  in raw throughput.

### Statistics and notifications

- [zmq mempool notifications](https://github.com/bitcoin/bitcoin/pull/7753): Add notifications when transactions enter or leave the mempool.

- bc-monitor: ncurses (console) tool for monitoring a bitcoind instance (see
  discussion in [PR #7753](https://github.com/bitcoin/bitcoin/pull/7753)). To
  be released soon.

### HTTP streaming

This is still very unstable.

- Add a streaming API to the HTTP server. This allows streaming data to the
  client chunk by chunk, which is useful when not the entire data is available
  at once or it is huge and wouldn't fit (efficiently) in memory.

- Allows downloading the entire UTXO set through `/rest/utxoset`. This is a raw
  dump of all outputs, the state normally hashed by `gettxoutsetinfo`. The dump
  is performed in the background by making use of leveldb snapshotting, so
  without keeping cs_main locked.
    - This can be useful for analysis purposes if you don't want to mess with
      bitcoin core's database
    - Filename (via content-disposition) is
      `utxoset-<height>-<bestblockhash>.dat`. Also a custom `X-Best-Block` and
      `X-Block-Height` header is added.

See [PR #7759](https://github.com/bitcoin/bitcoin/pull/7759) or the branch
[2016_03_utxo_streaming](https://github.com/laanwj/bitcoin/tree/2016_03_utxo_streaming).

Ready for review
--------------------

- [2016_04_i18n_unicode_rpc](https://github.com/laanwj/bitcoin/tree/2016_04_i18n_unicode_rpc)
  add full UTF-8 support to RPC (and the underlying JSON implementation). See also
  [PR #7892](https://github.com/bitcoin/bitcoin/pull/7892) and upstream
  [PR #22](https://github.com/jgarzik/univalue/pull/22).

  - See also: [Univalue fuzzing branch](https://github.com/laanwj/univalue/tree/2015_11_unifuzz),
    and [instructions](https://gist.github.com/laanwj/68551528b7ae641ccaeb519566ca67c7) for building
    univalue with afl-fuzz.

Documents and notes
--------------------

- [Reducing bitcoind memory usage](https://gist.github.com/laanwj/efe29c7661ce9b6620a7).

Scripts
--------

- [Start bitcoind in a screen in a debugger](https://gist.github.com/laanwj/29bc141fb8d10608651c).
- [Measure compilation speed](https://gist.github.com/laanwj/108877a28ec03836568a) as well
 as other statistics such as maximum memory usage per compilation unit. Original
 [PR #6658](https://github.com/bitcoin/bitcoin/issues/6658#issuecomment-144643696).
- [git-show-merge](https://gist.github.com/laanwj/ef8a6dcbb02313442462): show
  in which pull request a certain commit was merged into the current branch.
- [blockdb-troubleshoot](https://github.com/laanwj/blockdb-troubleshoot):
  collected bitcoin block database troubleshooting tools (from Python).
- [bitcoin-submittx](https://github.com/laanwj/bitcoin-submittx):
  stand-alone Bitcoin transaction submission tool (in Python).
- [JSON-RPC batching example](https://gist.github.com/laanwj/f2e0238bd151d5365c07bdd03467588b).

