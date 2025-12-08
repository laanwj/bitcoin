#!/usr/bin/env bash
# Copyright (c) 2019-2022 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
export LC_ALL=C
set -e -o pipefail
export TZ=UTC

# Although Guix _does_ set umask when building its own packages (in our case,
# this is all packages in manifest.scm), it does not set it for `guix
# shell`. It does make sense for at least `guix shell --container`
# to set umask, so if that change gets merged upstream and we bump the
# time-machine to a commit which includes the aforementioned change, we can
# remove this line.
#
# This line should be placed before any commands which creates files.
umask 0022

if [ -n "$V" ]; then
    # Print both unexpanded (-v) and expanded (-x) forms of commands as they are
    # read from this file.
    set -vx
    # Set VERBOSE for CMake-based builds
    export VERBOSE="$V"
fi

# Check that required environment variables are set
cat << EOF
Required environment variables as seen inside the container:
    DIST_ARCHIVE_BASE: ${DIST_ARCHIVE_BASE:?not set}
    DISTNAME: ${DISTNAME:?not set}
    HOST: ${HOST:?not set}
    SOURCE_DATE_EPOCH: ${SOURCE_DATE_EPOCH:?not set}
    JOBS: ${JOBS:?not set}
    DISTSRC: ${DISTSRC:?not set}
    OUTDIR: ${OUTDIR:?not set}
EOF

#####################
# Environment Setup #
#####################

unset LIBRARY_PATH
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset OBJCPLUS_INCLUDE_PATH

EXTRA_CXXFLAGS="-fdump-tree-all"

for variant in original reverted; do
    OUT_OBJ=test-${variant}.cpp.obj

    # removing -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 or using -D_FORTIFY_SOURCE=2 will result in determinism
    x86_64-w64-mingw32-g++ -DNOMINMAX -DWIN32 -DWIN32_LEAN_AND_MEAN -D_MT -D_WIN32_IE=0x0A00 -D_WIN32_WINNT=0x0A00 -D_WINDOWS \
        ${EXTRA_CXXFLAGS} \
        -O2 -g -fvisibility=hidden -fstack-reuse=none -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -std=c++20 \
        -MD -MT ${OUT_OBJ} -MF ${OUT_OBJ} -save-temps -o ${OUT_OBJ} -c \
        /bitcoin/test-${variant}.i && \
        sha256sum ${OUT_OBJ}
done
