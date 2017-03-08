// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_DB_H
#define BITCOIN_WALLET_DB_H

#include "clientversion.h"
#include "serialize.h"
#include "streams.h"
#include "sync.h"
#include "version.h"

#include <map>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>

/** An instance of this class represents one database.
 * For BerkeleyDB this is just a (env, strFile) tuple.
 **/
class CWalletDBWrapper
{
    friend class CDB;
public:
    /** Create dummy DB handle */
    CWalletDBWrapper()
    {
    }

    /** Rewrite the entire database on disk, with the exception of key pszSkip if non-zero
     */
    bool Rewrite(const char* pszSkip=nullptr);

    /** Back up the entire database to a file.
     */
    bool Backup(const std::string& strDest);

    /** Get a name for this database, for debugging etc.
     */
    std::string GetName() const { return strFile; }

    /** Make sure all changes are flushed to disk.
     */
    void Flush(bool shutdown);

private:
    std::string strFile;
};

class Dbc
{
public:
    void close();
};

class CDB
{
public:
    explicit CDB(CWalletDBWrapper &dbw, const char* pszMode = "r+", bool fFlushOnCloseIn=true) {}
    ~CDB() { }

    template <typename K, typename T>
    bool Read(const K& key, T& value)
    {
        return false;
    }

    template <typename K, typename T>
    bool Write(const K& key, const T& value, bool fOverwrite = true)
    {
        return true;
    }

    template <typename K>
    bool Erase(const K& key)
    {
        return false;
    }

    template <typename K>
    bool Exists(const K& key)
    {
        return false;
    }

    Dbc* GetCursor()
    {
        return NULL;
    }

    int ReadAtCursor(Dbc* pcursor, CDataStream& ssKey, CDataStream& ssValue, bool setRange = false)
    {
        return 0;
    }

    bool TxnBegin()
    {
        return true;
    }

    bool TxnCommit()
    {
        return true;
    }

    bool TxnAbort()
    {
        return true;
    }

    bool ReadVersion(int& nVersion)
    {
        nVersion = 0;
        return true;
    }

    bool WriteVersion(int nVersion)
    {
        return true;
    }
};

#endif // BITCOIN_WALLET_DB_H
