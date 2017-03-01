// Copyright (c) 2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_FS_H
#define BITCOIN_FS_H

#include <stdio.h>
#include <string>

#ifndef CLOUDABI

#include <boost/filesystem.hpp>
/** Filesystem operations and types */
namespace fs = boost::filesystem;

#else

/** Filesystem operations and types for CloudABI */
namespace fs {

class path
{
public:
    path();
    path(int fd, const std::string &p_in);
    path(const std::string &p_in);
    path(const char *p_in);

    int fd() const { return f; }
    const std::string &string() const { return p; }

    path parent_path() const;
    bool is_complete() const;
    bool empty() const;

    path& operator/=(const path& p);

private:
    int f;
    std::string p;

    void normalize();
};
inline path operator/(const path& lhs, const path& rhs) { return path(lhs) /= rhs; }

struct space_info
{
    uint64_t available;
};

/** Filesystem exception */
class filesystem_error: public std::runtime_error
{
public:
    /* May need more detail (e.g. error codes) here at some point */
    filesystem_error(const std::string &what):
        std::runtime_error(what)
    { }
};

/* functions */
bool create_directories(const path& p);
bool create_directory(const path& p);
uint64_t remove_all(const path& p);
uint64_t file_size(const path& p);
bool remove(const path& p);
bool exists(const path& p);
bool is_directory(const path& p);
void rename(const path& old_p, const path& new_p);
space_info space(const path& p);
path system_complete(const path& p);

/* types
 * ifstream
 * ofstream
 * directory_iterator
 */
}; // namespace fs

#endif

/** Bridge operations to C stdio */
namespace fsbridge {
    FILE *fopen(const fs::path& p, const char *mode);
    FILE *freopen(const fs::path& p, const char *mode, FILE *stream);
};

#endif
