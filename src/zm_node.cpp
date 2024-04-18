/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QDateTime>
#include <limits>
#include "deconz/atom_table.h"
#include "deconz/dbg_trace.h"
#include "deconz/timeref.h"
#include "deconz/u_rand32.h"
#include "deconz/ustring.h"
#include "zm_node.h"
#include "node_private.h"
#include "zm_neighbor.h"


static int ActiveEndpointsCheckInterval = 1800 * 1000;
static int MgmtLqiCheckInterval = 180 * 1000;
static const int IeeeAddrCheckInterval = 180 * 1000;
static const int BindingTableCheckInterval = 90 * 1000;
static const int PowerCheckInterval = 60 * 60 * 1000;
static const int MaxRetrys = 2;
static const uint MaxRetryWait = 600 * 1000;

namespace deCONZ
{

void setFetchInterval(RequestId item, int interval)
{
    interval *= 1000;
    switch (item)
    {
    case ReqActiveEndpoints:
        ActiveEndpointsCheckInterval = interval;
        break;

    case ReqMgmtLqi:
        MgmtLqiCheckInterval = interval;
        break;

    default:
        break;
    }
}

int getFetchInterval(RequestId item)
{
    switch (item)
    {
    case ReqActiveEndpoints:
        return ActiveEndpointsCheckInterval / 1000;

    case ReqMgmtLqi:
        return MgmtLqiCheckInterval / 1000;

    default:
        break;
    }

    return 0;
}

zmNode::zmNode(const MacCapabilities &macCapabilities) :
    Node()
{
    reset(macCapabilities);
//    touch();
    m_lastSeenByNeighbor.start();
}

zmNode::~zmNode()
{
}

/*!
    // Copy only public api parts
 */
zmNode &zmNode::operator=(const deCONZ::Node &other)
{

    deCONZ::Node *me = dynamic_cast<deCONZ::Node*>(this);
    DBG_Assert(me != 0);
    if (me)
    {
        *me = other;
    }

    return *this;
}

UString zmNode::extAddressString() const
{
    return d_ptr->extAddrStr;
}

/*!
    Returns all neighbors of the node.
 */
const std::vector<NodeNeighbor> &zmNode::neighbors() const
{
    return m_neighborsApi;
}

/*!
    Returns next endpoint to fetch or -1 if there arend any more.
 */
int zmNode::getNextUnfetchedEndpoint() const
{
    for (uint8_t ep : endpoints())
    {
        bool found = false;

        for (const auto &sd : simpleDescriptors())
        {
            if (ep == sd.endpoint())
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            return ep;
        }
    }

    if (!d_ptr->m_fetchEndpoints.empty())
    {
        return d_ptr->m_fetchEndpoints.back();
    }

    return -1;
}

void zmNode::removeFetchEndpoint(uint8_t ep)
{
    if ( d_ptr->m_fetchEndpoints.empty())
    {
        return;
    }

    if (ep == 255) // remove last
    {
       d_ptr->m_fetchEndpoints.pop_back();
       return;
    }

    for (uint8_t e : d_ptr->m_fetchEndpoints)
    {
        if (e == ep)
        {
            e = d_ptr->m_fetchEndpoints.back();
            d_ptr->m_fetchEndpoints.pop_back();
            return;
        }
    }
}

ZclCluster *zmNode::getCluster(int endpoint, uint16_t clusterId, ZclClusterSide side)
{
    SimpleDescriptor *sd = getSimpleDescriptor(endpoint);

    if (sd)
    {
        auto &clusters = sd->clusters(side);

        for (auto &cl : clusters)
        {
            if (cl.id() == clusterId)
            {
                return &cl;
            }
        }
    }

    return nullptr;
}

deCONZ::TimeMs zmNode::lastDiscoveryTryMs(deCONZ::SteadyTimeRef now) const
{
    if (isValid(m_lastDiscovery))
    {
        return now - m_lastDiscovery;
    }
    return {0};
}

void zmNode::discoveryTimerReset(SteadyTimeRef now)
{
     m_lastDiscovery = now;
}

bool zmNode::updatedClusterAttribute(SimpleDescriptor *simpleDescriptor, ZclCluster *cluster, ZclAttribute *attribute)
{
    if (!simpleDescriptor || !cluster || !attribute)
    {
        return false;
    }

    if (cluster->id() == 0x0000) // basic cluster
    {
    }

    return false;
}

void zmNode::setAddress(const Address &addr)
{
    d_ptr->address = addr;

    if (addr.hasExt())
    {
        char buf[24];
        AT_AtomIndex ati;
        snprintf(buf, sizeof(buf), FMT_MAC, FMT_MAC_CAST(addr.ext()));

        if (AT_AddAtom(buf, 18, &ati))
        {
            d_ptr->extAddrStr = UString::fromAtom(ati);
        }
    }
}

void zmNode::setVersion(const QString &version)
{
    if (!version.isEmpty() && m_swVersion != version)
    {
         m_swVersion = version;

         if (nodeDescriptor().manufacturerCode() == 0x1135)
         {
             int idx = version.indexOf('.');
             if (idx < 0)
             {
                 idx = 0;
             }
             else
             {
                 idx++;
             }
             const auto v = version.midRef(idx);
             if (!v.isEmpty())
             {
                 bool ok;
                 auto vv = v.toUInt(&ok, 16);
                 if (ok)
                 {
                     m_swVersionNum = vv;
                 }
             }
         }
    }
}

/*! Updates the last seen (response) unix timestamp. */
void zmNode::touch(deCONZ::SteadyTimeRef msecSinceEpoch)
{
    m_lastSeen = msecSinceEpoch;
    m_lastSeenElapsed.start();
    // reactivate if in fail state
    if (state() == deCONZ::FailureState)
    {
        DBG_Printf(DBG_INFO, "CTRL touch node: %s active again\n", extAddressString().c_str());
        setState(deCONZ::IdleState);
    }
}

/*! Updates the last seen unix timestamp. */
void zmNode::touchAsNeighbor()
{
    m_lastSeenByNeighbor.start();
}

int64_t zmNode::lastSeenByNeighbor() const
{
    return static_cast<int64_t>(m_lastSeenByNeighbor.elapsed());
}


/*! Resets the node data and state */
void zmNode::reset(MacCapabilities macCapabilities)
{
    resetAll();
    bool autoFetch = true; // TODO: set autofetch before calling reset() ... deCONZ::controller()->autoFetch();
    m_neighbors.clear();
    m_bindTable = BindingTable();
    setMacCapabilities(macCapabilities);
    m_lastSeen = {};
    m_lastSeenElapsed.invalidate();
    m_lastApsRequest = {};
    m_fcurItem = deCONZ::ReqUnknown;
    m_recvErrors = 0;
    m_state = deCONZ::IdleState;
    m_mgmtLqiStartIndex = 0x00;
    m_waitStateEnd = 0;
    m_lastDiscovery = {};
    m_needRejoin = false;

    NodeDescriptor nd = nodeDescriptor();
    nd.setDeviceType(deCONZ::UnknownDevice);
    setNodeDescriptor(nd);

    // static fetch items
    FetchInfo fi;

    m_fetchItems.clear();
    m_fetchItems.reserve(ReqMaxItems);

    for (int i = 0; i < ReqMaxItems; i++)
        m_fetchItems.push_back(fi); // empty dummys

    fi = FetchInfo(deCONZ::ReqIeeeAddr, IeeeAddrCheckInterval, MaxRetrys);
    fi.enabled = false;
    m_fetchItems[deCONZ::ReqIeeeAddr] = fi;

    fi = FetchInfo(deCONZ::ReqNodeDescriptor, NoCheckInterval, MaxRetrys);
    fi.enabled = autoFetch;
    m_fetchItems[deCONZ::ReqNodeDescriptor] = fi;

    fi = FetchInfo(deCONZ::ReqUserDescriptor, NoCheckInterval, MaxRetrys);
    fi.enabled = false;
    m_fetchItems[deCONZ::ReqUserDescriptor] = fi;

    resetItem(deCONZ::ReqActiveEndpoints);

    resetItem(deCONZ::ReqSimpleDescriptor);

    fi = FetchInfo(deCONZ::ReqMgmtBind, BindingTableCheckInterval, MaxRetrys);
    fi.addDependency(deCONZ::ReqNodeDescriptor);
    fi.enabled = false;
    m_fetchItems[deCONZ::ReqMgmtBind] = fi;

    m_fcurItem = deCONZ::ReqIeeeAddr;
}

void zmNode::resetItem(RequestId item)
{
    bool autoFetch = true; // TODO: set autofetch before calling reset() ... deCONZ::controller()->autoFetch();

    Q_ASSERT(item < ReqMaxItems);

    FetchInfo &fi = m_fetchItems[item];

    switch (item)
    {
    case deCONZ::ReqActiveEndpoints:
    {
        std::vector<uint8_t> emptyVec;
        setActiveEndpoints(emptyVec);

        fi = FetchInfo(deCONZ::ReqActiveEndpoints, ActiveEndpointsCheckInterval, MaxRetrys);
        fi.enabled = autoFetch;
    }
        break;

    case deCONZ::ReqSimpleDescriptor:
    {
        simpleDescriptors().clear();

        fi = FetchInfo(deCONZ::ReqSimpleDescriptor, NoCheckInterval, MaxRetrys);
        fi.addDependency(deCONZ::ReqActiveEndpoints);
        fi.enabled = autoFetch;
    }
        break;

    default:
        break;
    }
}

/*!
    Add or updates \p neighbor.

    If the neighbor is already known the internal entry will be
    updated. Otherwise a new entry will be added.
 */
bool zmNode::updateNeighbor(const zmNeighbor &neighbor)
{
    if (!neighbor.address().hasNwk() || !neighbor.address().hasExt())
    {
        return false;
    }

    std::vector<zmNeighbor>::iterator i = std::find(m_neighbors.begin(), m_neighbors.end(), neighbor);

    if (i != m_neighbors.end())
    {
        *i = neighbor;

        std::vector<NodeNeighbor>::iterator i = m_neighborsApi.begin();
        std::vector<NodeNeighbor>::iterator end = m_neighborsApi.end();

        for  (; i != end; ++i)
        {
            if (i->address().ext() == neighbor.address().ext())
            {
                *i = NodeNeighbor(neighbor.address(), neighbor.lqi());
                break;
            }
        }
    }
    else
    {
        m_neighbors.push_back(neighbor);
        m_neighborsApi.push_back(NodeNeighbor(neighbor.address(), neighbor.lqi()));
    }

    return true;
}

/*!
    Get the neighbor specified by \p address.

    \return true on success with the neighbor copied to \p out.
    \return false if the neighbor is unknown.
 */
bool zmNode::getNeighbor(const Address &address, zmNeighbor &out)
{
    zmNeighbor neib;
    neib.address() = address;

    std::vector<zmNeighbor>::const_iterator i = std::find(m_neighbors.begin(), m_neighbors.end(), neib);

    if (i != m_neighbors.end())
    {
        out = *i;
        return true;
    }

    return false;
}

zmNeighbor *zmNode::getNeighbor(const Address &address)
{
    auto i = std::find_if(m_neighbors.begin(), m_neighbors.end(), [&address](const auto &neib)
    {
        return address.hasExt() && neib.address().ext() == address.ext();
    });

    if (i != m_neighbors.end())
    {
        return &*i;
    }

    return nullptr;
}

/*!
    Removes all neighbors that are older than \p seconds.
 */
void zmNode::removeOutdatedNeighbors(int seconds)
{
    auto i = m_neighbors.begin();

    for (; i != m_neighbors.end();)
    {
        bool remove = false;

        if (!isValid(i->lastSeen()))
        {
            remove = true;
        }

        if (deCONZ::TimeSeconds{seconds} < i->lastSeen() - m_mgmtLqiLastRsp)
        {
            remove = true;
        }

        if (remove)
        {
            DBG_Printf(DBG_INFO, "remove outdated neighbor 0x%04X\n", i->address().nwk());

            auto j = m_neighborsApi.begin();
            const auto jend = m_neighborsApi.end();

            for  (; j != jend; ++j)
            {
                if (j->address().ext() == i->address().ext())
                {
                    *j = m_neighborsApi.back();
                    m_neighborsApi.pop_back();
                    break;
                }
            }

            *i = m_neighbors.back();
            m_neighbors.pop_back();
        }
        else
        {
            ++i;
        }
    }
}

/*!
    Removes neighbor spezified by \p address.
 */
void zmNode::removeNeighbor(const Address &address)
{
    std::vector<zmNeighbor>::iterator i;

    for (i = m_neighbors.begin(); i != m_neighbors.end(); ++i)
    {
        if (i->address().ext() == address.ext())
        {
            *i = m_neighbors.back();
            m_neighbors.pop_back();
            break;
        }
    }

    std::vector<NodeNeighbor>::iterator j = m_neighborsApi.begin();
    std::vector<NodeNeighbor>::iterator jend = m_neighborsApi.end();

    for  (; j != jend; ++j)
    {
        if (j->address().ext() == address.ext())
        {
            *j = m_neighborsApi.back();
            m_neighborsApi.pop_back();
            break;
        }
    }
}

CommonState zmNode::state() const
{
    // cascade here to keep things simple
    if (m_state == deCONZ::WaitState)
    {
        return deCONZ::BusyState;
    }
    return m_state;
}

void zmNode::setState(CommonState state)
{
    if (DBG_Assert(state != deCONZ::WaitState) == false)
    {
    }
    m_state = state;
}

void zmNode::setWaitState(uint timeoutSec)
{
    m_waitStateEnd = timeoutSec + deCONZ::steadyTimeRef().ref;
    m_state = deCONZ::WaitState;
}

void zmNode::checkWaitState()
{
    if (m_state == deCONZ::WaitState)
    {
        int64_t t = deCONZ::steadyTimeRef().ref;

        if (m_waitStateEnd < t)
        {
            DBG_Printf(DBG_INFO_L2, "node " FMT_MAC " leave wait state\n", FMT_MAC_CAST(address().ext()));
            m_state = deCONZ::IdleState;
        }
    }
}

bool zmNode::isInWaitState()
{
    return (m_state == deCONZ::WaitState);
}

/*!
    Incremtns the retry count for \p item.

    \return the new retry count.
 */
int zmNode::retryIncr(deCONZ::RequestId item)
{
    // static items
    if (item > ReqUnknown && item < ReqMaxItems)
    {
        FetchInfo &fi = m_fetchItems[item];

        if (fi.retries < INT_MAX)
        {
            fi.retries++;
        }

        if (fi.retries >= fi.retriesMax)
        {
            fi.lastCheck = deCONZ::steadyTimeRef().ref;
        }

        return fi.retries;
    }

    return -1;
}

/*!
    Returns the retry count for \p item or -1 if item not found.
 */
int zmNode::retryCount(deCONZ::RequestId item) const
{
    // static items
    if (item < ReqMaxItems)
    {
        const FetchInfo &fi = m_fetchItems[item];
        return fi.retries;
    }

    return -1;
}

/*!
    Set last mgmt lqi rsp \p time.
 */
void zmNode::setMgtmLqiLastRsp(SteadyTimeRef time)
{
    m_mgmtLqiLastRsp = time;
}

bool zmNode::FetchInfo::isEnabled()
{
    if (enabled && depend == 0)
    {
        return true;
    }

    return false;
}

void zmNode::FetchInfo::addDependency(deCONZ::RequestId id)
{
    if (id >= 0 && id < deCONZ::ReqMaxItems)
        depend |= 1 << id;
}

void zmNode::FetchInfo::removeDependency(deCONZ::RequestId id)
{
    if (id >= 0 && id < deCONZ::ReqMaxItems)
        depend &= ~(uint32_t)id;
}

/*!
    Optain if \p item has to be fetched (or fetched again).
 */
bool zmNode::needFetch(deCONZ::RequestId item)
{
    if (item >= ReqMaxItems)
    {
         // default for unknown items to prevent endless requests
        return false;
    }

    FetchInfo &fi = m_fetchItems[item];

    if (!fi.isEnabled())
    {
        return false;
    }

    if (fi.retries >= fi.retriesMax)
    {
        if ((fi.lastCheck + (int64_t)MaxRetryWait) < deCONZ::steadyTimeRef().ref)
        {
            // try again
            fi.retries = 0;
        }
        else
        {
            return false;
        }
    }

    switch (item)
    {
    case deCONZ::ReqNodeDescriptor:
    {
        if (nodeDescriptor().isNull())
        {
            return true;
        }
    }
        break;

    case deCONZ::ReqIeeeAddr:
    {
        if (fi.checkInterval != IeeeAddrCheckInterval)
        {
            fi.checkInterval = IeeeAddrCheckInterval;
        }

        if (address().hasNwk() && !address().hasExt())
        {
            return true;
        }
        else if (!fi.fetched)
        {
            return true;
        }
        else if ((fi.lastCheck + fi.checkInterval) < deCONZ::steadyTimeRef().ref)
        {

            return true;
        }
    }
        break;

    case deCONZ::ReqActiveEndpoints:
    {
        if (fi.checkInterval != ActiveEndpointsCheckInterval)
        {
            fi.checkInterval = ActiveEndpointsCheckInterval;
        }

        if (!fi.fetched)
        {
            return true;
        }
        else if (isEndDevice())
        {
            return false; // no periodic fetchting for end devices
        }
        else if (endpoints().empty())
        {
            return true;
        }
    }
        break;


    case deCONZ::ReqNwkAddr:
    {
        if (address().hasExt() && !address().hasNwk())
        {
            return true;
        }
        else if (!fi.fetched)
        {
            return true;
        }
    }
        break;

    case deCONZ::ReqSimpleDescriptor:
    {
        if (!fi.fetched)
        {
            return true;
        }
        else if (endpoints().size() == (uint)simpleDescriptors().size() &&
                 d_ptr->m_fetchEndpoints.empty())
        {
            return false;
        }

        return true;
    }

    case deCONZ::ReqPowerDescriptor:
        if (!powerDescriptor().isValid())
        {
            return true;
        }
        else if (!isEndDevice())
        {
            return false;
        }

        if (isEndDevice() && fi.fetched)
        {
            return false;
        }

        if (fi.checkInterval != PowerCheckInterval)
        {
            fi.checkInterval = PowerCheckInterval;
        }

        if ((fi.lastCheck + fi.checkInterval) < deCONZ::steadyTimeRef().ref)
        {
            return true;
        }
        break;

    case deCONZ::ReqMgmtLqi:
        if (isCoordinator() || isRouter())
        {
            if (!fi.fetched)
            {
                return true;
            }

            if (fi.checkInterval != MgmtLqiCheckInterval)
            {
                fi.checkInterval = MgmtLqiCheckInterval;
            }

            int64_t cur = deCONZ::steadyTimeRef().ref;

            if ((fi.lastCheck + fi.checkInterval) < cur)
            {
                return true;
            }
        }

        return false;

    default:
        break;
    }

    return !fi.fetched;
}

/*!
    Set the \p fetched state of a item.
 */
void zmNode::setFetched(deCONZ::RequestId item, bool fetched)
{
    if (item >= ReqMaxItems)
    {
        return;
    }

    FetchInfo &fi = m_fetchItems[item];

    switch (item)
    {
    case deCONZ::ReqActiveEndpoints:
    case deCONZ::ReqSimpleDescriptor:
    case deCONZ::ReqMgmtLqi:
    case deCONZ::ReqNwkAddr:
    case deCONZ::ReqIeeeAddr:
    case deCONZ::ReqMgmtBind:
    case deCONZ::ReqPowerDescriptor:
        if (fetched)
        {
            fi.lastCheck = deCONZ::steadyTimeRef().ref +  (U_rand32() % 30);
        }
        else
        {
            fi.lastCheck = 0;
        }
        break;

    default:
        break;
    }

    // force refetch of known simpledescriptors
    if (item == deCONZ::ReqSimpleDescriptor && !fetched)
    {
        for (const deCONZ::SimpleDescriptor &sd : simpleDescriptors())
        {
            auto &feps = d_ptr->m_fetchEndpoints;
            if (std::find(feps.begin(), feps.end(), sd.endpoint()) == feps.end())
            {
                feps.push_back(sd.endpoint());
            }
        }
    }

    fi.retries = 0;
    fi.fetched = fetched;

    if (fetched)
    {
        for (int i = 0; i < ReqMaxItems; i++)
        {
            FetchInfo &fi = m_fetchItems[i];
            fi.removeDependency(item);
        }
    }
}

#if 0
void zmNode::forceFetch(deCONZ::RequestId item, int delay)
{
    if (item >= ReqMaxItems)
    {
        return;
    }

    FetchInfo &fi = m_fetchItems[item];

    if (item == deCONZ::ReqSimpleDescriptor)
    {
        fi.addDependency(deCONZ::ReqActiveEndpoints);

        FetchInfo &fi1 = m_fetchItems[(int)deCONZ::ReqActiveEndpoints];
        fi1.lastCheck = 0;
        fi1.fetched = false;
        fi1.enabled = true;
    }

    fi.fetched = false;
    fi.lastCheck = deCONZ::steadyTimeRef().ref + delay;
}
#endif

bool zmNode::isFetchItemEnabled(deCONZ::RequestId item)
{
    if (item > ReqUnknown && item < ReqMaxItems)
    {
        return m_fetchItems[item].isEnabled();
    }
    return false;
}

void zmNode::setFetchItemEnabled(deCONZ::RequestId item, bool enabled)
{
    if (item > ReqUnknown && item < ReqMaxItems)
    {
        m_fetchItems[item].enabled = enabled;
        if (enabled)
        {
             m_fetchItems[item].fetched = false;
             m_fetchItems[item].lastCheck = 0;
        }
    }
}

void zmNode::checkInterval(deCONZ::RequestId item, int64_t *lastCheck, int *interval)
{
    if (item > ReqUnknown && item < ReqMaxItems)
    {
        const FetchInfo &fi = m_fetchItems[item];
        *lastCheck = fi.lastCheck;
        *interval = fi.checkInterval;
    }
    else
    {
        *lastCheck = 0;
        *interval = 0;
    }
}

RequestId zmNode::nextCurFetchItem()
{
    int cur = static_cast<int>(m_fcurItem);
    cur++;
    while (cur < ReqMaxItems)
    {
        const FetchInfo &fi = m_fetchItems[cur];
        if (fi.enabled)
        {
            break;
        }
        cur++;
    }

    m_fcurItem = static_cast<deCONZ::RequestId>(cur);
    if (m_fcurItem >= ReqMaxItems)
    {
        m_fcurItem = ReqUnknown;
    }

    return m_fcurItem;
}

} // namespace deCONZ
