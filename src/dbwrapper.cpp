// Copyright (c) 2012-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dbwrapper.h"

#include "util.h"
#include "random.h"

#include <boost/filesystem.hpp>

#include <stdint.h>

static void WipeLMDB(const boost::filesystem::path& path)
{
    LogPrintf("Wiping LMDB in %s\n", path.string());
    boost::filesystem::remove(path / "data.mdb");
    boost::filesystem::remove(path / "lock.mdb");
    try {
        boost::filesystem::remove(path);
    } catch(boost::filesystem::filesystem_error &e) {
        // Raised if the directory is not empty - we only want to remove the
        // directory if empty so that's OK.
    }
}

CDBWrapper::CDBWrapper(const boost::filesystem::path& path, size_t nCacheSize, bool fMemory, bool fWipe, bool obfuscate, uint64_t maxDbSize):
    env(NULL), dbi(0), rd_txn(0), wipe_on_close(false)
{
    // TODO nCacheSize is actually ignored at the moment
    dbwrapper_private::HandleError(mdb_env_create(&env));
    // TODO hardcoding mapSize is not acceptable, need to start this low then
    // increase when MDB_MAP_FULL is returned.
    // Increasing needs all transactions to be closed, so aborts the current
    // transaction. This means that the transaction needs to be restartable OR
    // we need to be able to estimate the maximum size of a transaction upfront.
    dbwrapper_private::HandleError(mdb_env_set_mapsize(env, maxDbSize));
    // Turn off readahead. May help random read performance when the DB is larger than RAM and system RAM is full.
    unsigned int flags = MDB_NORDAHEAD;
    if (fMemory) {
        wipe_on_close = true;
        // No need to sync, the database will be wiped anyway
        flags |= MDB_NOSYNC|MDB_WRITEMAP;
    } else {
        // Sync using mdb_env_sync explicitly.
        flags |= 0; //MDB_MAPASYNC|MDB_WRITEMAP;
    }
    if (fWipe)
        WipeLMDB(path);
    LogPrintf("Opening LMDB in %s (maxsize %d)\n", path.string(), maxDbSize);
    boost::filesystem::create_directories(path);
    dbwrapper_private::HandleError(mdb_env_open(env, path.string().c_str(), flags, 0644));
    // Open database
    MDB_txn* txn;
    dbwrapper_private::HandleError(mdb_txn_begin(env, NULL, 0, &txn));
    dbwrapper_private::HandleError(mdb_dbi_open(txn, NULL, 0, &dbi));
    dbwrapper_private::HandleError(mdb_txn_commit(txn));

    // Create one read-transaction used for all reads that is reset before
    // every write, and renewed after it.
    dbwrapper_private::HandleError(mdb_txn_begin(env, NULL, MDB_RDONLY, &rd_txn));

    LogPrintf("Opened LMDB successfully\n");

    // The base-case obfuscation key, which is a noop.
    obfuscate_key = std::vector<unsigned char>(OBFUSCATE_KEY_NUM_BYTES, '\000');

    bool key_exists = Read(OBFUSCATE_KEY_KEY, obfuscate_key);

    if (!key_exists && obfuscate && IsEmpty()) {
        // Initialize non-degenerate obfuscation if it won't upset
        // existing, non-obfuscated data.
        std::vector<unsigned char> new_key = CreateObfuscateKey();

        // Write `new_key` so we don't obfuscate the key with itself
        Write(OBFUSCATE_KEY_KEY, new_key);
        obfuscate_key = new_key;

        LogPrintf("Wrote new obfuscate key for %s: %s\n", path.string(), HexStr(obfuscate_key));
    }

    LogPrintf("Using obfuscation key for %s: %s\n", path.string(), HexStr(obfuscate_key));
}

CDBWrapper::~CDBWrapper()
{
    // Store path if we intend to wipe this db after close
    boost::filesystem::path path;
    const char *cpath;
    if (wipe_on_close && mdb_env_get_path(env, &cpath) == MDB_SUCCESS) {
        // cpath is part of lmdb and should not be freed
        path = cpath;
    }
    // mdb_dbi_close is unnecessary
    if (rd_txn)
        mdb_txn_abort(rd_txn);
    if (env)
        mdb_env_close(env);
    if (wipe_on_close && !path.empty())
        WipeLMDB(path);
}

bool CDBWrapper::WriteBatch(CDBBatch& batch, bool fSync)
{
    // Reset read transaction before write
    mdb_txn_reset(rd_txn);
    // Commit write
    dbwrapper_private::HandleError(mdb_txn_commit(batch.txn));
    batch.txn = 0;
    if (fSync)
        Sync();
    // Renew read transaction after write
    dbwrapper_private::HandleError(mdb_txn_renew(rd_txn));
    return true;
}

// Prefixed with null character to avoid collisions with other keys
//
// We must use a string constructor which specifies length so that we copy
// past the null-terminator.
const std::string CDBWrapper::OBFUSCATE_KEY_KEY("\000obfuscate_key", 14);

const unsigned int CDBWrapper::OBFUSCATE_KEY_NUM_BYTES = 8;

/**
 * Returns a string (consisting of 8 random bytes) suitable for use as an
 * obfuscating XOR key.
 */
std::vector<unsigned char> CDBWrapper::CreateObfuscateKey() const
{
    unsigned char buff[OBFUSCATE_KEY_NUM_BYTES];
    GetRandBytes(buff, OBFUSCATE_KEY_NUM_BYTES);
    return std::vector<unsigned char>(&buff[0], &buff[OBFUSCATE_KEY_NUM_BYTES]);
}

bool CDBWrapper::Sync()
{
    dbwrapper_private::HandleError(mdb_env_sync(env, 1));
    return true;
}

CDBIterator *CDBWrapper::NewIterator()
{
    MDB_cursor *cursor;
    dbwrapper_private::HandleError(mdb_cursor_open(rd_txn, dbi, &cursor));
    return new CDBIterator(*this, cursor);
}

bool CDBWrapper::IsEmpty()
{
    MDB_cursor *cursor;
    MDB_val key, data;
    dbwrapper_private::HandleError(mdb_cursor_open(rd_txn, dbi, &cursor));
    int rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    if (rc != MDB_NOTFOUND)
        dbwrapper_private::HandleError(rc);
    mdb_cursor_close(cursor);
    return rc == MDB_NOTFOUND;
}

CDBIterator::~CDBIterator()
{
   mdb_cursor_close(cursor);
}

void CDBIterator::SeekToFirst()
{
    int rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    if (rc != MDB_NOTFOUND)
        dbwrapper_private::HandleError(rc);
    valid = (rc == MDB_SUCCESS);
}

void CDBIterator::Seek(const void *data_in, size_t size)
{
    key.mv_data = const_cast<void*>(data_in);
    key.mv_size = size;
    int rc = mdb_cursor_get(cursor, &key, &data, MDB_SET_RANGE);
    if (rc != MDB_NOTFOUND)
        dbwrapper_private::HandleError(rc);
    valid = (rc == MDB_SUCCESS);
}

void CDBIterator::Next()
{
    int rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    if (rc != MDB_NOTFOUND)
        dbwrapper_private::HandleError(rc);
    valid = (rc == MDB_SUCCESS);
}

CDBBatch::CDBBatch(const CDBWrapper &parent):
    parent(parent), txn(NULL), dbi(parent.dbi)
{
    dbwrapper_private::HandleError(mdb_txn_begin(parent.env, NULL, 0, &txn));
}

CDBBatch::~CDBBatch()
{
    if (txn) {
        LogPrintf("lmdb: Aborting transaction\n");
        mdb_txn_abort(txn);
    }
}

namespace dbwrapper_private {

void HandleError(int status)
{
    if (status == MDB_SUCCESS)
        return;
    const char *errstr = mdb_strerror(status);
    LogPrintf("LMDB error: %s\n", errstr);
    throw dbwrapper_error(errstr);
}

const std::vector<unsigned char>& GetObfuscateKey(const CDBWrapper &w)
{
    return w.obfuscate_key;
}

};
