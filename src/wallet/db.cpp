// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "db.h"

#include "util.h"
#include "utilstrencodings.h"

#include <stdint.h>

bool CWalletDBWrapper::Rewrite(const char* pszSkip)
{
    return false;
}

bool CWalletDBWrapper::Backup(const std::string& strDest)
{
    return false;
}

void CWalletDBWrapper::Flush(bool shutdown)
{
}
