// Copyright (c) 2012-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_DBWRAPPER_H
#define BITCOIN_DBWRAPPER_H

#define DB_BATCH_LINEAR

#include "clientversion.h"
#include "serialize.h"
#include "streams.h"
#include "util.h"
#include "utilstrencodings.h"
#include "version.h"

#include <boost/filesystem/path.hpp>

#ifdef DB_BATCH_LINEAR
#include <deque>
#endif

class dbwrapper_error : public std::runtime_error
{
public:
    dbwrapper_error(const std::string& msg) : std::runtime_error(msg) {}
};

class CDBWrapper;

typedef std::vector<uint8_t> DBDataItem; // This MUST be uint8_t, with char (signed on x86) the sorting order will be broken
typedef std::map<DBDataItem, DBDataItem> DBMap;

/** Batch of changes queued to be written to a CDBWrapper */
#ifdef DB_BATCH_LINEAR
class CDBBatch
{
    friend class CDBWrapper;
    struct BatchItem {
    public:
        BatchItem(bool delIn=false): del(delIn) {}

        bool del;
        DBDataItem key;
        DBDataItem value;
    };
    std::deque<BatchItem> delta;

private:
    const CDBWrapper &parent;

public:
    /**
     * @param[in] parent    CDBWrapper that this batch is to be submitted to
     */
    CDBBatch(const CDBWrapper &parent) : parent(parent) { };

    template <typename K, typename V>
    void Write(const K& key, const V& value)
    {
        delta.push_back(BatchItem(false)); // c++11 emplace_back
        BatchItem &item = delta.back();

        CWDataStream<DBDataItem> ssKey(item.key, SER_DISK, CLIENT_VERSION);
        ssKey.reserve(ssKey.GetSerializeSize(key));
        ssKey << key;

        CWDataStream<DBDataItem> ssValue(item.value, SER_DISK, CLIENT_VERSION);
        ssValue.reserve(ssValue.GetSerializeSize(value));
        ssValue << value;
    }

    template <typename K>
    void Erase(const K& key)
    {
        delta.push_back(BatchItem(true)); // c++11 emplace_back
        BatchItem &item = delta.back();

        CWDataStream<DBDataItem> ssKey(item.key, SER_DISK, CLIENT_VERSION);
        ssKey.reserve(ssKey.GetSerializeSize(key));
        ssKey << key;
    }
};
#else
class CDBBatch
{
    friend class CDBWrapper;
    struct BatchItem {
    public:
        BatchItem(bool delIn=false): del(delIn) {}

        bool del;
        DBDataItem value;

        void swap(BatchItem &o)
        {
            bool tmpdel = del;
            del = o.del;
            o.del = tmpdel;
            value.swap(o.value);
        }
    };
    std::map<DBDataItem, BatchItem> delta;

private:
    const CDBWrapper &parent;

public:
    /**
     * @param[in] parent    CDBWrapper that this batch is to be submitted to
     */
    CDBBatch(const CDBWrapper &parent) : parent(parent) { };

    template <typename K, typename V>
    void Write(const K& key, const V& value)
    {
        DBDataItem slKey;
        BatchItem item(false);
        CWDataStream<DBDataItem> ssKey(slKey, SER_DISK, CLIENT_VERSION);
        ssKey.reserve(ssKey.GetSerializeSize(key));
        ssKey << key;

        CWDataStream<DBDataItem> ssValue(item.value, SER_DISK, CLIENT_VERSION);
        ssValue.reserve(ssValue.GetSerializeSize(value));
        ssValue << value;

        std::pair<std::map<DBDataItem, BatchItem>::iterator, bool> rv = delta.insert(std::make_pair(slKey, BatchItem()));
        rv.first->second.swap(item);
    }

    template <typename K>
    void Erase(const K& key)
    {
        DBDataItem slKey;
        BatchItem item(true);
        CWDataStream<DBDataItem> ssKey(slKey, SER_DISK, CLIENT_VERSION);
        ssKey.reserve(ssKey.GetSerializeSize(key));
        ssKey << key;

        std::pair<std::map<DBDataItem, BatchItem>::iterator, bool> rv = delta.insert(std::make_pair(slKey, BatchItem()));
        rv.first->second.swap(item);
    }
};
#endif

class CDBIterator
{
private:
    const CDBWrapper &parent;
    DBMap::const_iterator piter;

    /* seek to key, or the first item after it lexicographically */
    void Seek(const DBDataItem &key);
public:

    /**
     * @param[in] parent           Parent CDBWrapper instance.
     * @param[in] piterIn          The original leveldb iterator.
     */
    CDBIterator(const CDBWrapper &parent, DBMap::const_iterator piterIn) :
        parent(parent), piter(piterIn) { };
    ~CDBIterator();

    bool Valid();

    void SeekToFirst();

    template<typename K> void Seek(const K& key) {
        DBDataItem slKey;
        CWDataStream<DBDataItem> ssKey(slKey, SER_DISK, CLIENT_VERSION);
        ssKey.reserve(ssKey.GetSerializeSize(key));
        ssKey << key;
        Seek(slKey);
    }

    void Next();

    template<typename K> bool GetKey(K& key) {
        try {
            CRDataStream<DBDataItem> ssKey(piter->first, SER_DISK, CLIENT_VERSION);
            ssKey >> key;
        } catch (const std::exception&) {
            return false;
        }
        return true;
    }

    unsigned int GetKeySize() {
        return piter->first.size();
    }

    template<typename V> bool GetValue(V& value) {
        try {
            CRDataStream<DBDataItem> ssValue(piter->second, SER_DISK, CLIENT_VERSION);
            ssValue >> value;
        } catch (const std::exception&) {
            return false;
        }
        return true;
    }

    unsigned int GetValueSize() {
        return piter->second.size();
    }

};

class CDBWrapper
{
private:
    std::string filename;
    DBMap data;

    friend class CDBIterator;
public:
    /**
     * @param[in] path        Location in the filesystem where leveldb data will be stored.
     * @param[in] nCacheSize  Configures various leveldb cache settings.
     * @param[in] fMemory     If true, use leveldb's memory environment.
     * @param[in] fWipe       If true, remove all existing data.
     * @param[in] obfuscate   If true, store data obfuscated via simple XOR. If false, XOR
     *                        with a zero'd byte array.
     */
    CDBWrapper(const boost::filesystem::path& path, size_t nCacheSize, bool fMemory = false, bool fWipe = false, bool obfuscate = false);
    ~CDBWrapper();

    template <typename K, typename V>
    bool Read(const K& key, V& value) const
    {
        DBDataItem slKey;
        CWDataStream<DBDataItem> ssKey(slKey, SER_DISK, CLIENT_VERSION);
        ssKey.reserve(ssKey.GetSerializeSize(key));
        ssKey << key;

        DBMap::const_iterator i = data.find(slKey);
        if (i == data.end())
            return false;
        try {
            CRDataStream<DBDataItem> ssValue(i->second, SER_DISK, CLIENT_VERSION);
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
        DBDataItem slKey;
        CWDataStream<DBDataItem> ssKey(slKey, SER_DISK, CLIENT_VERSION);
        ssKey.reserve(ssKey.GetSerializeSize(key));
        ssKey << key;
        return data.find(slKey) != data.end();
    }

    template <typename K>
    bool Erase(const K& key, bool fSync = false)
    {
        CDBBatch batch(*this);
        batch.Erase(key);
        return WriteBatch(batch, fSync);
    }

    bool WriteBatch(CDBBatch& batch, bool fSync = false);

    bool Flush() { return true; }

    bool Sync()
    {
        CDBBatch batch(*this);
        return WriteBatch(batch, true);
    }

    CDBIterator *NewIterator()
    {
        return new CDBIterator(*this, data.end());
    }

    /**
     * Return true if the database managed by this class contains no entries.
     */
    bool IsEmpty();
};

#endif // BITCOIN_DBWRAPPER_H

