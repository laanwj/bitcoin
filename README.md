SHA256 performance experiment
=====================================

what is this
-------------

Profiling has shown that a non-trivial amount of time in bitcoind is spent in computing SHA256 hashes,
especially `sha256::Transform`. Speeding up this function should result in an overall performance gain.

Intel has published[1](#ref1) optimized assembler variants of SHA256 for various of their extension
instruction sets, namely SSE4, AVR, and RORX.
Even more recent Intel CPUs have SHA extensions[2](#ref2), which add specific instructions for hash computation,
but these are not considered here. 

In this experiment this assembly code was integrated into the Bitcoin Core
source tree to measure performance.

usage
------

```bash
apt-get install yasm
./configure CPPFLAGS="-DBENCH_RORX" # leave out the CPPFLAGS to skip RORX benchmark
make -j10 src/bench/bench_bitcoin
src/bench/bench_bitcoin
```

results
---------

### AMD FX-8370 Eight-Core Processor @ 4.00Ghz

Impl         | avg        | speed%
------------ | ---------- | ---------
basic |0.0044496   | 100
sse4  |0.00388814  | 114
avx   |0.00637428  | 70

On this CPU, it turns out that the AVX variant is slowest, even slower than the C implementation.
The SSE4 variant is about 14% faster than the C implementation on average.
RORX instructions are not supported.

It will be interesting to see how this performs on Intel chips.

### Intel(R) Xeon(R) CPU E3-1275 v5 @ 3.60GHz

Impl         | avg        | speed%
------------ | ---------- | -------
basic |0.00383578  | 100
sse4  |0.0025789   | 149
avx   |0.0025782   | 149

The SSE4 variant is about 49% faster than the C implementation on this CPU.
AVX result is no different from the SSE4 implementation within any realistic
error bound.

### Intel(R) Core(TM) i7-4800MQ @ 2.6 GHz

Impl         | avg        | speed%
------------ | ---------- | -------
basic |0.00608469  | 100
sse4  |0.00397209  | 153
avx   |0.00398497  | 153

Here, too, AVX and SSE4 are virtually the same speed, however the SSE4 variant
is 53% faster than the C implementation.

### Haswell based Xeon

Impl         | avg        | speed%
------------ | ---------- | --------
basic |0.00532052   | 100
sse4  |0.003443     | 155
avx   |0.0034525    | 154
rorx  |0.002903     | 183
rorx_x8ms|0.0028543 | 186

### Broadwell-ep Xeon

Impl         | avg        | speed%
------------ | ---------- | --------
basic |0.00599851   | 100
sse4  |0.00396052   | 151
avx   |0.00397483   | 151
rorx  |0.00334802   | 179
rorx_x8ms | 0.00328667  | 183

references
-----------

1. <a id="ref1"/> Fast SHA-256 Implementations on Intel® Architecture Processors [paper](https://www-ssl.intel.com/content/www/us/en/intelligent-systems/intel-technology/sha-256-implementations-paper.html) and
  [implementation](http://downloadmirror.intel.com/22357/eng/sha256_code_release_v2.zip).
2. <a id="ref2"/> [Intel SHA extensions](https://software.intel.com/en-us/articles/intel-sha-extensions): New Instructions Supporting the Secure Hash Algorithm on Intel® Architecture Processors.
