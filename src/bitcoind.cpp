// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "chainparams.h"
#include "clientversion.h"
#include "compat.h"
#include "fs.h"
#include "rpc/server.h"
#include "init.h"
#include "noui.h"
#include "scheduler.h"
#include "util.h"
#include "httpserver.h"
#include "httprpc.h"
#include "utilstrencodings.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/thread.hpp>

#include <stdio.h>
#ifdef CLOUDABI
#include <boost/algorithm/string/join.hpp>
#include <program.h>
#include <argdata.hpp>
#endif

/* Introduction text for doxygen: */

/*! \mainpage Developer documentation
 *
 * \section intro_sec Introduction
 *
 * This is the developer documentation of the reference client for an experimental new digital currency called Bitcoin (https://www.bitcoin.org/),
 * which enables instant payments to anyone, anywhere in the world. Bitcoin uses peer-to-peer technology to operate
 * with no central authority: managing transactions and issuing money are carried out collectively by the network.
 *
 * The software is a community-driven open source project, released under the MIT license.
 *
 * \section Navigation
 * Use the buttons <code>Namespaces</code>, <code>Classes</code> or <code>Files</code> at the top of the page to start navigating the code.
 */

void WaitForShutdown(boost::thread_group* threadGroup)
{
    bool fShutdown = ShutdownRequested();
    // Tell the main threads to shutdown.
    while (!fShutdown)
    {
        MilliSleep(200);
        fShutdown = ShutdownRequested();
    }
    if (threadGroup)
    {
        Interrupt(*threadGroup);
        threadGroup->join_all();
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//
bool AppInit()
{
    boost::thread_group threadGroup;
    CScheduler scheduler;

    bool fRet = false;

    // Process help and version before taking care about datadir
    if (IsArgSet("-?") || IsArgSet("-h") ||  IsArgSet("-help") || IsArgSet("-version"))
    {
        std::string strUsage = strprintf(_("%s Daemon"), _(PACKAGE_NAME)) + " " + _("version") + " " + FormatFullVersion() + "\n";

        if (IsArgSet("-version"))
        {
            strUsage += FormatParagraph(LicenseInfo());
        }
        else
        {
            strUsage += "\n" + _("Usage:") + "\n" +
                  "  bitcoind [options]                     " + strprintf(_("Start %s Daemon"), _(PACKAGE_NAME)) + "\n";

            strUsage += "\n" + HelpMessage(HMM_BITCOIND);
        }

#ifndef CLOUDABI
        fprintf(stdout, "%s", strUsage.c_str());
#endif
        return true;
    }

    try
    {
        if (!fs::is_directory(GetDataDir(false)))
        {
            fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n", GetArg("-datadir", "").c_str());
            return false;
        }
        try
        {
            ReadConfigFile(GetArg("-conf", BITCOIN_CONF_FILENAME));
        } catch (const std::exception& e) {
            fprintf(stderr,"Error reading configuration file: %s\n", e.what());
            return false;
        }
        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        try {
            SelectParams(ChainNameFromCommandLine());
        } catch (const std::exception& e) {
            fprintf(stderr, "Error: %s\n", e.what());
            return false;
        }

        // -server defaults to true for bitcoind but not for the GUI so do this here
        SoftSetBoolArg("-server", true);
        // Set this early so that parameter interactions go to console
        InitLogging();
        InitParameterInteraction();
        if (!AppInitBasicSetup())
        {
            // InitError will have been called with detailed error, which ends up on console
            exit(EXIT_FAILURE);
        }
        if (!AppInitParameterInteraction())
        {
            // InitError will have been called with detailed error, which ends up on console
            exit(EXIT_FAILURE);
        }
        if (!AppInitSanityChecks())
        {
            // InitError will have been called with detailed error, which ends up on console
            exit(EXIT_FAILURE);
        }
        if (GetBoolArg("-daemon", false))
        {
#if HAVE_DECL_DAEMON
            fprintf(stdout, "Bitcoin server starting\n");

            // Daemonize
            if (daemon(1, 0)) { // don't chdir (1), do close FDs (0)
                fprintf(stderr, "Error: daemon() failed: %s\n", strerror(errno));
                return false;
            }
#else
            fprintf(stderr, "Error: -daemon is not supported on this operating system\n");
            return false;
#endif // HAVE_DECL_DAEMON
        }

        fRet = AppInitMain(threadGroup, scheduler);
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "AppInit()");
    } catch (...) {
        PrintExceptionContinue(NULL, "AppInit()");
    }

    if (!fRet)
    {
        Interrupt(threadGroup);
        // threadGroup.join_all(); was left out intentionally here, because we didn't re-test all of
        // the startup-failure cases to make sure they don't result in a hang due to some
        // thread-blocking-waiting-for-another-thread-during-startup case
    } else {
        WaitForShutdown(&threadGroup);
    }
    Shutdown();

    return fRet;
}

#ifndef CLOUDABI
int main(int argc, char* argv[])
{
    SetupEnvironment();

    // Connect bitcoind signal handlers
    noui_connect();

    ParseParameters(argc, argv);

    return (AppInit() ? EXIT_SUCCESS : EXIT_FAILURE);
}
#else
void program_main(const argdata_t *ad) {
    mstd::optional<int> console_fd;
    mstd::optional<int> datadir_fd;
    mstd::optional<int> rpc_fd;
    mstd::optional<int> p2p_fd;
    mstd::optional<int> zmq_fd;
    argdata_t::map args;
    for (auto &i: ad->as_map()) {
        auto key = i.first->as_str();
        if (key == "console") {
            console_fd = i.second->get_fd();
        } else if(key == "datadir") {
            datadir_fd = i.second->get_fd();
        } else if(key == "args") {
            args = i.second->as_map();
        } else if(key == "rpc") {
            rpc_fd = i.second->get_fd();
        } else if(key == "p2p") {
            p2p_fd = i.second->get_fd();
        } else if(key == "zmq") {
            zmq_fd = i.second->get_fd();
        }
    }
    // Console fd is optional
    if (console_fd) {
        SetConsoleFD(console_fd.value());
    }
    // Datadir fd is mandatory
    if (datadir_fd) {
        SetDataDirFD(datadir_fd.value());
    } else {
        LogPrintf("Need to specify data directory\n");
        std::exit(1);
    }
    // Log test message
    fPrintToConsole = true;
    LogPrintf("Welcome to cloudabi\n");

    SetupEnvironment();
    noui_connect();

    // Process arguments
    LogPrintf("Processing arguments\n");
    for (const auto &entry: args) {
        std::string key = "-" + std::string(entry.first->as_str());
	mstd::optional<argdata_t::seq> seqval = entry.second->get_seq();
        if (seqval) {
            std::vector<std::string> values;
            for (const auto &x: seqval.value()) {
                values.push_back(std::string(x->as_str()));
            }
            LogPrintf("multiarg %s: %s\n", key, boost::algorithm::join(values, ", "));
            SoftSetMultiArg(key, values);
        } else { /* Convert any sensible kind of argument to string */
            mstd::optional<mstd::string_view> strval = entry.second->get_str();
            mstd::optional<bool> boolval = entry.second->get_bool();
            mstd::optional<double> doubleval = entry.second->get_float();
            mstd::optional<int64_t> intval = entry.second->get_int<int64_t>();
            std::string value;
            if (strval) {
                value = strval.value();
            } else if (boolval) {
                value = boolval.value() ? "1" : "0";
            } else if(doubleval) {
                value = strprintf("%f", doubleval.value());
            } else if(intval) {
                value = strprintf("%d", intval.value());
            }
            LogPrintf("arg %s: %s\n", key, value);
            SoftSetArg(key, value);
        }
    }
    // Pass RPC and P2P fd as arguments
    // TODO: should be able to accept mulitple of these to bind to multiple addresses,
    // as well as associated bind/whitelist flags.
    // TODO: this is a hack, these arguments should be passed in a configuration
    // structure instead of magically enter the program through a global
    // namespace.
    if (rpc_fd) {
        ForceSetArg("-rpcfd", itostr(rpc_fd.value()));
        LogPrintf("rpcfd: %i\n", rpc_fd.value());
    }
    if (p2p_fd) {
        ForceSetArg("-p2pfd", itostr(p2p_fd.value()));
        LogPrintf("p2pfd: %i\n", p2p_fd.value());
    }
    if (zmq_fd) {
        ForceSetArg("-zmqfd", itostr(zmq_fd.value()));
        LogPrintf("zmqfd: %i\n", zmq_fd.value());
    }

    std::exit(AppInit() ? EXIT_SUCCESS : EXIT_FAILURE);
}
#endif
