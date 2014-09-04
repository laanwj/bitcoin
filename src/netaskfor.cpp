// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "netaskfor.h"

#include <boost/thread.hpp>

#include "net.h"
#include "util.h"

/**
 * Inventory item request management.
 *
 * Maintains state separate from CNode.
 * There is a two-way mapping between CNodeAskForState and mapInvRequests.
 */
namespace {

/** Node specific state for netaskfor module */
class CNodeAskForState
{
public:
    CNodeAskForState(): node(0) {}

    /** Set of inv items that are being requested from this node */
    std::set<CInv> setAskFor;

    /** Network connection associated with this node */
    CNode *node; /// TODO ugly but needed for sending getdata
};

typedef std::multimap<int64_t, CInv> InvRequestsWorkQueue;

/** State of one inventory item request */
class CInvState
{
public:
    typedef std::set<NodeId> NodeSet;
    /** IDs of nodes that have this item */
    NodeSet nodes;
    /** IDs of nodes that we have not tried requesting this inv from already */
    NodeSet notRequestedFrom;
    /** Current node that this is being requested from */
    NodeId beingRequestedFrom;
    /** Must correspond to current entry in work queue for this
     *  inventory item (or invRequestsWorkQueue.end())
     */
    InvRequestsWorkQueue::iterator workQueueIter;
};
typedef std::map<CInv, CInvState> MapInvRequests;

/** Local data, all protected by cs_invRequests */
CCriticalSection cs_invRequests;
std::map<NodeId, CNodeAskForState> mapNodeAskForState;
MapInvRequests mapInvRequests;
/** The work queue keeps track when is the next time an inv item request needs
 * to be revisited.
 * Invariant: Each inv item has at most one entry.
 */
InvRequestsWorkQueue invRequestsWorkQueue;
CTimeoutCondition condInvRequests;
/** Exit thread flag */
bool fStopThread;

/** Get local state for a node id.
 * @note Requires that cs_invRequests is held
 */
CNodeAskForState *State(NodeId pnode)
{
    std::map<NodeId, CNodeAskForState>::iterator it = mapNodeAskForState.find(pnode);
    if (it == mapNodeAskForState.end())
        return NULL;
    return &it->second;
}

/** Handler for when a new node appears */
void InitializeNode(NodeId nodeid, const CNode *pnode)
{
    LOCK(cs_invRequests);
    /*CNodeAskForState &state = */
    mapNodeAskForState.insert(std::make_pair(nodeid, CNodeAskForState())).first->second;
}

/** Handler to clean up when a node goes away */
void FinalizeNode(NodeId nodeid)
{
    LOCK(cs_invRequests);
    CNodeAskForState *state = State(nodeid);
    assert(state);

    /// Clean up any requests that were underway to the node,
    /// or refer to the node.
    BOOST_FOREACH(const CInv &inv, state->setAskFor)
    {
        MapInvRequests::iterator i = mapInvRequests.find(inv);
        if (i != mapInvRequests.end())
        {
            i->second.nodes.erase(nodeid);
            i->second.notRequestedFrom.erase(nodeid);

            if (i->second.beingRequestedFrom == nodeid)
            {
                LogPrint("tx", "%s: %s was being requested from destructing peer=%i\n",
                        __func__,
                        inv.ToString(), nodeid);
                i->second.beingRequestedFrom = 0;
                /// Make sure the old workqueue item for the inv is removed,
                /// to avoid spurious retries
                if (i->second.workQueueIter != invRequestsWorkQueue.end())
                    invRequestsWorkQueue.erase(i->second.workQueueIter);
                /// Re-trigger request logic
                i->second.workQueueIter = invRequestsWorkQueue.insert(std::make_pair(0, inv));
                condInvRequests.notify_one();
            }
        }
    }
    mapNodeAskForState.erase(nodeid);
}

/** Forget a certain inventory item request
 * @note requires that cs_invRequests lock is held.
 */
void Forget(MapInvRequests::iterator i)
{
    /// Remove reference to this inventory item request from nodes
    BOOST_FOREACH(NodeId nodeid, i->second.nodes)
    {
        CNodeAskForState *state = State(nodeid);
        assert(state);
        state->setAskFor.erase(i->first);
    }
    /// Remove from workqueue
    if (i->second.workQueueIter != invRequestsWorkQueue.end())
        invRequestsWorkQueue.erase(i->second.workQueueIter);
    /// Remove from map
    mapInvRequests.erase(i);
}

/** Actually request an item from a node.
 * @note requires that cs_invRequests lock is held.
 */
void RequestItem(NodeId nodeid, CInvState &invstate, const CInv &inv, bool isRetry)
{
    /// TODO what locks on node to we need here?
    CNodeAskForState *state = State(nodeid);
    assert(state && state->node);
    CNode *node = state->node;

    LogPrint("tx", "%s: Requesting %s from peer=%i (%s)\n", __func__,
            inv.ToString(), nodeid,
            isRetry ? "retry" : "first request");
    invstate.beingRequestedFrom = nodeid;

    int64_t nNow = GetTimeMicros();
    node->mapAskFor.insert(std::make_pair(nNow, inv);
}

void ThreadHandleAskFor()
{
    while (!fStopThread)
    {
        LogPrint("tx2", "%s: iteration\n", __func__);
        int64_t timeToNext = std::numeric_limits<int64_t>::max();
        {
            LOCK(cs_invRequests);
            int64_t now = GetTimeMicros();
            /// Process work queue entries that are timestamped either now or before now
            while (!invRequestsWorkQueue.empty() && invRequestsWorkQueue.begin()->first <= now)
            {
                const CInv &inv = invRequestsWorkQueue.begin()->second;
                MapInvRequests::iterator it = mapInvRequests.find(inv);
                LogPrint("tx2", "%s: processing %s\n", __func__, inv.ToString());
                if (it != mapInvRequests.end())
                {
                    CInvState &invstate = it->second;
                    invstate.workQueueIter = invRequestsWorkQueue.end();
                    /// Pick a node to request from, if available
                    ///
                    /// Currently this picks the node with the lowest node id
                    /// (notRequestedFrom is an ordered set) that offers the item
                    /// that we haven't / tried yet.
                    if (invstate.notRequestedFrom.empty())
                    {
                        LogPrint("tx2", "%s: No more nodes to request %s from, discarding request\n", __func__, inv.ToString());
                        Forget(it);
                    } else {
                        CInvState::NodeSet::iterator first = invstate.notRequestedFrom.begin();
                        NodeId nodeid = *first;
                        invstate.notRequestedFrom.erase(first);

                        RequestItem(nodeid, invstate, inv, invRequestsWorkQueue.begin()->first != 0);

                        /// Need to revisit this request after timeout
                        invstate.workQueueIter = invRequestsWorkQueue.insert(std::make_pair(now + NetAskFor::REQUEST_TIMEOUT, inv));
                    }
                } else {
                    LogPrint("tx2", "%s: request for %s is missing!\n", __func__, inv.ToString());
                }
                invRequestsWorkQueue.erase(invRequestsWorkQueue.begin());
            }

            /// Compute time to next event
            if (!invRequestsWorkQueue.empty())
                timeToNext = invRequestsWorkQueue.begin()->first - GetTimeMicros();
        }
        /// If we don't know how long until next work item, wait until woken up
        if (timeToNext == std::numeric_limits<int64_t>::max())
        {
            LogPrint("tx2", "%s: blocking\n", __func__);
            condInvRequests.wait();
        } else if (timeToNext > 0)
        {
            LogPrint("tx2", "%s: waiting for %d us\n", __func__, timeToNext);
            condInvRequests.timed_wait((timeToNext+999LL)/1000LL);
        }
    }
}

void StartThreads(boost::thread_group& threadGroup)
{
    fStopThread = false;
    /// Inventory management thread
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "askfor", &ThreadHandleAskFor));
}

void StopThreads()
{
    fStopThread = true;
    condInvRequests.notify_one();
}

}

namespace NetAskFor
{

void Completed(CNode *node, const CInv& inv)
{
    LOCK(cs_invRequests);
    MapInvRequests::iterator i = mapInvRequests.find(inv);
    if (i != mapInvRequests.end())
    {
        LogPrint("tx2", "%s: %s\n", __func__, inv.ToString());
        Forget(i);
    } else {
        /// This can happen if a node sends a transaction without unannouncing it with 'inv'
        /// first, or then we retry a request, which completes (and thus forget about), and then
        /// the original node comes back and sends our requested data anyway.
        LogPrint("tx2", "%s: %s not found!\n", __func__, inv.ToString());
    }
}

void AskFor(CNode *node, const CInv& inv)
{
    LOCK(cs_invRequests);
    NodeId nodeid = node->GetId();
    CNodeAskForState *state = State(nodeid);
    assert(state);
    state->node = node;

    /// Bound number of concurrent inventory requests to each node, this has
    /// the indirect effect of bounding all data structures.
    if (state->setAskFor.size() > MAX_SETASKFOR_SZ)
        return;

    LogPrint("tx", "askfor %s  peer=%d\n", inv.ToString(), nodeid);

    MapInvRequests::iterator it = mapInvRequests.find(inv);
    if (it == mapInvRequests.end())
    {
        std::pair<MapInvRequests::iterator, bool> ins = mapInvRequests.insert(std::make_pair(inv, CInvState()));
        it = ins.first;
        /// As this is the first time that this item gets announced by anyone, add it to the work queue immediately
        it->second.workQueueIter = invRequestsWorkQueue.insert(std::make_pair(0, inv));
        condInvRequests.notify_one();
    }
    std::pair<CInvState::NodeSet::iterator, bool> ins2 = it->second.nodes.insert(nodeid);
    if (ins2.second)
    {
        /// If this is the first time this node announces the inv item, add it to the set of untried nodes
        /// for the item.
        it->second.notRequestedFrom.insert(nodeid);
    }
    state->setAskFor.insert(inv);
}

void RegisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.InitializeNode.connect(&InitializeNode);
    nodeSignals.FinalizeNode.connect(&FinalizeNode);
    nodeSignals.StartThreads.connect(&StartThreads);
    nodeSignals.StopThreads.connect(&StopThreads);
}

void UnregisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.InitializeNode.disconnect(&InitializeNode);
    nodeSignals.FinalizeNode.disconnect(&FinalizeNode);
    nodeSignals.StartThreads.disconnect(&StartThreads);
    nodeSignals.StopThreads.disconnect(&StopThreads);
}

};
