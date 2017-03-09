// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "db.h"

#include "util.h"
#include "utilstrencodings.h"

#include <leveldb/cache.h>
#include <leveldb/env.h>
#include <leveldb/filter_policy.h>
#include <memenv.h>

#include <stdint.h>

static leveldb::Options GetOptions(size_t nCacheSize)
{
    leveldb::Options options;
    options.block_cache = leveldb::NewLRUCache(nCacheSize / 2);
    options.write_buffer_size = nCacheSize / 4; // up to two write buffers may be held in memory simultaneously
    options.filter_policy = leveldb::NewBloomFilterPolicy(10);
    options.compression = leveldb::kNoCompression;
    options.max_open_files = 64;
    if (leveldb::kMajorVersion > 1 || (leveldb::kMajorVersion == 1 && leveldb::kMinorVersion >= 16)) {
        // LevelDB versions before 1.16 consider short writes to be corruption. Only trigger error
        // on corruption in later versions.
        options.paranoid_checks = true;
    }
    return options;
}

CWalletDBWrapper::CWalletDBWrapper()
{
    penv = leveldb::NewMemEnv(leveldb::Env::Default());
    readoptions.verify_checksums = true;
    iteroptions.verify_checksums = true;
    iteroptions.fill_cache = false;
    syncoptions.sync = true;
    options = GetOptions(1024);
    options.create_if_missing = true;
    options.env = penv;
    leveldb::Status status = leveldb::DB::Open(options, path.string(), &pdb);
    if (!status.ok()) {
        throw CDBError("Error creating in-memory wallet LevelDB: " + status.ToString());
    }
}

CWalletDBWrapper::CWalletDBWrapper(const boost::filesystem::path& path_in, size_t nCacheSize):
    path(path_in)
{
    penv = NULL;
    readoptions.verify_checksums = true;
    iteroptions.verify_checksums = true;
    iteroptions.fill_cache = false;
    syncoptions.sync = true;
    options = GetOptions(nCacheSize);
    options.create_if_missing = true;
    TryCreateDirectory(path);
    LogPrintf("Opening wallet LevelDB in %s\n", path.string());
    leveldb::Status status = leveldb::DB::Open(options, path.string(), &pdb);
    if (!status.ok()) {
        throw CDBError("Error opening wallet LevelDB: " + status.ToString());
    }
    LogPrintf("Opened wallet LevelDB successfully\n");
}

CWalletDBWrapper::~CWalletDBWrapper()
{
#ifdef LEVELDB_DEBUG
    LogPrintf("~CWalletDBWrapper()\n");
#endif
    delete pdb;
    pdb = NULL;
    delete options.filter_policy;
    options.filter_policy = NULL;
    delete options.block_cache;
    options.block_cache = NULL;
    delete penv;
    options.env = NULL;
}

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

CDB::CDB(CWalletDBWrapper &dbw_in, const char* pszMode, bool fFlushOnCloseIn):
    dbw(dbw_in), hasChanges(false), aborted(false), flushOnClose(fFlushOnCloseIn)
{
}

CDB::~CDB()
{
    if (hasChanges && !aborted) {
#ifdef LEVELDB_DEBUG
        LogPrintf("Committing batch\n");
#endif
        leveldb::Status status = dbw.pdb->Write(flushOnClose ? dbw.syncoptions : dbw.writeoptions, &batch);
        if (!status.ok()) {
            throw CDBError("Error writing changes to wallet LevelDB: " + status.ToString());
        }
    }
#ifdef LEVELDB_DEBUG
    else {
        if (hasChanges) {
            LogPrintf("Aborted batch with changes\n");
        }
    }
#endif
}

Dbc* CDB::GetCursor()
{
    leveldb::Iterator *rv = dbw.pdb->NewIterator(dbw.iteroptions);
    if (rv) {
        rv->SeekToFirst();
        return new Dbc(rv);
    } else {
        return NULL;
    }
}

int CDB::ReadAtCursor(Dbc* pcursor, CDataStream& ssKey, CDataStream& ssValue, bool setRange)
{
    leveldb::Iterator *piter = pcursor->piter;
    if (setRange) { // setRange does a seek
        leveldb::Slice slKey(ssKey.data(), ssKey.size());
        piter->Seek(slKey);
    }
    if (piter->Valid()) {
        // Convert to streams
        leveldb::Slice slKey = piter->key();
        ssKey.SetType(SER_DISK);
        ssKey.clear();
        ssKey.write(slKey.data(), slKey.size());
        leveldb::Slice slValue = piter->value();
        ssValue.SetType(SER_DISK);
        ssValue.clear();
        ssValue.write(slValue.data(), slValue.size());

        // Advance
        piter->Next();
    } else {
        return DB_NOTFOUND;
    }
    return 0;
}

void Dbc::close()
{
    delete piter;
    delete this;
}
