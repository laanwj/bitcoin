SHA256 performance experiment
=====================================

what is this
-------------

Profiling has shown that a non-trivial amount of time in bitcoind is spent in computing SHA256 hashes,
especially `sha256::Transform`. Speeding up this function should result in an overall performance gain.

Intel has [published][intel] optimized assembler variants of SHA256 for various of their extension
instruction sets, namely SSE4, AVR, and RORX.
Even more recent Intel CPUs have [SHA extensions][sha]. 

In this experiment this assembly code was integrated into the Bitcoin Core
source tree to measure performance.

usage
------

    apt-get install yasm
    ./configure CPPFLAGS="-DBENCH_RORX" # leave out the CPPFLAGS to skip RORX benchmark
    make -j10 src/bench/bench_bitcoin
    src/bench/bench_bitcoin

results
---------

### AMD FX-8370 Eight-Core Processor

Impl         | min      | max      | avg
------------ | -------- | -------- | ----------
SHA256_avx   |0.00635675|0.00653851|0.00637428
SHA256_basic |0.00444281|0.00449353|0.0044496
SHA256_sse4  |0.0038569 |0.00395656|0.00388814

On this CPU, it turns out that the AVX variant is slowest, even slower than the C implementation.
The SSE4 variant is about 13% faster on average.
RORX instructions are not supported.

It will be interesting to see how this performs on Intel chips.

references
-----------

- [intel]: Fast SHA-256 Implementations on Intel® Architecture Processors [paper](https://www-ssl.intel.com/content/www/us/en/intelligent-systems/intel-technology/sha-256-implementations-paper.html) and
  [implementation](http://downloadmirror.intel.com/22357/eng/sha256_code_release_v2.zip).
- [intelsha]: [Intel SHA extensions](https://software.intel.com/en-us/articles/intel-sha-extensions).
