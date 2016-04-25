// Copyright (c) 2012-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/** Optimized dummy in-memory database.
 * State is read from disk in constructor, and written back to disk in destructor.
 *
 * TODO: An iterator should lock the database.
 */

#include "dbwrapper.h"

#include "util.h"
#include "random.h"

#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>

#include <stdint.h>

CDBWrapper::CDBWrapper(const boost::filesystem::path& path, size_t nCacheSize, bool fMemory, bool fWipe, bool obfuscate)
{
    boost::filesystem::create_directories(path);
    filename = (path / "data.db").string();
    if (fWipe) {
        LogPrintf("db: Wiping %s\n", filename);
        boost::filesystem::remove(filename);
    }
    FILE *file = fopen(filename.c_str(), "rb");
    if (file) {
        LogPrintf("db: Reading state from %s\n", filename);
        int64_t nTime1 = GetTimeMicros();
        int64_t count = 0;
        CAutoFile ss(file, SER_NETWORK, PROTOCOL_VERSION);
        while(true) {
            DBDataItem key, value;
            ss >> key;
            if (key.empty()) // empty key marks EOF
                break;
            std::pair<DBMap::iterator, bool> rv = data.insert(std::make_pair(key, DBDataItem()));
            ss >> rv.first->second;
            ++count;
        }
        int64_t nTime2 = GetTimeMicros();
        LogPrintf("db: Read state from %s [%i records in %.2fms]\n", filename, count, (nTime2 - nTime1) * 1e-3);
    } else {
        LogPrintf("db: Starting with clean slate\n");
    }
}

CDBWrapper::~CDBWrapper()
{
    LogPrintf("db: Write state to %s\n", filename);
    int64_t nTime1 = GetTimeMicros();
    int64_t count = 0;
    FILE *file = fopen(filename.c_str(), "wb");
    CAutoFile ss(file, SER_NETWORK, PROTOCOL_VERSION);
    BOOST_FOREACH(const PAIRTYPE(DBDataItem,DBDataItem) &p, data) {
        ss << p.first;
        ss << p.second;
        count += 1;
    }
    DBDataItem empty;
    ss << empty;
    int64_t nTime2 = GetTimeMicros();
    LogPrintf("db: Written state to %s [%i records in %.2fms]\n", filename, count, (nTime2 - nTime1) * 1e-3);
}

bool CDBWrapper::WriteBatch(CDBBatch& batch, bool fSync)
{
    // Note: destroys the batch
#ifdef DB_BATCH_LINEAR
    for(std::deque<CDBBatch::BatchItem>::iterator i = batch.delta.begin(); i != batch.delta.end(); ++i) {
        if (i->del)
            data.erase(i->key);
        else {
            std::pair<DBMap::iterator,bool> rv = data.insert(std::make_pair(i->key, DBDataItem()));
            rv.first->second.swap(i->value);
        }
    }
#else
    // TODO: could make use of the fact that a batch as well as data is sorted
    for(std::map<DBDataItem, CDBBatch::BatchItem>::iterator i = batch.delta.begin(); i != batch.delta.end(); ++i) {
        if (i->second.del)
            data.erase(i->first);
        else {
            std::pair<DBMap::iterator,bool> rv = data.insert(std::make_pair(i->first, DBDataItem()));
            rv.first->second.swap(i->second.value);
        }
    }
#endif
    return true;
}

bool CDBWrapper::IsEmpty()
{
    return data.empty();
}

CDBIterator::~CDBIterator() {
}

bool CDBIterator::Valid() {
    return piter != parent.data.end();
}

void CDBIterator::Seek(const DBDataItem &key)
{
    piter = parent.data.lower_bound(key);
}

void CDBIterator::SeekToFirst() {
    piter = parent.data.begin();
}

void CDBIterator::Next() {
    ++piter;
}
