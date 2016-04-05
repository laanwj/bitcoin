// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "chainparams.h"
#include "clientversion.h"
#include "init.h"
#include "util.h"
#include "utilstrencodings.h"
#include "txdb.h"
#include "hash.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/scoped_ptr.hpp>

#include <stdio.h>

static bool AppInit(int argc, char* argv[])
{
    ParseParameters(argc, argv);

    // Process help and version before taking care about datadir
    if (mapArgs.count("-?") || mapArgs.count("-h") ||  mapArgs.count("-help") || mapArgs.count("-version"))
    {
        fprintf(stdout, "No help available\n");
        return false;
    }

    try
    {
        if (!boost::filesystem::is_directory(GetDataDir(false)))
        {
            fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n", mapArgs["-datadir"].c_str());
            return false;
        }
        try
        {
            ReadConfigFile(mapArgs, mapMultiArgs);
        } catch (const std::exception& e) {
            fprintf(stderr,"Error reading configuration file: %s\n", e.what());
            return false;
        }
        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        try {
            SelectParams(ChainNameFromCommandLine());
        } catch (const std::exception& e) {
            fprintf(stderr, "Error: %s\n", e.what());
            return false;
        }
        return true;
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "AppInit()");
    } catch (...) {
        PrintExceptionContinue(NULL, "AppInit()");
    }
    return false;
}

int main(int argc, char* argv[])
{
    SetupEnvironment();

    if (!AppInit(argc, argv))
        return 1;

    CCoinsViewDB db(100*1024*1024, false, false);
    boost::scoped_ptr<CCoinsViewCursor> pcursor(db.Cursor());

    FILE *file = fopen("utxo.dat","wb");
    CAutoFile ss(file, SER_NETWORK, PROTOCOL_VERSION);
    ss << pcursor->GetBestBlock();
    printf("Best block is %s\n", pcursor->GetBestBlock().ToString().c_str());
    while (pcursor->Valid()) {
        uint256 key;
        CCoins coins;
        if (pcursor->GetKey(key) && pcursor->GetValue(coins)) {
            ss << key << coins;
        } else {
            printf("Error: unable to read value\n");
            exit(1);
        }
        pcursor->Next();
    }

    return 0;
}

