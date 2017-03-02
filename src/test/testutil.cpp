// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "testutil.h"

#ifdef WIN32
#include <shlobj.h>
#endif

#include "fs.h"

#ifdef CLOUDABI
static fs::path temp_path;

void SetTempPath(const fs::path &p) {
    temp_path = p;
}
fs::path GetTempPath() {
    return temp_path;
}
#else
fs::path GetTempPath() {
    return fs::path(".");
}
#endif

