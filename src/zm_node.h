/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_NODE_H
#define ZM_NODE_H

#include <QString>
#include <QElapsedTimer>
#include <vector>

#include "deconz/binding_table.h"
#include "deconz/node.h"
#include "deconz/ustring.h"
#include "deconz/zdp_descriptors.h"
#include "deconz/timeref.h"

/*!
    \class zmNode

    This class holds all data about a node and it's direct neighbors.


 */
class zmNeighbor;

namespace deCONZ
{
    void /*DECONZ_DLLSPEC*/ setFetchInterval(RequestId item, int interval);
    int /*DECONZ_DLLSPEC*/ getFetchInterval(RequestId item);

    struct RoutingTableEntry
    {
        uint16_t dstAddress;
        uint16_t nextHopAddress;
        uint8_t status;
        bool memConstraint;
        bool manyToOne;
        bool routeRecordRequired;
    };

class /*DECONZ_DLLSPEC*/ zmNode : public deCONZ::Node
{

public:

    enum Constant
    {
        NoCheckInterval = -1
    };

    explicit zmNode(const MacCapabilities &macCapabilities);
    ~zmNode();
    zmNode& operator=(const Node& other);

    UString extAddressString() const;

    const BindingTable &bindingTable() const { return m_bindTable; }
    BindingTable &bindingTable() { return m_bindTable; }
    const std::vector<NodeNeighbor> &neighbors() const;

    int getNextUnfetchedEndpoint() const;
    void removeFetchEndpoint(uint8_t ep);
    ZclCluster *getCluster(int endpoint, uint16_t clusterId, ZclClusterSide side);

    // discovery
    TimeMs lastDiscoveryTryMs(deCONZ::SteadyTimeRef now) const;
    void discoveryTimerReset(deCONZ::SteadyTimeRef now);

    bool updatedClusterAttribute(SimpleDescriptor *simpleDescriptor, ZclCluster *cluster, ZclAttribute *attribute);
    // basic cluster
    const QString &vendor() const { return m_vendor; }
    const QString &modelId() const { return m_modelId; }
    const QString &swVersion() const { return m_swVersion; }
    uint32_t swVersionNum() const { return m_swVersionNum; }
    void setAddress(const deCONZ::Address &addr);
    void setVendor(const QString &vendor) { m_vendor = vendor; }
    void setModelId(const QString &modelId) { m_modelId = modelId; }
    void setVersion(const QString &version);

    int battery() const { return m_battery; }
    void setBattery(int battery) { m_battery = battery; }

    // fetch helper
    bool needFetch(RequestId item);
    void setFetched(RequestId item, bool fetched);
    bool isFetchItemEnabled(RequestId item);
    void setFetchItemEnabled(RequestId item, bool enabled);
    void checkInterval(RequestId item, int64_t *lastCheck, int *interval);
    RequestId curFetchItem() const { return m_fcurItem; }
    RequestId nextCurFetchItem();
    void reset(MacCapabilities macCapabilities = MacCapabilities());
    void resetItem(deCONZ::RequestId item);
    void touch(deCONZ::SteadyTimeRef msecSinceEpoch);
    void touchAsNeighbor();
    deCONZ::SteadyTimeRef lastSeen() const { return m_lastSeen; }
    int64_t lastSeenElapsed() const { return m_lastSeenElapsed.isValid() ? m_lastSeenElapsed.elapsed() : 0; }
    int64_t lastSeenByNeighbor() const;
    deCONZ::SteadyTimeRef lastApsRequestTime() const { return m_lastApsRequest; }
    void setLastApsRequestTime(deCONZ::SteadyTimeRef time) { m_lastApsRequest = time; }

    void setZombieInternal(bool isZombie)
    {
        setIsZombie(isZombie);
        if (isZombie == false)
        {
            setState(deCONZ::IdleState);
            resetRecErrors();
        }
        else
        {
            setState(deCONZ::FailureState);
            m_lastSeen = {};
            m_lastSeenElapsed.invalidate();
        }
    }
    int retryIncr(RequestId item);
    int retryCount(RequestId item) const;
    uint8_t mgmtLqiStartIndex() const { return m_mgmtLqiStartIndex; }
    void setMgmtLqiStartIndex(uint8_t startIndex) { m_mgmtLqiStartIndex = startIndex; }
    void setMgtmLqiLastRsp(deCONZ::SteadyTimeRef time);
    Address &parentAddress() { return m_parentAddr; }
    const Address &parentAddress() const { return m_parentAddr; }
    bool updateNeighbor(const zmNeighbor &neighbor);
    bool getNeighbor(const Address &address, zmNeighbor &out);
    zmNeighbor *getNeighbor(const Address &address);
    void removeOutdatedNeighbors(int seconds);
    void removeNeighbor(const Address &address);

    void setNeedRejoin(bool needRejoin) { m_needRejoin = needRejoin; }
    bool needRejoin() const { return m_needRejoin; }

    int recvErrors() const { return m_recvErrors; }
    int recvErrorsIncrement() { return m_recvErrors++; }
    void resetRecErrors() { m_recvErrors = 0; }
    CommonState state() const;
    void setState(CommonState state);
    void setWaitState(uint timeoutSec);
    void checkWaitState();
    bool isInWaitState();
    void setHasDDF(int hasDDF) { m_hasDDF = hasDDF; }
    int hasDDF() const { return m_hasDDF; }

    std::vector<RoutingTableEntry> &routes() { return m_routes; }
    const std::vector<RoutingTableEntry> &routes() const { return m_routes; }

private:
    CommonState m_state;
    int64_t m_waitStateEnd;
    int m_recvErrors;
    QElapsedTimer m_lastSeenByNeighbor;
    deCONZ::SteadyTimeRef m_lastSeen; //!< Time then this node was last seen.
    deCONZ::SteadyTimeRef m_mgmtLqiLastRsp;
    deCONZ::SteadyTimeRef m_lastApsRequest; //!< Time then last aps request was sent.
    deCONZ::SteadyTimeRef m_lastDiscovery;
    QElapsedTimer m_lastSeenElapsed;
    uint8_t m_mgmtLqiStartIndex;
    Address m_parentAddr;
    PowerDescriptor m_powerDescr;
    // fields from basic cluster
    QString m_swVersion;
    uint32_t m_swVersionNum = 0;
    QString m_modelId;
    QString m_vendor;
    bool m_needRejoin;
    int m_hasDDF = 0;
    int m_battery = -1; // 0–100 or -1 for invalid

    struct FetchInfo
    {
        FetchInfo() : id(ReqUnknown), enabled(false), depend(0) {}
        FetchInfo(RequestId reqId, int ckInterval, int maxRetries) : id(reqId), enabled(false), fetched(false), retries(0), retriesMax(maxRetries), lastCheck(0), checkInterval(ckInterval), depend(0) {}
        bool isEnabled();
        void addDependency(RequestId id);
        void removeDependency(RequestId id);
        RequestId id;
        bool enabled;
        bool fetched;
        int retries;
        int retriesMax;
        int64_t lastCheck;
        int checkInterval;
        uint32_t depend = 0;
    };

    std::vector<zmNeighbor> m_neighbors; //!< The neighbor table.
    std::vector<NodeNeighbor> m_neighborsApi; //!< high level neighbor table
    BindingTable m_bindTable;
    std::vector<FetchInfo> m_fetchItems;
    RequestId m_fcurItem;
    std::vector<RoutingTableEntry> m_routes;
};

} // namespace deCONZ

#include <QPointF>
struct NodeInfo
{
    NodeInfo() = default;
    bool operator ==(const NodeInfo &other) { return id == other.id; }
    bool operator <(const NodeInfo &other) const;

    bool isValid() const { return (id && data && g); }
    uint32_t id = 0; //!< Internal unique id.
    deCONZ::zmNode *data = nullptr; //!< The node data.
    zmgNode *g = nullptr; //!< The QGraphicsItem representation.
    QPointF pos;
};

Q_DECLARE_METATYPE(NodeInfo)

#endif // ZM_NODE_H
