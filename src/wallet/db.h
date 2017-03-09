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
#include "util.h"

#include <map>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <utilstrencodings.h>

static const size_t WDBWRAPPER_PREALLOC_KEY_SIZE = 64;
static const size_t WDBWRAPPER_PREALLOC_VALUE_SIZE = 1024;

/** Error code emulated from BDB, returned by ReadAtCursor */
enum
{
    DB_NOTFOUND = -1,
};

/** CDB fatal error */
class CDBError: public std::runtime_error
{
public:
    CDBError(const std::string& msg) : std::runtime_error(msg) {}
};

/** An instance of this class represents one database.
 * For BerkeleyDB this is just a (env, strFile) tuple.
 **/
class CWalletDBWrapper
{
    friend class CDB;
private:
    //! database path
    const boost::filesystem::path path;
    //
    //! custom environment this database is using (may be NULL in case of default environment)
    leveldb::Env* penv;

    //! database options used
    leveldb::Options options;

    //! options used when reading from the database
    leveldb::ReadOptions readoptions;

    //! options used when iterating over values of the database
    leveldb::ReadOptions iteroptions;

    //! options used when writing to the database
    leveldb::WriteOptions writeoptions;

    //! options used when sync writing to the database
    leveldb::WriteOptions syncoptions;

    //! the database itself
    leveldb::DB* pdb;

public:
    /** Create dummy DB handle */
    CWalletDBWrapper();

    /** Create real DB handle */
    CWalletDBWrapper(const boost::filesystem::path& path, size_t nCacheSize);
    ~CWalletDBWrapper();

    /** Rewrite the entire database on disk, with the exception of key pszSkip if non-zero
     */
    bool Rewrite(const char* pszSkip=nullptr);

    /** Back up the entire database to a file.
     */
    bool Backup(const std::string& strDest);

    /** Get a name for this database, for debugging etc.
     */
    std::string GetName() const { return path.string(); }

    /** Make sure all changes are flushed to disk.
     */
    void Flush(bool shutdown);
};

class Dbc
{
    friend class CDB;
public:
    Dbc(leveldb::Iterator *piter_in): piter(piter_in) {}

    void close();
private:
    leveldb::Iterator *piter;
};

class CDB
{
public:
    explicit CDB(CWalletDBWrapper &dbw, const char* pszMode = "r+", bool fFlushOnCloseIn=true);
    ~CDB();

    // Reading
    template <typename K, typename T>
    bool Read(const K& key, T& value)
    {
        assert(!hasChanges); // Doesn't handle the case where the batch has changes in it
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(WDBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        leveldb::Slice slKey(ssKey.data(), ssKey.size());
#ifdef LEVELDB_DEBUG
        LogPrintf("Reading key %s\n", HexStr(ssKey.data(), ssKey.data()+ssKey.size()));
#endif

        std::string strValue;
        leveldb::Status status = dbw.pdb->Get(dbw.readoptions, slKey, &strValue);
        if (!status.ok()) {
            if (status.IsNotFound()) {
#ifdef LEVELDB_DEBUG
                LogPrintf("Not found key %s\n", HexStr(ssKey.data(), ssKey.data()+ssKey.size()));
#endif
                return false;
            }
            throw CDBError("Error reading wallet LevelDB: " + status.ToString());
        }
#ifdef LEVELDB_DEBUG
        LogPrintf(" -> value %s\n", HexStr(strValue.data(), strValue.data()+strValue.size()));
#endif
        try {
            CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
            ssValue >> value;
        } catch (const std::exception&) {
            return false;
        }
        return true;
    }

    template <typename K>
    bool Exists(const K& key)
    {
        assert(!hasChanges); // Doesn't handle the case where the batch has changes in it
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(WDBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        leveldb::Slice slKey(ssKey.data(), ssKey.size());

        std::string strValue;
        leveldb::Status status = dbw.pdb->Get(dbw.readoptions, slKey, &strValue);
        if (!status.ok()) {
            if (status.IsNotFound()) {
                return false;
            }
            throw CDBError("Error checking for existence in wallet LevelDB: " + status.ToString());
        }
        return true;
    }

    Dbc* GetCursor();
    int ReadAtCursor(Dbc* pcursor, CDataStream& ssKey, CDataStream& ssValue, bool setRange = false);

    // Writing
    template <typename K, typename T>
    bool Write(const K& key, const T& value, bool fOverwrite = true)
    {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(WDBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        leveldb::Slice slKey(ssKey.data(), ssKey.size());

        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        ssValue.reserve(WDBWRAPPER_PREALLOC_VALUE_SIZE);
        ssValue << value;
        leveldb::Slice slValue(ssValue.data(), ssValue.size());
#ifdef LEVELDB_DEBUG
        LogPrintf("Writing %s %s\n",
                HexStr(ssKey.data(), ssKey.data()+ssKey.size()),
                HexStr(ssValue.data(), ssValue.data()+ssValue.size()));
#endif

        batch.Put(slKey, slValue);
        hasChanges = true;
        return true;
    }

    template <typename K>
    bool Erase(const K& key)
    {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(WDBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        leveldb::Slice slKey(ssKey.data(), ssKey.size());
#ifdef LEVELDB_DEBUG
        LogPrintf("Erasing %s\n", 
                HexStr(ssKey.data(), ssKey.data()+ssKey.size()));
#endif

        batch.Delete(slKey);
        hasChanges = true;
        return true;
    }

    // Transaction API
    bool TxnBegin()
    {
        // Nothing to do here
        return true;
    }

    bool TxnCommit()
    {
        // Nothing to do, this is the default
        return true;
    }

    bool TxnAbort()
    {
        aborted = true;
        return true;
    }

    bool ReadVersion(int& nVersion)
    {
        nVersion = 0;
        return Read(std::string("version"), nVersion);
    }

    bool WriteVersion(int nVersion)
    {
        return Write(std::string("version"), nVersion);
    }
private:
    CWalletDBWrapper &dbw;
    leveldb::WriteBatch batch;
    bool hasChanges;
    bool aborted;
    bool flushOnClose;
};

#endif // BITCOIN_WALLET_DB_H
