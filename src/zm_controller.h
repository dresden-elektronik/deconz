/*
 * Copyright (c) 2013-2026 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_CONTROLLER_H
#define ZM_CONTROLLER_H

#include <QObject>
#include <QList>
#include <QHash>
#include <array>
#include <vector>
#include <QElapsedTimer>

#include "deconz/types.h"
#include "deconz/binding_table.h"
#include "deconz/zdp_descriptors.h"
#include "deconz/net_descriptor.h"
#include "deconz/timeref.h"
#include "common/zm_protocol.h"
#include "zm_node.h"
#include "zm_gsourceroute.h"
#include "deconz/aps.h"
#include "deconz/aps_controller.h"
#include "deconz/zcl.h"

/*!
    \class zmController

    Main controller to handle all node discovery and interaction.
 */
class zmgNode;
class NodeLink;
class zmController;
class zmMaster;
class zmNetEvent;
class zmNeighbor;
class zmNetDescriptorModel;
class zmGraphicsView;
class QAction;
class QGraphicsScene;
class QSettings;

extern QAction *readBindingTableAction;
extern QAction *readNodeDescriptorAction;
extern QAction *readActiveEndpointsAction;
extern QAction *readSimpleDescriptorsAction;
extern QAction *deleteNodeAction;
extern QAction *resetNodeAction;
extern QAction *addSourceRouteAction;
extern QAction *removeSourceRouteAction;

namespace deCONZ
{
    class SimpleDescriptor;
    class NodeInterface;
    struct Beacon;
    void notifyUser(const QString &text);
    zmController *controller();

    enum NodeKey {
        NodeKeyRequestNodeDescriptor = Qt::Key_1,
        NodeKeyRequestPowerDescriptor = Qt::Key_2,
        NodeKeyRequestNwkAddress = Qt::Key_0,
        NodeKeyRequestRouteTable = Qt::Key_R,
        NodeKeyRequestMgmtLeave = Qt::Key_L,
        NodeKeyRequestChildRejoin = Qt::Key_N,
        NodeKeyRequestNwkLeave = Qt::Key_P,
        NodeKeyRequestActiveEndpoints = Qt::Key_7,
        NodeKeyRequestSimpleDescriptors = Qt::Key_8,
        NodeKeyRequestUpdateNetwork = Qt::Key_W,
        NodeKeyRefresh = Qt::Key_Refresh,
        NodeKeyDelete = Qt::Key_Delete,
        NodeKeyDeviceAnnce = Qt::Key_A,
        NodeKeyEdScan = Qt::Key_S
    };
}

struct LinkInfo
{
    LinkInfo() : a(nullptr), b(nullptr), linkAge(0), linkLqi(0.5), linkAgeUnix(0), link(nullptr) {}
    bool isValid() const { return (a && b && link);}
    zmgNode *a;
    zmgNode *b;
    float linkAge;
    float linkLqi;
    deCONZ::SteadyTimeRef linkAgeUnix;
    NodeLink *link;

    bool operator==(const LinkInfo &rhs)
    {
        if (link != rhs.link)
        {
            return false;
        }

        if ((a == rhs.a) && (b == rhs.b))
        {
            return true;
        }
        else if ((a == rhs.a) && (b == rhs.b))
        {
            return true;
        }

        return false;
    }
};

struct BindLinkInfo
{
    BindLinkInfo() : link(nullptr) {}
    bool isValid() { return (link != nullptr); }
    deCONZ::Binding binding;
    NodeLink *link;
};

struct AddressPair
{
    AddressPair() { }
    deCONZ::Address aAddr;
    deCONZ::Address bAddr;
    deCONZ::MacCapabilities aMacCapabilities;
    deCONZ::MacCapabilities bMacCapabilities;
};

struct FastDiscover
{
    deCONZ::Address addr{};
    deCONZ::SteadyTimeRef tAnnounce;
    std::array<uint16_t, 3> clusters{};
    uint8_t clusterCount = 0;
    struct
    {
        uint8_t busy : 1;
        uint8_t done : 1;
        uint8_t errors: 6;
    };
};

enum LinkViewMode
{
    LinkShowAge,
    LinkShowLqi
};

QString createUuid(const QString &prefix);
void generateUniqueId2(uint64_t extAddress, char *buf, unsigned buflen);
void CoreNode_NotifyDeviceChanged(uint64_t mac, const char *path);

class zmController : public deCONZ::ApsController
{
    Q_OBJECT

    Q_PROPERTY(int sourceRouteMmaxHops READ sourceRouteMaxHops WRITE setSourceRouteMaxHops NOTIFY sourceRouteMaxHopsChanged)
    Q_PROPERTY(int sourceRouteMinLqi READ sourceRouteMinLqi WRITE setSourceRouteMinLqi NOTIFY sourceRouteMinLqiChanged)
    Q_PROPERTY(bool sourceRoutingEnabled READ sourceRoutingEnabled WRITE setSourceRoutingEnabled NOTIFY sourceRoutingEnabledChanged)

public:
    enum
    {
        MainTickMs = 80
    };

    explicit zmController(zmMaster *master, zmNetDescriptorModel *networks, QGraphicsScene *scene, zmGraphicsView *graph, QObject *parent = nullptr);
    ~zmController();
    int zombieCount() const { return m_zombieCount; }
    int nodeCount() const { return static_cast<int>(m_nodes.size()); }
    NodeInfo nodeAt(size_t index);
    NodeInfo nodeWithMac(uint64_t mac);
    int zclCommandRequest(const deCONZ::Address &address, deCONZ::ApsAddressMode addressMode, const deCONZ::SimpleDescriptor &simpleDescriptor, const deCONZ::ZclCluster &cluster, const deCONZ::ZclCommand &command);
    const deCONZ::SimpleDescriptor *getCompatibleEndpoint(const deCONZ::SimpleDescriptor &other);
    void setNetworkConfig(const zmNet &net, const uint8_t *items);
    void setEndpointConfig(uint8_t index, const deCONZ::SimpleDescriptor &descriptor);
    void bindReq(deCONZ::BindReq *req);
    LinkInfo *linkInfo(zmgNode *aNode, zmgNode *bNode, deCONZ::DeviceRelationship relationship);
    void checkBindingLink(const deCONZ::Binding &binding);
    void removeBindingLink(const deCONZ::Binding &binding);
    void clearAllApsRequestsToNode(NodeInfo node);
    uint8_t genSequenceNumber() { return m_genSequenceNumber++; }
    void nodeKeyPressed(uint64_t extAddr, int key);
    deCONZ::State deviceState() const { return m_devState; }
    void setDeviceState(deCONZ::State state);
    void unregisterGNode(zmgNode *gnode);
    void addNodePlugin(deCONZ::NodeInterface *plugin);
    int apsQueueSize();
    int apsdeDataRequest(const deCONZ::ApsDataRequest &req);
    int checkIdOverFlowApsDataRequest(const deCONZ::ApsDataRequest &req);
    int resolveAddress(deCONZ::Address &addr);
    deCONZ::State networkState();
    int setNetworkState(deCONZ::State state);
    int setPermitJoin(uint8_t duration);
    int getNode(int index, const deCONZ::Node **node);
    bool updateNode(const deCONZ::Node &node);
    void readParameterResponse(ZM_State_t status, ZM_DataId_t id, const uint8_t *data, uint16_t length);
    void sendDeviceAnnce();
    bool sendMatchDescriptorReq(uint16_t clusterId);
    bool sendMgmtLeaveRequest(deCONZ::zmNode *node, bool removeChildren, bool rejoin);
    bool sendNwkLeaveRequest(deCONZ::zmNode *node, bool removeChildren, bool rejoin);
    bool sendForceChildRejoin(deCONZ::zmNode *node);
    bool setParameter(deCONZ::U8Parameter parameter, uint8_t value);
    bool setParameter(deCONZ::U16Parameter parameter, uint16_t value);
    bool setParameter(deCONZ::U32Parameter parameter, uint32_t value);
    bool setParameter(deCONZ::U64Parameter parameter, uint64_t value);
    bool setParameter(deCONZ::ArrayParameter parameter, QByteArray value);
    bool setParameter(deCONZ::VariantMapParameter parameter, QVariantMap value);
    bool setParameter(deCONZ::StringParameter parameter, const QString &value);
    uint8_t getParameter(deCONZ::U8Parameter parameter);
    uint16_t getParameter(deCONZ::U16Parameter parameter);
    uint32_t getParameter(deCONZ::U32Parameter parameter);
    uint64_t getParameter(deCONZ::U64Parameter parameter);
    QString getParameter(deCONZ::StringParameter parameter);
    QByteArray getParameter(deCONZ::ArrayParameter parameter);
    QVariantMap getParameter(deCONZ::VariantMapParameter parameter, int index);
    void addSourceRoute(const std::vector<zmgNode*> gnodes);
    void removeSourceRoute(zmgNode *gnode);
    void activateSourceRoute(const deCONZ::SourceRoute &sourceRoute);
    int sourceRouteMinLqi() const { return m_sourceRouteMinLqi; }
    int sourceRouteMaxHops() const { return m_sourceRouteMaxHops; }
    bool sourceRoutingEnabled() const { return m_sourceRoutingEnabled; }
    int minLqiDisplay() const { return m_minLqiDisplay; }
    void addBinding(const deCONZ::Binding &binding);
    void removeBinding(const deCONZ::Binding &binding);
    void onApsdeDataIndication(const deCONZ::ApsDataIndication &ind);
    const deCONZ::ApsDataRequest *getApsRequest(uint id) const;
    void onApsdeDataConfirm(const deCONZ::ApsDataConfirm &confirm);

    void onNodeSelected(uint64_t mac);
    void onNodeDeselected(uint64_t mac);
    uint8_t nextRequestId();

private slots:
    void onMasterStateChanged();
    void onRestNodeUpdated(quint64 extAddress, const QString &item, const QString &value);
    void apsdeDataRequestDone(uint8_t id, uint8_t status);
    bool apsdeDataRequestQueueSetStatus(int id, deCONZ::CommonState state);
    void deviceConnected();
    void deviceDisconnected(int);
    void emitApsDataConfirm(uint8_t id, uint8_t status);
    void onMacPoll(const deCONZ::Address &address, uint32_t lifeTime);
    void onBeacon(const deCONZ::Beacon &beacon);
    void verifyChildNode(NodeInfo *node);
    void onSourceRouteChanged(const deCONZ::SourceRoute &sourceRoute);
    void onSourceRouteDeleted(const QString &uuid);
    void initSourceRouting(const QSettings &config);
    void deleteSourcesRouteWith(const deCONZ::Address &addr);

    void tick();
    void linkTick();
    void neighborTick();
    void timeoutTick();
    void fetchZdpTick();
    void zombieTick();
    void linkCreateTick();
    void bindLinkTick();
    void bindTick();
    void deviceDiscoverTick();
    void readParamTimerFired();
    bool sendNextApsdeDataRequest(NodeInfo *dst = nullptr);

Q_SIGNALS:
    void notify(const zmNetEvent&);
    void configEvent(int, quint8);
    void sourceRouteMinLqiChanged(int sourceRouteMinLqi);
    void sourceRouteMaxHopsChanged(int sourceRouteMmaxHops);    
    void sourceRoutingEnabledChanged(bool sourceRoutingEnabled);

protected:
    void timerEvent(QTimerEvent *event);

public slots:
    int getNetworkConfig();
    void loadNodesFromDb();
    void saveNodesState();
    void queueSaveNodesState();
    void saveSourceRouteConfig();
    void queueSaveSourceRouteConfig();
    void restoreNodesState();
    void toggleLqiView(bool show);
    void toggleNeighborLinks(bool show);
    bool autoFetchFFD() { return m_autoFetchFFD; }
    void setAutoFetchingFFD(bool enabled);
    bool autoFetchRFD() { return m_autoFetchRFD; }
    void setAutoFetchingRFD(bool enabled);
    bool autoFetch() { return m_autoFetch; }
    void setAutoFetching();
    void deviceState(int state);
    void sendNext();
    void sendNextLater();
    void appAboutToQuit();

    void setSourceRouteMinLqi(int sourceRouteMinLqi);
    void setSourceRouteMaxHops(int sourceRouteMmaxHops);
    void setSourceRoutingEnabled(bool sourceRoutingEnabled);
    void setFastNeighborDiscovery(bool fastDiscovery);
    void setMinLqiDisplay(int minLqi);

private:
    enum NodeRemoveMode
    {
        NodeRemoveFinally,
        NodeRemoveZombie,
        NodeRemoveHide
    };

    void addDeviceDiscover(const AddressPair &a);
    void visualizeNodeIndication(NodeInfo *node, deCONZ::Indication indication);
    void visualizeNodeChanged(NodeInfo *node, deCONZ::Indication indication);
    void checkDeviceAnnce(const deCONZ::Address &address, deCONZ::MacCapabilities macCapabilities);
    void checkAddressChange(const deCONZ::Address &address, NodeInfo *node = nullptr);
    NodeInfo createNode(const deCONZ::Address &addr, deCONZ::MacCapabilities macCapabilities);
    void fastPrope(uint64_t ext, uint16_t nwk, uint8_t macCapabilities);
    void wakeNode(NodeInfo *node);
    void deleteNode(NodeInfo *node, NodeRemoveMode finally);
    bool sendMgtmLqiRequest(NodeInfo *info);
    bool sendMgtmRtgRequest(NodeInfo *node, uint8_t startIndex);
    bool sendNodeDescriptorRequest(NodeInfo *node);
    bool sendPowerDescriptorRequest(NodeInfo *node);
    bool sendActiveEndpointsRequest(NodeInfo *node);
    bool sendUpdateNetworkRequest(NodeInfo *node);
    bool sendSimpleDescriptorRequest(NodeInfo *node, uint8_t endpoint);
    bool sendEdScanRequest(NodeInfo *node, uint32_t channels);
    bool sendZclDiscoverAttributesRequest(NodeInfo *node, const deCONZ::SimpleDescriptor &sd, uint16_t clusterId, uint16_t startAttribute);
    void zclReportAttributesIndication(NodeInfo *node, const deCONZ::ApsDataIndication &ind, const deCONZ::ZclFrame &zclFrame, deCONZ::NodeEvent &event);
    void zclReadAttributesResponse(NodeInfo *node, const deCONZ::ApsDataIndication &ind, deCONZ::ZclFrame &zclFrame, deCONZ::NodeEvent &event);
    void zclDiscoverAttributesResponse(NodeInfo *node, const deCONZ::ApsDataIndication &ind, deCONZ::ZclFrame &zclFrame);
    bool zclReadReportConfigurationResponse(NodeInfo *node, const deCONZ::ApsDataIndication &ind, const deCONZ::ZclFrame &zclFrame);

    struct LinkAge
    {
    };

    NodeInfo *getNode(const deCONZ::Address &addr, deCONZ::AddressMode mode);
    NodeInfo *getNode(deCONZ::zmNode *dnode);

    deCONZ::SteadyTimeRef m_apsGroupIndicationTimeRef;
    int m_apsGroupDelayMs = 0;
    deCONZ::ZclFrame m_zclFrame;
    bool m_otauActive;
    bool m_autoPollingActive;
    bool m_zdpUseApsAck;
    uint8_t m_fwUpdateActive;
    QTimer *m_netConfigTimer;
    QTimer *m_linkCheckTimer;
    QTimer *m_neibCheckTimer;
    QTimer *m_saveNodesTimer;
    QTimer *m_saveSourceRouteConfigTimer;
    QTimer *m_sendNextTimer;
    QTimer *m_readParamTimer;
    uint8_t m_genSequenceNumber;
    bool m_showLqi = false;
    bool m_showNeighborLinks = true;
    QElapsedTimer m_fetchLqiTickMsCounter;
    deCONZ::SteadyTimeRef m_lastDiscoveryRequest; // global NWK and IEEE requests
    deCONZ::SteadyTimeRef m_lastNodeAdded;
    deCONZ::SteadyTimeRef m_lastEndDeviceAnnounce;
    QElapsedTimer m_lastNodeDeleted;
    int m_fetchZdpDelay;
    qint64 m_fetchMgmtLqiDelay;
    int m_timer;
    int m_timeoutTimer;
    int m_otauActivity;
    int m_zombieDelay;
    int m_nodeZombieIter;
    int m_zombieCount;
    size_t m_discoverIter;
    size_t m_lqiIter;
    int m_linkIter;
    deCONZ::SteadyTimeRef m_linkUpdateTime;
    int m_neibIter;
    int m_fetchCurNode;
    bool m_waitForQueueEmpty;
    bool m_autoFetchFFD;
    bool m_autoFetchRFD;
    bool m_autoFetch;
    QString m_frameCounterKey;
    uint32_t m_frameCounter = 0;
    uint m_maxBusyApsPerNode;
    uint m_saveNodesChanges;
    deCONZ::State m_devState;
    zmMaster *m_master;
    QGraphicsScene *m_scene;

    QString m_devName;
    QByteArray m_securityMaterial0;
    std::vector<FastDiscover> m_fastDiscover;
    std::vector<NodeInfo> m_nodes;
    std::vector<NodeInfo> m_nodesDead;
    std::vector<deCONZ::SourceRoute> m_routes;
    QList<LinkInfo> m_neighbors;
    QList<LinkInfo> m_neighborsDead;
    QList<BindLinkInfo> m_bindings;
    QList<deCONZ::BindReq> m_bindQueue;
    std::vector<deCONZ::Address> m_bindLinkQueue;
    QList<AddressPair> m_deviceDiscoverQueue;
    QList<AddressPair> m_createLinkQueue;
    std::vector<deCONZ::ApsDataRequest> m_apsRequestQueue;
    std::vector<zmgSourceRoute*> m_gsourceRoutes;
    int m_apsBusyCounter;
    LinkViewMode m_linkViewMode;
    zmGraphicsView *m_graph;
    QObject *m_restPlugin;

    int m_deviceWatchdogOk;
    int m_sourceRouteMinLqi = 130;
    int m_sourceRouteMaxHops = 5;
    bool m_sourceRoutingEnabled = false;
    bool m_sourceRouteRequired = false;
    bool m_fastDiscovery = false;
    int m_minLqiDisplay = 0;
};

#endif // ZM_CONTROLLER_H
