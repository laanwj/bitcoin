// Copyright (c) 2012-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_DBWRAPPER_H
#define BITCOIN_DBWRAPPER_H

#include "clientversion.h"
#include "serialize.h"
#include "streams.h"
#include "util.h"
#include "utilstrencodings.h"
#include "version.h"

#include <boost/filesystem/path.hpp>

#include <lmdb/lmdb.h>

class dbwrapper_error : public std::runtime_error
{
public:
    dbwrapper_error(const std::string& msg) : std::runtime_error(msg) {}
};

class CDBWrapper;

/** These should be considered an implementation detail of the specific database.
 */
namespace dbwrapper_private {

/** Handle database error by throwing dbwrapper_error exception.
 */
void HandleError(int status);

/** Work around circular dependency, as well as for testing in dbwrapper_tests.
 * Database obfuscation should be considered an implementation detail of the
 * specific database.
 */
const std::vector<unsigned char>& GetObfuscateKey(const CDBWrapper &w);

};

/** Batch of changes queued to be written to a CDBWrapper */
class CDBBatch
{
    friend class CDBWrapper;

private:
    const CDBWrapper &parent;
    MDB_txn *txn;
    MDB_dbi dbi;

    // Disable copying
    CDBBatch(const CDBBatch&);
    CDBBatch& operator=(const CDBBatch&);
public:
    /**
     * @param[in] parent    CDBWrapper that this batch is to be submitted to
     */
    CDBBatch(const CDBWrapper &parent);
    ~CDBBatch();

    template <typename K, typename V>
    void Write(const K& key, const V& value)
    {
	MDB_val slKey, slValue;
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(ssKey.GetSerializeSize(key));
        ssKey << key;
        slKey.mv_data = &ssKey[0];
        slKey.mv_size = ssKey.size();

        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        ssValue.reserve(ssValue.GetSerializeSize(value));
        ssValue << value;
        ssValue.Xor(dbwrapper_private::GetObfuscateKey(parent));
        slValue.mv_data = &ssValue[0];
        slValue.mv_size = ssValue.size();

        dbwrapper_private::HandleError(mdb_put(txn, dbi, &slKey, &slValue, 0));
    }

    template <typename K>
    void Erase(const K& key)
    {
	MDB_val slKey;
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(ssKey.GetSerializeSize(key));
        ssKey << key;
        slKey.mv_data = &ssKey[0];
        slKey.mv_size = ssKey.size();
        int status = mdb_del(txn, dbi, &slKey, NULL);
        if (status != MDB_NOTFOUND) // delete of non-existent key is ok
            dbwrapper_private::HandleError(status);
    }
};

class CDBIterator
{
private:
    const CDBWrapper &parent;
    MDB_cursor *cursor;
    bool valid;
    MDB_val key, data;

    void Seek(const void *data_in, size_t size);

    // Disable copying
    CDBIterator(const CDBIterator&);
    CDBIterator& operator=(const CDBIterator&);
public:

    /**
     * @param[in] parent           Parent CDBWrapper instance.
     * @param[in] cursorIn         The original lmdb iterator.
     */
    CDBIterator(const CDBWrapper &parent, MDB_cursor *cursorIn) :
        parent(parent), cursor(cursorIn), valid(false) { }
    ~CDBIterator();

    bool Valid() { return valid; }

    void SeekToFirst();

    template<typename K> void Seek(const K& key) {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(ssKey.GetSerializeSize(key));
        ssKey << key;
        Seek(&ssKey[0], ssKey.size());
    }

    void Next();

    template<typename K> bool GetKey(K& key_out) {
        assert(valid);
        try {
            CDataStream ssKey((const char*)key.mv_data, (const char*)key.mv_data + key.mv_size, SER_DISK, CLIENT_VERSION);
            ssKey >> key_out;
        } catch (const std::exception&) {
            return false;
        }
        return true;
    }

    unsigned int GetKeySize() {
        assert(valid);
        return key.mv_size;
    }

    template<typename V> bool GetValue(V& value_out) {
        assert(valid);
        try {
            CDataStream ssValue((const char*)data.mv_data, (const char*)data.mv_data + data.mv_size, SER_DISK, CLIENT_VERSION);
            ssValue.Xor(dbwrapper_private::GetObfuscateKey(parent));
            ssValue >> value_out;
        } catch (const std::exception&) {
            return false;
        }
        return true;
    }

    unsigned int GetValueSize() {
        assert(valid);
        return data.mv_size;
    }

};

class CDBWrapper
{
    friend const std::vector<unsigned char>& dbwrapper_private::GetObfuscateKey(const CDBWrapper &w);
    friend class CDBBatch;
private:
    //! custom environment this database is using (may be NULL in case of default environment)
    MDB_env* env;

    //! database handle
    MDB_dbi dbi;

    //! reusable transaction for reading
    MDB_txn* rd_txn;

    //! Wipe database on close?
    bool wipe_on_close;

    //! a key used for optional XOR-obfuscation of the database
    std::vector<unsigned char> obfuscate_key;

    //! the key under which the obfuscation key is stored
    static const std::string OBFUSCATE_KEY_KEY;

    //! the length of the obfuscate key in number of bytes
    static const unsigned int OBFUSCATE_KEY_NUM_BYTES;

    std::vector<unsigned char> CreateObfuscateKey() const;

public:
    /**
     * @param[in] path        Location in the filesystem where lmdb data will be stored.
     * @param[in] nCacheSize  IGNORED
     * @param[in] fMemory     If true, use lmdb's memory environment.
     * @param[in] fWipe       If true, remove all existing data.
     * @param[in] obfuscate   If true, store data obfuscated via simple XOR. If false, XOR
     *                        with a zero'd byte array.
     */
    CDBWrapper(const boost::filesystem::path& path, size_t nCacheSize, bool fMemory, bool fWipe, bool obfuscate, uint64_t maxDbSize);
    ~CDBWrapper();

    template <typename K, typename V>
    bool Read(const K& key, V& value) const
    {
	MDB_val slKey, slData;
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(ssKey.GetSerializeSize(key));
        ssKey << key;
        slKey.mv_data = &ssKey[0];
        slKey.mv_size = ssKey.size();

        int status = mdb_get(rd_txn, dbi, &slKey, &slData);
        if (status != MDB_SUCCESS) {
            if (status == MDB_NOTFOUND)
                return false;
            LogPrintf("LMDB read failure: %s\n", mdb_strerror(status));
            dbwrapper_private::HandleError(status);
        }
        try {
            CDataStream ssValue((const char*)slData.mv_data, (const char*)slData.mv_data + slData.mv_size, SER_DISK, CLIENT_VERSION);
            ssValue.Xor(obfuscate_key);
            ssValue >> value;
        } catch (const std::exception&) {
            return false;
        }
        return true;
    }

    template <typename K, typename V>
    bool Write(const K& key, const V& value, bool fSync = false)
    {
        CDBBatch batch(*this);
        batch.Write(key, value);
        return WriteBatch(batch, fSync);
    }

    template <typename K>
    bool Exists(const K& key) const
    {
        MDB_val slKey, slData;
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(ssKey.GetSerializeSize(key));
        ssKey << key;
        slKey.mv_data = &ssKey[0];
        slKey.mv_size = ssKey.size();

        int status = mdb_get(rd_txn, dbi, &slKey, &slData);
        if (status != MDB_SUCCESS && status != MDB_NOTFOUND) {
            LogPrintf("LMDB read failure: %s\n", mdb_strerror(status));
            dbwrapper_private::HandleError(status);
        }
        return status == MDB_SUCCESS;
    }

    template <typename K>
    bool Erase(const K& key, bool fSync = false)
    {
        CDBBatch batch(*this);
        batch.Erase(key);
        return WriteBatch(batch, fSync);
    }

    bool WriteBatch(CDBBatch& batch, bool fSync = false);

    // not available for LMDB; provide for compatibility with BDB
    bool Flush()
    {
        return true;
    }

    bool Sync();
    CDBIterator *NewIterator();

    /**
     * Return true if the database managed by this class contains no entries.
     */
    bool IsEmpty();
};

#endif // BITCOIN_DBWRAPPER_H

