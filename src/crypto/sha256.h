// Copyright (c) 2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_SHA256_H
#define BITCOIN_CRYPTO_SHA256_H

#include <stdint.h>
#include <stdlib.h>

/** A hasher class for SHA-256. */
class CSHA256
{
private:
    uint32_t s[8];
    unsigned char buf[64];
    uint64_t bytes;

public:
    static const size_t OUTPUT_SIZE = 32;

    CSHA256();
    CSHA256& Write(const unsigned char* data, size_t len);
    void Finalize(unsigned char hash[OUTPUT_SIZE]);
    CSHA256& Reset();

    enum class Impl {
        BASIC,    //!< Basic C-based implementation
        SSE4,     //!< SSE4 assembly
        AVX,      //!< AVX instructions
        RORX,     //!< RORX instruction
        RORX_X8MS //!< RORX 64-bit wide
    };

    //! Globally set SHA256 implementation
    static void SetImplementation(Impl i);
};

#endif // BITCOIN_CRYPTO_SHA256_H
