// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "testutil.h"

#ifdef WIN32
#include <shlobj.h>
#endif

#include "fs.h"

fs::path GetTempPath() {
#ifdef CLOUDABI
    return fs::path(".");
#else
    return fs::temp_directory_path();
#endif
}
