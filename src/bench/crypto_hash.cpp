// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <iostream>

#include "bench.h"
#include "bloom.h"
#include "hash.h"
#include "uint256.h"
#include "utiltime.h"
#include "crypto/ripemd160.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "crypto/sha512.h"

/* Number of bytes to hash per iteration */
static const uint64_t BUFFER_SIZE = 1000*1000;

// Uncomment to enable RORX benchmarking (only if CPU has support)
// #define BENCH_RORX

static void RIPEMD160(benchmark::State& state)
{
    uint8_t hash[CRIPEMD160::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    while (state.KeepRunning())
        CRIPEMD160().Write(in.data(), in.size()).Finalize(hash);
}

static void SHA1(benchmark::State& state)
{
    uint8_t hash[CSHA1::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    while (state.KeepRunning())
        CSHA1().Write(in.data(), in.size()).Finalize(hash);
}

static void SHA256_basic(benchmark::State& state)
{
    CSHA256::SetImplementation(CSHA256::Impl::BASIC);
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    while (state.KeepRunning())
        CSHA256().Write(in.data(), in.size()).Finalize(hash);
}

static void SHA256_sse4(benchmark::State& state)
{
    CSHA256::SetImplementation(CSHA256::Impl::SSE4);
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    while (state.KeepRunning())
        CSHA256().Write(in.data(), in.size()).Finalize(hash);
    CSHA256::SetImplementation(CSHA256::Impl::BASIC);
}

static void SHA256_avx(benchmark::State& state)
{
    CSHA256::SetImplementation(CSHA256::Impl::AVX);
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    while (state.KeepRunning())
        CSHA256().Write(in.data(), in.size()).Finalize(hash);
    CSHA256::SetImplementation(CSHA256::Impl::BASIC);
}

static void SHA256_rorx(benchmark::State& state)
{
    CSHA256::SetImplementation(CSHA256::Impl::RORX);
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    while (state.KeepRunning())
        CSHA256().Write(in.data(), in.size()).Finalize(hash);
    CSHA256::SetImplementation(CSHA256::Impl::BASIC);
}

static void SHA256_rorx_x8ms(benchmark::State& state)
{
    CSHA256::SetImplementation(CSHA256::Impl::RORX_X8MS);
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    while (state.KeepRunning())
        CSHA256().Write(in.data(), in.size()).Finalize(hash);
    CSHA256::SetImplementation(CSHA256::Impl::BASIC);
}

static void SHA256_32b(benchmark::State& state)
{
    std::vector<uint8_t> in(32,0);
    while (state.KeepRunning()) {
        for (int i = 0; i < 1000000; i++) {
            CSHA256().Write(in.data(), in.size()).Finalize(&in[0]);
        }
    }
}

static void SHA512(benchmark::State& state)
{
    uint8_t hash[CSHA512::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    while (state.KeepRunning())
        CSHA512().Write(in.data(), in.size()).Finalize(hash);
}

static void SipHash_32b(benchmark::State& state)
{
    uint256 x;
    while (state.KeepRunning()) {
        for (int i = 0; i < 1000000; i++) {
            *((uint64_t*)x.begin()) = SipHashUint256(0, i, x);
        }
    }
}

BENCHMARK(RIPEMD160);
BENCHMARK(SHA1);
BENCHMARK(SHA256_basic);
BENCHMARK(SHA256_sse4);
BENCHMARK(SHA256_avx);
#ifdef BENCH_RORX
BENCHMARK(SHA256_rorx);
BENCHMARK(SHA256_rorx_x8ms);
#endif
BENCHMARK(SHA512);

BENCHMARK(SHA256_32b);
BENCHMARK(SipHash_32b);
