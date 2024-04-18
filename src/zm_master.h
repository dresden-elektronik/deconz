/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_MASTER_H
#define ZM_MASTER_H

#ifdef __cplusplus

#include <QVariant>
#include "deconz/types.h"
#include "deconz/aps.h"
#include "deconz/touchlink_controller.h"
#include "common/zm_protocol.h"

#ifdef Q_OS_WIN
    #define HTTP_SERVER_PORT 80
#else
    #define HTTP_SERVER_PORT 8080
#endif

/*!
    \brief The master is communication partner to the device.
 */
struct zm_command;
class zmNeighbor;
class zmNode;
class zmMaster;
class zmQueItem;
class QTcpServer;
class QTcpSocket;
class QTimer;
class SerialCom;

namespace deCONZ {
    zmMaster *master();
    class HttpServer;
    class HttpClientHandler;

    enum DeviceDisconnectReason
    {
        DeviceDisconnectFromMaster,
        DeviceDisconnectNormal,
        DeviceDisconnectNoPermisson,
        DeviceDisconnectTimeout,
        DeviceDisconnectIoError,
        DeviceDisconnectBootloaderOnly
    };

    struct Beacon {
        quint16 source;
        quint16 panId;
        quint8 channel;
        quint8 flags;
        quint8 updateId;
    };

    struct NwkLeaveRequest
    {
        quint16 flags = 0;
        quint16 dstAddress = 0;
    };
}

/*!
    \class zmNetEvent

    A event send from zmMaster if data from the device arrived.

    The data associated with a event is described in the deCONZ::NetEvent
    enum.
 */
class zmNetEvent
{
public:
    zmNetEvent() : m_node(0), m_cluster(0), m_type(deCONZ::UnknownEvent), m_cookie(0), m_listIndex(0), m_listSize(0) { }
    /*! The event type. */
    deCONZ::NetEvent type() const { return m_type; }
    void setType(deCONZ::NetEvent type) { m_type = type; }
    /*! The address of interrest. */
    deCONZ::Address &address() { return m_addr; }
    const deCONZ::Address &address() const { return m_addr; }
    void setValue(const QVariant &value) { m_value = value; }
    /*! Generic data. */
    const QVariant &value() const { return m_value; }
    /*! The Node */
    deCONZ::zmNode *node() const { return m_node; }
    void setNode(deCONZ::zmNode *node) { m_node = node; }
    /*! The cluster-id. */
    quint16 cluster() const { return m_cluster; }
    void setCluster(quint16 cluster) { m_cluster = cluster; }
    uint16_t cookie() const { return m_cookie; }
    void setCookie(uint16_t cookie) { m_cookie = cookie; }
    int listSize() const { return m_listSize; }
    void setListSize(int size) { m_listSize = size; }
    int listIndex() const { return m_listIndex; }
    void setListIndex(int index) { m_listIndex = index; }

private:
    QVariant m_value;
    deCONZ::zmNode *m_node;
    quint16 m_cluster;
    deCONZ::Address m_addr;
    deCONZ::NetEvent m_type;
    uint16_t m_cookie;
    int m_listIndex;
    int m_listSize;
};

/*!
    \class zmMaster

    Low level connection to the device.

    The master will send a zmNetEvent thenever new data is available.
    It also forwards requests to the device.
 */
class zmMaster : public deCONZ::TouchlinkController
{
    Q_OBJECT

public:

    enum MasterState
    {
        MASTER_OFF    = 'O',
        MASTER_CONNECTING = 'C',
        MASTER_IDLE   = 'I'
    };

    enum MasterEvent
    {
        ACTION_PROCESS    = 0,
        EVENT_GOT_DATA    = 1,
        EVENT_GOT_STATUS  = 2,
        EVENT_TIMEOUT     = 3,
        EVENT_ITEM_ADDED  = 4
    };

    explicit zmMaster(QObject *parent = 0);
    ~zmMaster();
    int openSerial(const QString &port, int baudrate);
    int apsdeDataRequest(const deCONZ::ApsDataRequest &aps);
    int nwkLeaveRequest(const deCONZ::NwkLeaveRequest &req);

    // touchlink
    int startInterpanMode(uint8_t channel);
    int sendInterpanRequest(const deCONZ::TouchlinkRequest &req);

    quint16 httpServerPort() const;
    const QString &httpServerRoot() const;
    int registerHttpClientHandler(deCONZ::HttpClientHandler *handler);
    int firmwareVersionRequest();
    int unlockMaxNodes();
    uint16_t maxNodes() const { return m_maxNodes; }

    /*!
        \internal
     */
    void processPacked(const zm_command *cmd);
    void comExit();
    bool connected();
    bool isOpen();
    void taskHandler(MasterEvent event);
    void handleStateIdle(MasterEvent event);
    void handleEventGotAck();
    void handleTimeouts();
    void apsSendFailed(int id);
    deCONZ::State netState();
    bool hasFreeApsRequest();
    uint16_t deviceProtocolVersion() const { return m_devProtocolVersion; }
    uint32_t deviceFirmwareVersion() const { return m_devFirmwareVersion; }
    const QString &devicePath() const;
    const QString &deviceName() const;
    void startTaskTimer(MasterEvent event, int interval, int line);

Q_SIGNALS:
    void deviceConnected();
    void deviceDisconnected(int);
    void deviceState();
    void deviceActivity();
    void deviceStateTimeOut();
    void apsdeDataIndication(const deCONZ::ApsDataIndication&);
    void apsdeDataConfirm(const deCONZ::ApsDataConfirm&);
    void commandQueueEmpty();
    void apsdeDataRequestDone(uint8_t id, uint8_t status);
    void writeParameterDone(uint8_t id, uint8_t status);
    void changeNetStateDone(uint8_t status);
    void netStateChanged();
    void netStartDone(uint8_t zdoStatus);
    /*! Is emitted after a parameter was received from device and updated in internal storage.
        \param parameter a ZM_DataId_t identifier
     */
    void parameterUpdated(int parameter);
    void macPoll(deCONZ::Address address, quint32 lifeTime);
    void beacon(const deCONZ::Beacon&);

    // touchlink
    void startInterpanModeConfirm(deCONZ::TouchlinkStatus);
    void sendInterpanConfirm(deCONZ::TouchlinkStatus);
//    void interpanIndication(const QByteArray &);

public slots:
    int rebootDevice();
    void factoryReset();
    int resetDeviceWatchdog(quint32 ttl);
    int readParameters();
    int readParameter(ZM_DataId_t id);
    int readParameterWithArg(ZM_DataId_t id, const uint8_t *data, uint8_t length);
    int writeParameter(ZM_DataId_t id, const uint8_t *data, uint8_t length);
    int verifyChildNode(const deCONZ::Address &address, quint8 macCapabilities);
    int forceRejoinChildNode(const deCONZ::Address &address);
    void joinNetwork();
    void leaveNetwork();
    void onDeviceConnected();
    void onDeviceDisconnected(int reason);
    void sendNextCommand();
    void taskTimerFired();
//    void readRegisterTimerFired();

private:
    void initSerialCom();
    void processQueue();
    void queNextApsDataRequest();
    int queApsDataConfirm();
    int queApsDataIndication();
    void queInterpanDataIndication();
    void queInterpanDataConfirm();
    void queGetStartNetworkConfirmStatus();
    void checkStatus0(const uint8_t *status);
    void checkStatus1(const uint8_t *status);
    void killCommand(const struct zm_command *cmd, ZM_State_t state);
    void setState(MasterState state);

private Q_SLOTS:
    void killCommandQueue();
    void bootloaderStarted();
    void appAboutToQuit();

protected:
    void timerEvent(QTimerEvent *event);

private:
    int m_bootloaderStarted;
    int m_timeoutTimer = -1;
    int m_packetCounter;
    deCONZ::ApsDataIndication m_ind;
    int m_readParamCount;
    quint16 m_httpServerPort;
    deCONZ::HttpServer *m_httpServer;
    uint16_t m_maxNodes;
    MasterEvent m_taskTimerEvent;
    int m_taskTimerLine;
    QTimer *m_taskTimer;
    uint16_t m_devProtocolVersion;
    uint32_t m_devFirmwareVersion;
    QString m_devName;
    QString m_emptyString;
    QString m_serialPort;
};

#endif
#endif // ZM_MASTER_H
