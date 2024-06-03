/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QCoreApplication>
#include <QGraphicsScene>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QSettings>
#include <QUuid>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>

#include "actor/service.h"
#include "actor/cxx_helper.h"

#include "aps_private.h"
#include "deconz/buffer_helper.h"
#include "deconz/dbg_trace.h"
#include "deconz/node_event.h"
#include "deconz/util.h"
#include "deconz/zdp_descriptors.h"
#include "deconz/zdp_profile.h"
#include "deconz/green_power_controller.h"
#include "deconz/u_assert.h"
#include "deconz/u_sstream.h"
#include "source_routing.h"
#include "db_nodes.h"
#include "zcl_private.h"
#include "zm_app.h"
#include "zm_binddropbox.h"
#include "zm_controller.h"
#include "zm_cluster_info.h"
#include "zm_gnode.h"
#include "zm_glink.h"
#include "zm_gsocket.h"
#include "zm_gsourceroute.h"
#include "zm_graphicsview.h"
#include "zm_master.h"
#include "zm_neighbor.h"
#include "zm_netdescriptor_model.h"
#include "zm_netedit.h"
#include "zm_node.h"
#include "zm_node_model.h"
#include "zm_global.h"

//! plugin interfaces
#include "deconz/node_interface.h"

#define NODE_ADDED_ZOMBIE_DELAY (60 * 1000)
#define MAX_ZOMBIE_DELAY (60 * 60 * 1000)
#define DEVICE_TTL_RESET (60 * 120) // 120 minutes
#define DEVICE_TTL_RESET_THRESHOLD 600 // reset watchdog if all ok and ttl below threshold

#define DEVICE_ZDP_LOOPBACK_OK     0x0001
#define DEVICE_RX_NETWORK_OK       0x0002
#define DEVICE_CONFIG_NETWORK_OK   0x0004

#define DEVICE_ALL_OK (DEVICE_ZDP_LOOPBACK_OK | DEVICE_RX_NETWORK_OK | DEVICE_CONFIG_NETWORK_OK)

#define GREEN_POWER_PROFILE_ID  0xa1e0
#define GREEN_POWER_CLUSTER_ID  0x0021
#define GREEN_POWER_ENDPOINT    0xf2

using namespace deCONZ;

#define AM_ACTOR_ID_CORE_APS    2005
#define AM_ACTOR_ID_CORE_NET    2006

enum CommonMessageIds
{
   M_ID_LIST_DIR_REQ = AM_MESSAGE_ID_COMMON_REQUEST(1),
   M_ID_LIST_DIR_RSP = AM_MESSAGE_ID_COMMON_RESPONSE(1),
   M_ID_READ_ENTRY_REQ = AM_MESSAGE_ID_COMMON_REQUEST(2),
   M_ID_READ_ENTRY_RSP = AM_MESSAGE_ID_COMMON_RESPONSE(2)
};

// provide global access
deCONZ::NodeModel *_nodeModel = 0;
zmNetDescriptorModel *_netModel = 0;
deCONZ::SteadyTimeRef m_steadyTimeRef;

static zmController *_apsCtrl = nullptr;
static size_t tickCounter = 0;
#ifdef USE_ACTOR_MODEL
static struct am_api_functions *am = nullptr;
static struct am_actor am_actor_core_net;
static struct am_actor am_actor_core_aps;

static uint64_t aps_frames_tx = 0;
static uint64_t aps_frames_rx = 0;
#endif

// manufacturer codes
// http://cgit.osmocom.org/wireshark/plain/epan/dissectors/packet-zbee.h
#define VENDOR_PHILIPS      0x100B
#define VENDOR_DDEL         0x1135
#define VENDOR_115F         0x115F // Used by Xiaomi
#define VENDOR_IKEA         0x117C

const uint64_t macPrefixMask   = 0xffffff0000000000ULL;
const uint64_t deMacPrefix     = 0x00212e0000000000ULL;
const uint64_t jennicMacPrefix = 0x00158d0000000000ULL;


namespace {
    const int NetConfigFetchDelay = 2 * 1000;
    const int LinkCheckInterval = 1080;
    const int NeibCheckInterval = 5109;
    const int SaveNodeTimerInterval = 1000 * 60 * 10;
    constexpr deCONZ::TimeSeconds ZombieDelta{1800}; // s
    constexpr deCONZ::TimeSeconds ZombieDeltaEndDevice{60 * 60 * 4}; // s
    const uint MaxLinkAge = 60 * 60 * 8; // s
    const int MaxApsRequestsZdp = 2;
    const int MaxApsRequests = 24;
    const int MaxApsBusyRequests = 6;
    const int TickMs = zmController::MainTickMs;
    const int MaxRecvErrors = 5;
    const int MaxRecvErrorsZombie = 10;
    constexpr deCONZ::TimeSeconds MaxTimeOut{60};
    constexpr deCONZ::TimeSeconds MaxConfirmedTimeOut{10};
    const int MaxZdpTimeout = 3;
    const int MinGroupDelay = 50;
    const int MaxGroupDelay = 300;
    constexpr deCONZ::TimeSeconds ZombieDiscoveryEmptyInterval{60}; // 60 s
    constexpr deCONZ::TimeSeconds ZombieDiscoveryInterval{60}; // 60 s
    constexpr deCONZ::TimeSeconds MaxZombieDiscoveryInterval{60 * 30}; // 30 min
}

bool NodeInfo::operator <(const NodeInfo &other) const
{
    if (data && other.data)
    {
        return data->address().ext() < other.data->address().ext();
    }

    return data ? true : false;
}

namespace deCONZ {


zmController *controller()
{
    return static_cast<zmController*>(deCONZ::ApsController::instance());
}

NodeModel *nodeModel()
{
    return _nodeModel;
}

zmNetDescriptorModel *netModel()
{
    return _netModel;
}

}

QString createUuid(const QString &prefix)
{
    return prefix + QUuid::createUuid().toString().remove('{').remove('}');
}

int APS_RequestsBusyCount(const std::vector<deCONZ::ApsDataRequest> &queue)
{
    int result = 0;

    for (const auto &req : queue)
    {
        if (req.state() == deCONZ::BusyState)
        {
            if (!req.confirmed())
            {
                result++;
            }
        }
    }

    return result;
}

/* Bit 0 Access */
#define AM_ENTRY_MODE_READONLY 0
#define AM_ENTRY_MODE_WRITEABLE 1

/* Bit 16-19 Display */
#define AM_ENTRY_MODE_DISPLAY_AUTO (0U << 16)
#define AM_ENTRY_MODE_DISPLAY_HEX  (1U << 16)
#define AM_ENTRY_MODE_DISPLAY_BIN  (2U << 16)

static int CoreNet_ListDirectoryRequest(struct am_message *msg)
{
    unsigned i;
    struct am_message *m;

    unsigned short tag;
    am_string url;
    unsigned req_index;

    tag = am->msg_get_u16(msg);
    url = am->msg_get_string(msg);
    req_index = am->msg_get_u32(msg);

    /* end of parsing */

    if (msg->status != AM_MSG_STATUS_OK)
        return AM_CB_STATUS_INVALID;

    m = am->msg_alloc();
    if (!m)
        return AM_CB_STATUS_MESSAGE_ALLOC_FAILED;

    am->msg_put_u16(m, tag);

    uint32_t mode = 0;

    if (url.size == 0 && req_index == 0)
    {
        /*
         * root directory
         */
        am->msg_put_u8(m, AM_RESPONSE_STATUS_OK);
        am->msg_put_cstring(m, "");
        am->msg_put_u32(m, req_index);
        am->msg_put_u32(m, 0); /* no next index */

        am->msg_put_u32(m, 2); /* count */
        /*************************************/
        am->msg_put_cstring(m, "net");
        am->msg_put_cstring(m, "dir");
        am->msg_put_u32(m, mode); /* mode */

        am->msg_put_cstring(m, ".actor");
        am->msg_put_cstring(m, "dir");
        am->msg_put_u32(m, mode); /* mode */
    }
    else if (url == ".actor" && req_index == 0)
    {
        am->msg_put_u8(m, AM_RESPONSE_STATUS_OK);
        am->msg_put_string(m, url.data, url.size);
        am->msg_put_u32(m, req_index);
        am->msg_put_u32(m, 0); /* no next index */

        am->msg_put_u32(m, 1); /* count */
        /*************************************/
        am->msg_put_cstring(m, "name");
        am->msg_put_cstring(m, "str");
        am->msg_put_u32(m, mode); /* mode */
    }
    else if (url == "net" && req_index == 0)
    {
        am->msg_put_u8(m, AM_RESPONSE_STATUS_OK);
        am->msg_put_string(m, url.data, url.size);
        am->msg_put_u32(m, req_index);
        am->msg_put_u32(m, 0); /* no next index */

        am->msg_put_u32(m, 1); /* count */
        /*************************************/
        am->msg_put_cstring(m, "0");
        am->msg_put_cstring(m, "dir");
        am->msg_put_u32(m, mode); /* mode */
    }
    else if (url == "net/0" && req_index == 0)
    {
        const struct fix_entries
        {
            const char *name;
            const char *type;
            uint32_t mode;
        } fix_entries[] = {
            { "channel_mask",       "u32",  AM_ENTRY_MODE_WRITEABLE | AM_ENTRY_MODE_DISPLAY_HEX },
            { "device_type",        "str",  AM_ENTRY_MODE_WRITEABLE },
            { "ext_panid",          "u64",  AM_ENTRY_MODE_WRITEABLE | AM_ENTRY_MODE_DISPLAY_HEX },
            { "mac_address",        "u64",  AM_ENTRY_MODE_WRITEABLE | AM_ENTRY_MODE_DISPLAY_HEX },
            { "network_key",        "blob", AM_ENTRY_MODE_WRITEABLE },
            { "nwk_address",        "u16",  AM_ENTRY_MODE_WRITEABLE | AM_ENTRY_MODE_DISPLAY_HEX },
            { "nwk_updateid",       "u8",   AM_ENTRY_MODE_WRITEABLE },
            { "panid",              "u16",  AM_ENTRY_MODE_WRITEABLE | AM_ENTRY_MODE_DISPLAY_HEX },
            { "predefined_panid",   "u8",   AM_ENTRY_MODE_WRITEABLE },
            { "security_mode",      "u8",   AM_ENTRY_MODE_WRITEABLE },
            { "static_nwk_address", "u8",   AM_ENTRY_MODE_WRITEABLE },
            { "tc_address",         "u64",  AM_ENTRY_MODE_WRITEABLE | AM_ENTRY_MODE_DISPLAY_HEX },
            { "tc_link_key",        "blob", AM_ENTRY_MODE_WRITEABLE },
            { "use_ext_panid",      "u64",  AM_ENTRY_MODE_WRITEABLE | AM_ENTRY_MODE_DISPLAY_HEX }
        };

        const unsigned count = sizeof(fix_entries) / sizeof(fix_entries[0]);

        am->msg_put_u8(m, AM_RESPONSE_STATUS_OK);
        am->msg_put_string(m, url.data, url.size);
        am->msg_put_u32(m, req_index);
        am->msg_put_u32(m, 0); /* no next index */

        am->msg_put_u32(m, count);

        for (i = 0; i < count; i++)
        {
            am->msg_put_cstring(m, fix_entries[i].name);
            am->msg_put_cstring(m, fix_entries[i].type);
            am->msg_put_u32(m, fix_entries[i].mode);
        }
    }
    else
    {
        am->msg_put_u8(m, AM_RESPONSE_STATUS_NOT_FOUND);
    }

    m->src = msg->dst;
    m->dst = msg->src;
    m->id = M_ID_LIST_DIR_RSP;
    am->send_message(m);

    return AM_CB_STATUS_OK;
}

static int CoreNet_ReadEntryRequest(struct am_message *msg)
{
    struct am_message *m;
    U_SStream ss;

    uint16_t tag;
    am_string url;

    uint32_t mode = AM_ENTRY_MODE_WRITEABLE;
    uint64_t mtime = 0;

    tag = am->msg_get_u16(msg);
    url = am->msg_get_string(msg);

    if (msg->status != AM_MSG_STATUS_OK)
        return AM_CB_STATUS_INVALID;

    m = am->msg_alloc();
    if (!m)
        return AM_CB_STATUS_MESSAGE_ALLOC_FAILED;

    am->msg_put_u16(m, tag);


    U_sstream_init(&ss, url.data, url.size);

    am->msg_put_u8(m, AM_RESPONSE_STATUS_OK);
    am->msg_put_string(m, url.data, url.size);

    if (U_sstream_starts_with(&ss, "net/0"))
    {
        const zmNet &net = _netModel->currentNetwork();

        if (url == "net/0/channel_mask")
        {
            am->msg_put_cstring(m, "u32");
            am->msg_put_u32(m, mode | AM_ENTRY_MODE_DISPLAY_HEX);
            am->msg_put_u64(m, mtime);
            am->msg_put_u32(m, net.channelMask());
        }
        else if (url == "net/0/device_type")
        {
            am->msg_put_cstring(m, "str");
            am->msg_put_u32(m, mode);
            am->msg_put_u64(m, mtime);
            if (net.deviceType() == deCONZ::Coordinator)
                am->msg_put_cstring(m, "coordinator");
            else
                am->msg_put_cstring(m, "router");
        }
        else if (url == "net/0/ext_panid")
        {
            am->msg_put_cstring(m, "u64");
            am->msg_put_u32(m, mode | AM_ENTRY_MODE_DISPLAY_HEX);
            am->msg_put_u64(m, mtime);
            am->msg_put_u64(m, net.pan().ext());
        }
        else if (url == "net/0/mac_address")
        {
            am->msg_put_cstring(m, "u64");
            am->msg_put_u32(m, mode | AM_ENTRY_MODE_DISPLAY_HEX);
            am->msg_put_u64(m, mtime);
            am->msg_put_u64(m, net.ownAddress().ext());
        }
        else if (url == "net/0/network_key" && net.networkKey().size() == 16)
        {
            am->msg_put_cstring(m, "blob");
            am->msg_put_u32(m, mode);
            am->msg_put_u64(m, mtime);
            am->msg_put_blob(m, (unsigned)net.networkKey().size(), (unsigned char*)net.networkKey().data());
        }
        else if (url == "net/0/nwk_address")
        {
            am->msg_put_cstring(m, "u16");
            am->msg_put_u32(m, mode | AM_ENTRY_MODE_DISPLAY_HEX);
            am->msg_put_u64(m, mtime);
            am->msg_put_u16(m, net.ownAddress().nwk());
        }
        else if (url == "net/0/nwk_updateid")
        {
            am->msg_put_cstring(m, "u8");
            am->msg_put_u32(m, mode);
            am->msg_put_u64(m, mtime);
            am->msg_put_u8(m, net.nwkUpdateId());
        }
        else if (url == "net/0/panid")
        {
            am->msg_put_cstring(m, "u16");
            am->msg_put_u32(m, mode | AM_ENTRY_MODE_DISPLAY_HEX);
            am->msg_put_u64(m, mtime);
            am->msg_put_u16(m, net.pan().nwk());
        }
        else if (url == "net/0/predefined_panid")
        {
            am->msg_put_cstring(m, "u8");
            am->msg_put_u32(m, mode);
            am->msg_put_u64(m, mtime);
            am->msg_put_u8(m, net.predefinedPanId() ? 1 : 0);
        }
        else if (url == "net/0/security_mode")
        {
            am->msg_put_cstring(m, "u8");
            am->msg_put_u32(m, mode);
            am->msg_put_u64(m, mtime);
            am->msg_put_u8(m, net.securityMode());
        }
        else if (url == "net/0/static_nwk_address")
        {
            am->msg_put_cstring(m, "u8");
            am->msg_put_u32(m, mode);
            am->msg_put_u64(m, mtime);
            am->msg_put_u8(m, net.staticAddress() ? 1 : 0);
        }
        else if (url == "net/0/tc_address")
        {
            am->msg_put_cstring(m, "u64");
            am->msg_put_u32(m, mode | AM_ENTRY_MODE_DISPLAY_HEX);
            am->msg_put_u64(m, mtime);
            am->msg_put_u64(m, net.trustCenterAddress().ext());
        }
        else if (url == "net/0/tc_link_key" && net.trustCenterLinkKey().size() == 16)
        {
            am->msg_put_cstring(m, "blob");
            am->msg_put_u32(m, mode);
            am->msg_put_u64(m, mtime);
            am->msg_put_blob(m, (unsigned)net.trustCenterLinkKey().size(), (unsigned char*)net.trustCenterLinkKey().data());
        }
        else if (url == "net/0/use_ext_panid")
        {
            am->msg_put_cstring(m, "u64");
            am->msg_put_u32(m, mode | AM_ENTRY_MODE_DISPLAY_HEX);
            am->msg_put_u64(m, mtime);
            am->msg_put_u64(m, net.panAps().ext());
        }
        else
        {
            m->pos = 0;
        }
    }
    else if (U_sstream_starts_with(&ss, ".actor/"))
    {
        if (url == ".actor/name")
        {
            am->msg_put_cstring(m, "str");
            am->msg_put_u32(m, mode);
            am->msg_put_u64(m, mtime);
            am->msg_put_cstring(m, "core/net");
        }
        else
        {
            m->pos = 0;
        }
    }
    else
    {
        m->pos = 0;
    }

    if (m->pos == 0)
    {
        am->msg_put_u16(m, tag);
        am->msg_put_u8(m, AM_RESPONSE_STATUS_NOT_FOUND);
    }

    m->src = msg->dst;
    m->dst = msg->src;
    m->id = M_ID_READ_ENTRY_RSP;
    am->send_message(m);

    return AM_CB_STATUS_OK;
}

static int CoreAps_ListDirectoryRequest(struct am_message *msg)
{
    struct am_message *m;

    unsigned short tag;
    am_string url;
    unsigned req_index;

    tag = am->msg_get_u16(msg);
    url = am->msg_get_string(msg);
    req_index = am->msg_get_u32(msg);

    /* end of parsing */

    if (msg->status != AM_MSG_STATUS_OK)
        return AM_CB_STATUS_INVALID;

    m = am->msg_alloc();
    if (!m)
        return AM_CB_STATUS_MESSAGE_ALLOC_FAILED;

    am->msg_put_u16(m, tag);

    uint32_t mode = 0;

    if (url.size == 0 && req_index == 0)
    {
        /*
         * root directory
         */
        am->msg_put_u8(m, AM_RESPONSE_STATUS_OK);
        am->msg_put_cstring(m, "");
        am->msg_put_u32(m, req_index);
        am->msg_put_u32(m, 0); /* no next index */

        am->msg_put_u32(m, 2); /* count */
        /*************************************/
        am->msg_put_cstring(m, "frames_rx");
        am->msg_put_cstring(m, "u64");
        am->msg_put_u32(m, mode); /* mode */

        am->msg_put_cstring(m, "frames_tx");
        am->msg_put_cstring(m, "u64");
        am->msg_put_u32(m, mode); /* mode */
    }
    else
    {
        am->msg_put_u8(m, AM_RESPONSE_STATUS_NOT_FOUND);
    }

    m->src = msg->dst;
    m->dst = msg->src;
    m->id = M_ID_LIST_DIR_RSP;
    am->send_message(m);

    return AM_CB_STATUS_OK;
}

static int CoreAps_ReadEntryRequest(struct am_message *msg)
{
    struct am_message *m;

    uint16_t tag;
    am_string url;

    uint32_t mode = AM_ENTRY_MODE_WRITEABLE;
    uint64_t mtime = 0;

    tag = am->msg_get_u16(msg);
    url = am->msg_get_string(msg);

    if (msg->status != AM_MSG_STATUS_OK)
        return AM_CB_STATUS_INVALID;

    m = am->msg_alloc();
    if (!m)
        return AM_CB_STATUS_MESSAGE_ALLOC_FAILED;

    am->msg_put_u16(m, tag);
    am->msg_put_u8(m, AM_RESPONSE_STATUS_OK);
    am->msg_put_string(m, url.data, url.size);

    if (url == "frames_rx")
    {
        am->msg_put_cstring(m, "u64");
        am->msg_put_u32(m, mode);
        am->msg_put_u64(m, mtime);
        am->msg_put_u64(m, aps_frames_rx);
    }
    else if (url == "frames_tx")
    {
        am->msg_put_cstring(m, "u64");
        am->msg_put_u32(m, mode);
        am->msg_put_u64(m, mtime);
        am->msg_put_u64(m, aps_frames_tx);
    }
    else if (url == ".actor/name")
    {
        am->msg_put_cstring(m, "str");
        am->msg_put_u32(m, mode);
        am->msg_put_u64(m, mtime);
        am->msg_put_cstring(m, "core/aps");
    }
    else
    {
        m->pos = 0;
    }

    if (m->pos == 0)
    {
        am->msg_put_u16(m, tag);
        am->msg_put_u8(m, AM_RESPONSE_STATUS_NOT_FOUND);
    }

    m->src = msg->dst;
    m->dst = msg->src;
    m->id = M_ID_READ_ENTRY_RSP;
    am->send_message(m);

    return AM_CB_STATUS_OK;
}

// TODO(mpi): put in own module
static int CoreNet_MessageCallback(struct am_message *msg)
{
    if (msg->id == M_ID_READ_ENTRY_REQ)
        return CoreNet_ReadEntryRequest(msg);

    if (msg->id == M_ID_LIST_DIR_REQ)
        return CoreNet_ListDirectoryRequest(msg);

    DBG_Printf(DBG_INFO, "core/net: msg from: %u\n", msg->src);
    return AM_CB_STATUS_UNSUPPORTED;
}

static int CoreAps_MessageCallback(struct am_message *msg)
{
    if (msg->id == M_ID_READ_ENTRY_REQ)
        return CoreAps_ReadEntryRequest(msg);

    if (msg->id == M_ID_LIST_DIR_REQ)
        return CoreAps_ListDirectoryRequest(msg);

    DBG_Printf(DBG_INFO, "core/aps: msg from: %u\n", msg->src);
    return AM_CB_STATUS_UNSUPPORTED;
}


zmController::zmController(zmMaster *master,
                           zmNetDescriptorModel *networks,
                           QGraphicsScene *scene,
                           zmGraphicsView *graph,
                           QObject *parent) :
    deCONZ::ApsController(parent),
    m_master(master),
    m_scene(scene),
    m_graph(graph)
{
#ifdef USE_ACTOR_MODEL
    am = AM_ApiFunctions();
    AM_INIT_ACTOR(&am_actor_core_net, AM_ACTOR_ID_CORE_NET, CoreNet_MessageCallback);
    AM_INIT_ACTOR(&am_actor_core_aps, AM_ACTOR_ID_CORE_APS, CoreAps_MessageCallback);

    am->register_actor(&am_actor_core_net);
    am->register_actor(&am_actor_core_aps);
#endif

    QString configPath = deCONZ::getStorageLocation(deCONZ::ConfigLocation);
    QSettings config(configPath, QSettings::IniFormat);

    {
        QString sqliteDatabaseName = deCONZ::getStorageLocation(deCONZ::ApplicationsDataLocation) + QLatin1String("/zll.db");
        std::vector<QString> locations = { configPath, sqliteDatabaseName };

        for (const QString &loc : locations)
        {
            const QFileInfo fi(loc);

            if (!fi.exists())
            {
                DBG_Printf(DBG_INFO, "Warning: %s doesn't exists\n", qPrintable(loc));
            }
            else if (!fi.isWritable())
            {
                DBG_Printf(DBG_INFO, "Warning: %s not writeable (please check file permissions)\n", qPrintable(loc));
            }
            else
            {
                DBG_Printf(DBG_INFO, "%s exists and is writeable\n", qPrintable(loc));
            }
        }
    }

    m_restPlugin = 0;
    m_apsBusyCounter = 0;
    m_zdpUseApsAck = (deCONZ::appArgumentNumeric("--zdp-aps-ack", 1) == 1) ? true : false;
    m_otauActive = false;
    m_otauActivity = 0;
    m_autoPollingActive = true;
    m_fwUpdateActive = FirmwareUpdateIdle;

    m_deviceWatchdogOk = 0;
    m_fetchZdpDelay = 500;
    m_fetchMgmtLqiDelay = deCONZ::appArgumentNumeric("--mgtmlqi-delay", 3000);
    m_fetchLqiTickMsCounter.start();
    m_showLqi = false;
    m_apsGroupDelayMs = MinGroupDelay;

    initSourceRouting(config);

    _netModel = networks;
    m_devState = deCONZ::NotInNetwork;

    // create ZCL database
    deCONZ::ZclDataBase *zclDb = deCONZ::zclDataBase();
    Q_UNUSED(zclDb);
    _nodeModel = new deCONZ::NodeModel(this);

    m_fetchCurNode = 0;
    m_linkViewMode = LinkShowLqi;

    m_netConfigTimer = new QTimer(this);
    m_netConfigTimer->setInterval(NetConfigFetchDelay);
    m_netConfigTimer->setSingleShot(true);

    connect(m_netConfigTimer, SIGNAL(timeout()),
            this, SLOT(getNetworkConfig()));

    m_linkCheckTimer = new QTimer(this);
    m_linkCheckTimer->setInterval(LinkCheckInterval);
    m_linkCheckTimer->setSingleShot(false);
    connect(m_linkCheckTimer, SIGNAL(timeout()),
            this, SLOT(linkTick()));
    m_linkCheckTimer->start();

    m_neibCheckTimer = new QTimer(this);
    m_neibCheckTimer->setInterval(NeibCheckInterval);
    m_neibCheckTimer->setSingleShot(false);
    connect(m_neibCheckTimer, SIGNAL(timeout()),
            this, SLOT(neighborTick()));
    m_neibCheckTimer->start();

    m_saveNodesTimer = new QTimer(this);
    m_saveNodesTimer->setInterval(SaveNodeTimerInterval);
    m_saveNodesTimer->setSingleShot(false);
    connect(m_saveNodesTimer, SIGNAL(timeout()),
            this, SLOT(saveNodesState()));
    m_saveNodesTimer->start();

    m_sendNextTimer = new QTimer(this);
    m_sendNextTimer->setInterval(50);
    m_sendNextTimer->setSingleShot(true);
    connect(m_sendNextTimer, SIGNAL(timeout()),
            this, SLOT(sendNext()));

    m_readParamTimer = new QTimer(this);
    m_readParamTimer->setInterval(60 * 1000);
    m_readParamTimer->setSingleShot(false);
    connect(m_readParamTimer, SIGNAL(timeout()),
            this, SLOT(readParamTimerFired()));
    m_readParamTimer->start();

    m_maxBusyApsPerNode = 2;

    m_autoFetch = true;
    m_autoFetchFFD = true;
    m_autoFetchRFD = false;
    m_saveNodesChanges = 0;
    m_genSequenceNumber = 0;
    m_linkIter = 0;
    m_neibIter = 0;
    m_nodeZombieIter = 0;
    m_zombieCount = 0;
    m_zombieDelay = 0;
    m_timer = startTimer(TickMs);
    m_timeoutTimer = startTimer(TickMs);

    connect(m_master, &zmMaster::macPoll, this, &zmController::onMacPoll);

    connect(m_master, &zmMaster::beacon, this, &zmController::onBeacon);

    // setup queue process
    connect(m_master, SIGNAL(commandQueueEmpty()),
            this, SLOT(sendNext()));

    connect(m_master, SIGNAL(deviceConnected()),
            this, SLOT(deviceConnected()));

    connect(m_master, SIGNAL(deviceDisconnected(int)),
            this, SLOT(deviceDisconnected(int)));

    connect(m_master, SIGNAL(apsdeDataRequestDone(uint8_t,uint8_t)),
            this, SLOT(apsdeDataRequestDone(uint8_t,uint8_t)));

    connect(this, &zmController::sourceRouteChanged, this, &zmController::onSourceRouteChanged);
    connect(this, &zmController::sourceRouteDeleted, this, &zmController::onSourceRouteDeleted, Qt::QueuedConnection);

    // cleanup handler
    connect(QCoreApplication::instance(), SIGNAL(aboutToQuit()),
            this, SLOT(appAboutToQuit()));

    _apsCtrl = this;
}

zmController::~zmController()
{
    closeDb();

    deCONZ::ZclDataBase *zclDb = deCONZ::zclDataBase();
    delete zclDb;

    for (auto &n : m_nodesDead)
    {
        if (n.data)
        {
            delete n.data;
            n.data = nullptr;
        }
    }

    for (auto &n : m_nodes)
    {
        if (n.data)
        {
            delete n.data;
            n.data = nullptr;
        }
    }

    QSettings config(deCONZ::getStorageLocation(deCONZ::ConfigLocation), QSettings::IniFormat);
    storeSourceRoutingConfig(&config);

     _netModel = nullptr;
    _nodeModel = nullptr;
    _apsCtrl = nullptr;
}

static bool isValidMacAddress(uint64_t mac)
{
    return (mac & uint64_t(0xffffff)) != 0;
}

/*
    \return   0 on success
             -1 if not connected

 */
int zmController::getNetworkConfig()
{
    if (!m_master->connected())
    {
        return -1;
    }

    deCONZ::master()->readParameters();
    return 0;
}

/*!
    Sets configuriation for local endpoint.

    \param index - index of endpoint
    \param descriptor - a simple descriptor
 */
void zmController::setEndpointConfig(uint8_t index, const deCONZ::SimpleDescriptor &descriptor)
{
    QByteArray arr;
    QDataStream stream(&arr, QIODevice::ReadWrite);

    stream.setByteOrder(QDataStream::LittleEndian);
    stream << index;
    descriptor.writeToStream(stream);

    if (!arr.isEmpty())
    {
        if (deCONZ::master()->writeParameter(ZM_DID_STK_ENDPOINT, (const uint8_t*)arr.constData(), arr.size()) != 0)
        {
            DBG_Printf(DBG_ERROR, "CTRL failed to write parameter ZM_DID_STK_ENDPOINT\n");
        }
        else if (!m_nodes.empty())
        {
            // force reload of the settings
            m_nodes[0].data->setFetched(deCONZ::ReqActiveEndpoints, false);
            m_nodes[0].data->setFetched(deCONZ::ReqSimpleDescriptor, false);
            m_nodes[0].data->resetItem(deCONZ::ReqActiveEndpoints);
            m_nodes[0].data->resetItem(deCONZ::ReqSimpleDescriptor);
            m_nodes[0].g->updated(deCONZ::ReqSimpleDescriptor);
        }
    }
}

/*
 * TODO(mpi): items can really be a 32-bit bitmap.
 */
void zmController::setNetworkConfig(const zmNet &net, const uint8_t *items)
{
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    uint8_t buf[64+1]; // max is (len + 2 * network key)

    int itemsSize = items[0]; // size in first byte
    items = &items[1];

    for (int i = 0; i < itemsSize; i++)
    {
        uint8_t len = 0;
        uint8_t id = items[i];

        switch (id)
        {
        case ZM_DID_MAC_ADDRESS:
            u64 = net.ownAddress().ext();
            put_u64_le(buf, &u64);
            len = 8;
            break;

        case ZM_DID_APS_CHANNEL_MASK:
            u32 = net.channelMask();
            put_u32_le(buf, &u32);
            len = 4;
            break;

        case ZM_DID_APS_DESIGNED_COORDINATOR:
            buf[0] = (net.deviceType() == deCONZ::Coordinator) ? 1 : 0;
            len = 1;
            break;

        case ZM_DID_APS_USE_EXTENDED_PANID:
            u64 = net.panAps().ext();
            put_u64_le(buf, &u64);
            len = 8;
            break;

        case ZM_DID_NWK_PANID:
            u16 = net.pan().nwk();
            put_u16_le(buf, &u16);
            len = 2;
            break;

        case ZM_DID_STK_PREDEFINED_PANID:
            buf[0] = (net.predefinedPanId() == 1) ? 1 : 0;
            len = 1;
            break;

        case ZM_DID_STK_CONNECT_MODE:
            buf[0] = (uint8_t)net.connectMode();
            len = 1;
            break;

        case ZM_DID_STK_SECURITY_MODE:
            buf[0] = net.securityMode();
            len = 1;
            break;

        case ZM_DID_STK_NETWORK_KEY:
        {
            const QByteArray &key = net.networkKey();

            if (key.size() == 16)
            {
                buf[0] = 0x00; // key index 0
                for (int i = 0; i < key.size(); i++)
                {
                    buf[i + 1] = key[i];
                }

                len = 17;
            }
            else
            {
                DBG_Printf(DBG_ERROR, "CTRL can't set network key with invalid size %d\n", key.size());
            }
        }
            break;

        case ZM_DID_ZLL_KEY:
        {
            const QByteArray &key = net.zllKey();

            if (key.size() == 16)
            {
                for (int i = 0; i < key.size(); i++)
                {
                    buf[i] = key[i];
                }

                len = 16;
            }
            else
            {
                DBG_Printf(DBG_ERROR, "CTRL can't set ZLL key with invalid size %d\n", key.size());
            }
        }
            break;

        case ZM_DID_ZLL_FACTORY_NEW:
            buf[0] = net.zllFactoryNew() ? 1 : 0;
            len = 1;
            break;

        case ZM_DID_STK_LINK_KEY:
        {
            // TODO: fix this is only writing TC link key
            const QByteArray &key = net.trustCenterLinkKey();

            if (key.size() == 16)
            {
                uint64_t tcAddr = net.trustCenterAddress().ext();
                put_u64_le(buf, &tcAddr);

                for (int i = 0; i < key.size(); i++)
                {
                    buf[i + 8] = key[i];
                }

                len = 24;
            }
            else
            {
                DBG_Printf(DBG_ERROR, "CTRL can't set link key with invalid size %d\n", key.size());
            }
        }
            break;

//        case ZM_DID_STK_ENDPOINT:
//            data.setU8(net.endpoint());
//            break;
#if 0
        case ZM_DID_NWK_SECURITY_MATERIAL_SET:
        {
            QByteArray key;
            data.setU8((items[i] >> 16) & 0xFF);

            if (data.u8() == ZM_STANDARD_NETWORK_KEY)
            {
                key = net.networkKey();
            }
            else if (data.u8() == ZM_TRUST_CENTER_LINK_KEY)
            {
                key = net.trustCenterLinkKey();
            }
            else if (data.u8() == ZM_MASTER_KEY)
            {
                key = net.trustCenterMasterKey();
            }
            else
            {
                ok = false;
            }


            if (key.size() == 16)
            {
                for (int i = 1; i < 17; i++)
                {
                    data.data()[i] = key[i - 1];
                }

                data.setDataSize(17);
            }
            else
            {
                ok = false;
            }

        }
            break;
#endif // if 0

        case ZM_DID_APS_TRUST_CENTER_ADDRESS:
            u64 = net.trustCenterAddress().ext();
            put_u64_le(buf, &u64);
            len = 8;
            break;

        case ZM_DID_STK_STATIC_NETWORK_ADDRESS:
            buf[0] = net.staticAddress();
            len = 1;
            break;

        case ZM_DID_NWK_NETWORK_ADDRESS:
            u16  = net.ownAddress().nwk();
            put_u16_le(buf, &u16);
            len = 2;
            break;

        case ZM_DID_STK_NWK_UPDATE_ID:
            buf[0] = net.nwkUpdateId();
            len = 1;
            break;

        default:
            len = 0;
            break;
        }

        if (len)
        {
            if (deCONZ::master()->writeParameter((ZM_DataId_t)id, buf, len) != 0)
            {
                DBG_Printf(DBG_ERROR, "CTRL failed to write parameter id: 0x%02X\n", id);
            }
        }
    }
}

void zmController::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_timer)
    {
        tick();
    }
    else if (event->timerId() == m_timeoutTimer)
    {
        timeoutTick();
    }
}

/*!
    Checks if a link exists and create a new one if not so.

    The timestamp of the link will be updatet.
    \param relationship Relationship of \p bNode to \p aNode.
 */
LinkInfo *zmController::linkInfo(zmgNode *aNode, zmgNode *bNode, deCONZ::DeviceRelationship relationship)
{
    if (!aNode || !bNode)
        return nullptr;

    deCONZ::zmNode *a = aNode->data();
    deCONZ::zmNode *b = bNode->data();

    if (!a || !b)
        return nullptr;

    switch (relationship)
    {
    case deCONZ::ParentRelation:
        if (a->parentAddress() != b->address())
        {
            a->parentAddress() = b->address();
            DBG_Printf(DBG_ZDP, "update parent of %04X to %04X PR", a->address().nwk(), b->address().nwk());
        }
        break;

    case deCONZ::ChildRelation:
        if (b->parentAddress() != a->address())
        {
            b->parentAddress() = a->address();
            DBG_Printf(DBG_ZDP, "update parent of %04X to %04X CR", b->address().nwk(), a->address().nwk());
        }
        break;

    case deCONZ::SiblingRelation:
        if (b->parentAddress() != a->parentAddress())
        {
            b->parentAddress() = a->parentAddress();
            DBG_Printf(DBG_ZDP, "update parent of %04X to %04X sibling relation\n", b->address().nwk(), a->parentAddress().nwk());
        }
        break;

    default:
        break;
    }

    for (int j = 0; j < m_neighbors.size(); j++)
    {
        LinkInfo &li = m_neighbors[j];

        if ((li.a == aNode) && (li.b == bNode))
        {
            li.linkAgeUnix = m_steadyTimeRef;
            return &li;
        }

        if ((li.a == bNode) && (li.b == aNode))
        {
            li.linkAgeUnix = m_steadyTimeRef;
            return &li;
        }
    }

    // create new connection
    LinkInfo li;

    // reuse deadlink
    if (!m_neighborsDead.empty())
    {
        li = m_neighborsDead.back();
        m_neighborsDead.pop_back();

        if (li.link)
        {
            li.link->setSockets(aNode->socket(zmgNode::NeighborSocket),
                                bNode->socket(zmgNode::NeighborSocket));
            li.link->setLinkType(NodeLink::LinkNormal);
            //DBG_Printf(DBG_INFO, "reuse dead link (dead link container size now %d)\n", m_neighborsDead.size());
        }
    }

    if (!li.link) // create new link
    {
        li.link = new NodeLink(aNode->socket(zmgNode::NeighborSocket),
                               bNode->socket(zmgNode::NeighborSocket));
    }

    li.a = aNode;
    li.b = bNode;
    li.linkAgeUnix = m_steadyTimeRef;

    li.a->addLink(li.link);
    li.b->addLink(li.link);
    m_neighbors.push_back(li);

    li.link->updatePosition();
    li.link->setVisible(false); // use link tick

    return &m_neighbors.last();
}

void zmController::checkBindingLink(const deCONZ::Binding &binding)
{
    NodeInfo *srcNode = 0;
    NodeInfo *dstNode = 0;
    BindLinkInfo li;

    // check if nodes exist
    deCONZ::Address addr;
    addr.setExt(binding.srcAddress());
    srcNode = getNode(addr, deCONZ::ExtAddress);

    if (srcNode)
    {
        dstNode = getNode(binding.dstAddress(), deCONZ::ExtAddress);
    }

    if (!srcNode || !dstNode)
    {
        return;
    }

    //QList<BindLinkInfo>::iterator i;
    for (auto i = m_bindings.cbegin(); i < m_bindings.cend(); ++i)
    {
        if ((i->binding == binding))
        {
            return; // binding exists
        }
    }


    // if a link exists we just reuse it but create a correct BindLinkInfo
    if (li.isValid())
    {
        li.binding = binding;
        m_bindings.append(li);
    }
    else
    {
        // the link does not exist create one
        NodeSocket *srcSocket = srcNode->g->socket(binding.srcEndpoint(), binding.clusterId(), deCONZ::ServerCluster);
        NodeSocket *dstSocket = dstNode->g->socket(binding.dstEndpoint(), binding.clusterId(), deCONZ::ClientCluster);

        if (srcSocket && dstSocket)
        {
            li.binding = binding;
            li.link = new NodeLink(srcSocket, dstSocket);
            li.link->setLinkType(NodeLink::LinkBinding);
        }
        else
        {
            return;
        }

        srcNode->g->addLink(li.link);
        dstNode->g->addLink(li.link);
        m_bindings.append(li);

        li.link->updatePosition();
    }
}

void zmController::removeBindingLink(const Binding &binding)
{
    QList<BindLinkInfo>::iterator i;
    for (i = m_bindings.begin(); i != m_bindings.end(); ++i)
    {
        if (i->binding == binding)
        {
            if (i->link)
            {
                deCONZ::Address addr;
                addr.setExt(binding.srcAddress());

                NodeInfo *node1 = getNode(addr, deCONZ::ExtAddress);
                if (node1)
                {
                    node1->g->remLink(i->link);
                }

                node1 = getNode(binding.dstAddress(), deCONZ::ExtAddress);
                if (node1)
                {
                    node1->g->remLink(i->link);
                }
                i->link->hide();
                delete i->link;
                i->link = 0;
            }

            m_bindings.erase(i);
            break;
        }
    }
}

void zmController::clearAllApsRequestsToNode(NodeInfo node)
{
    if (!node.data)
    {
        return;
    }

    auto i = m_apsRequestQueue.begin();
    const auto end = m_apsRequestQueue.end();
    while (i != end)
    {
        if (node.data->address().hasNwk() && i->dstAddress().hasNwk())
        {
            if (node.data->address().nwk() == i->dstAddress().nwk())
            {
                i->setState(deCONZ::FinishState);
            }
        }
        else if (node.data->address().hasExt() && i->dstAddress().hasExt())
        {
            if (node.data->address().ext() == i->dstAddress().ext())
            {
                i->setState(deCONZ::FinishState);
            }
        }

        if (i != end)
        {
            ++i;
        }
    }
}

void zmController::nodeKeyPressed(deCONZ::zmNode *dnode, int key)
{
    if (m_nodes.empty())
    {
        return;
    }

    zmNetEvent event;
    NodeInfo *node = getNode(dnode);

    DBG_Assert(node->data == dnode);
    DBG_Assert(node->g != 0);
    DBG_Assert(node->data != 0);

    if (node && (node->data == dnode))
    {
        if (key == deCONZ::NodeKeyRefresh)
        {
            //node->data->forceFetch(deCONZ::ReqIeeeAddr, 1);
            //node->data->forceFetch(deCONZ::ReqNodeDescriptor, 1);
            //node->data->forceFetch(deCONZ::ReqPowerDescriptor, 1);
            //node->data->forceFetch(deCONZ::ReqActiveEndpoints, 1);
            //node->data->forceFetch(deCONZ::ReqSimpleDescriptor, 1);
            //node->data->forceFetch(deCONZ::ReqMgmtBind, 1);
            node->data->reset(node->data->macCapabilities());
            node->data->touch(m_steadyTimeRef);
            event.setType(deCONZ::NodeDataChanged);
            event.setNode(node->data);
            emit notify(event);
            deCONZ::nodeModel()->updateNode(*node);
        }
        else if (key == deCONZ::NodeKeyDelete)
        {
            if (node->data != m_nodes[0].data)
            {
                event.setType(deCONZ::NodeDeleted);
                event.setNode(node->data);
                emit notify(event);

                deleteNode(node, NodeRemoveFinally);
            }
        }
        else if (key == deCONZ::NodeKeyRequestNwkAddress)
        {
            if (sendNwkAddrRequest(node))
            {
            }
        }
        else if (key == deCONZ::NodeKeyRequestNodeDescriptor)
        {
            if (sendNodeDescriptorRequest(node))
            {
            }
        }
        else if (key == deCONZ::NodeKeyRequestPowerDescriptor)
        {
            if (sendPowerDescriptorRequest(node))
            {
            }
        }
        else if (key == deCONZ::NodeKeyRequestUpdateNetwork)
        {
            if (sendUpdateNetworkRequest(node))
            {
            }
        }
        else if (key == deCONZ::NodeKeyRequestRouteTable)
        {
            if (sendMgtmRtgRequest(node, 0))
            {
            }
        }
        else if (key == deCONZ::NodeKeyRequestMgmtLeave)
        {
            bool rejoin = true;
            bool removeChildren = false;
            if (sendMgmtLeaveRequest(node->data, removeChildren, rejoin))
            {
            }
        }
        else if (key == deCONZ::NodeKeyRequestNwkLeave)
        {
            bool rejoin = true;
            bool removeChildren = false;
            if (sendNwkLeaveRequest(node->data, removeChildren, rejoin))
            {
            }
        }
        else if (key == deCONZ::NodeKeyRequestChildRejoin)
        {
            if (sendForceChildRejoin(node->data))
            {
            }
        }
        else if (key == deCONZ::NodeKeyRequestActiveEndpoints)
        {
            if (sendActiveEndpointsRequest(node))
            {
            }
        }
        else if (key == deCONZ::NodeKeyDeviceAnnce)
        {
            sendDeviceAnnce();
        }
        else if (key == deCONZ::NodeKeyRequestSimpleDescriptors)
        {
            for (uint8_t endpoint: node->data->endpoints())
            {
                if (sendSimpleDescriptorRequest(node, endpoint))
                {
                }
            }
        }
        else if (key == deCONZ::NodeKeyEdScan)
        {
            const auto &net = _netModel->currentNetwork();
            sendEdScanRequest(node, net.channelMask());
        }
        else if (key == Qt::Key_9)
        {

            const deCONZ::SimpleDescriptor *sd = node->data->getSimpleDescriptor(deCONZ::clusterInfo()->endpoint());

            if (!sd || !sd->isValid())
            {
                return;
            }

            DBG_Printf(DBG_INFO, "send ZCL discover attributes req to node %s\n", node->data->extAddressString().c_str());
            uint8_t startIndex = 0;
            if (sendZclDiscoverAttributesRequest(node, *sd, deCONZ::clusterInfo()->clusterId(), startIndex))
            {
            }
        }
        else if (key == Qt::Key_5)
        {
            sendMatchDescriptorReq(0x006);
        }
        else
        {
            DBG_Printf(DBG_INFO, "Unhandled node key %d\n", key);
        }
    }
}

static  quint32 processFrameCounter(uint64_t mac, uint32_t oldFrameCounter, uint32_t newFrameCounter, const QString &fcKey)
{
    if (mac == 0 || fcKey.isEmpty())
    {
        return newFrameCounter;
    }

    if (oldFrameCounter > newFrameCounter)
    {
        DBG_Printf(DBG_INFO, "Warning frame counter %u (0x%08X) lower than previous one %u (0x%08X)\n",
                   newFrameCounter, newFrameCounter, oldFrameCounter, oldFrameCounter);

        if ((oldFrameCounter - newFrameCounter) > (UINT32_MAX / 2))
        {
            DBG_Printf(DBG_INFO, "TODO handle frame counter wrap\n");
        }
        else if (deCONZ::master()->deviceProtocolVersion() >= DECONZ_PROTOCOL_VERSION_1_12)
        {
            uint8_t out[4];
            oldFrameCounter += 300;
            put_u32_le(out, &oldFrameCounter);

            DBG_Printf(DBG_INFO, "Raise frame counter to %u (0x%08X)\n", oldFrameCounter, oldFrameCounter);
            deCONZ::master()->writeParameter(ZM_DID_STK_FRAME_COUNTER, out, 4);
        }
    }
    else if ((newFrameCounter - oldFrameCounter) > 500)
    {
        QSettings config(deCONZ::getStorageLocation(deCONZ::ConfigLocation), QSettings::IniFormat);
        config.setValue(fcKey, newFrameCounter);
    }

    return newFrameCounter;
}

/*!
    Receipt of a read parameter response frame.
 */
void zmController::readParameterResponse(ZM_State_t status, ZM_DataId_t id, const uint8_t *data, uint16_t length)
{
    if (!m_nodes.empty())
    {
        // visualizeNodeChanged(&m_nodes[0], deCONZ::IndicateDataUpdate);
    }

    if (status != ZM_STATE_SUCCESS)
    {
        DBG_Printf(DBG_INFO_L2, "CTRL read param resp id: 0x%02X, status: 0x%02X\n", (uint8_t)id, (uint8_t)status);
        return; // TODO: do something useful
    }

    if (length < 1)
    {
        DBG_Printf(DBG_INFO, "CTRL read param resp id: 0x%02X, status: 0x%02X, length: %u (empty)\n", id, status, length);
        return;
    }

    // update device state
    if (id == ZM_DID_STK_NETWORK_STATUS)
    {
        switch (data[0])
        {
        case ZM_NET_JOINING: deCONZ::setDeviceState(deCONZ::Connecting); break;
        case ZM_NET_ONLINE:  deCONZ::setDeviceState(deCONZ::InNetwork); break;
        case ZM_NET_LEAVING: deCONZ::setDeviceState(deCONZ::Leaving); break;
        case ZM_NET_OFFLINE: // fall through
        default:             deCONZ::setDeviceState(deCONZ::NotInNetwork);
            break;
        }

        return;
    }

    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    uint16_t updateCount = 0;
    zmNet &net = _netModel->currentNetwork();

    switch (id)
    {
    case ZM_DID_MAC_ADDRESS:
    {
        DBG_Assert(length >= 8);
        get_u64_le(data, &u64);

        if (isValidMacAddress(u64))
        {
            if (m_frameCounterKey.isEmpty() || net.ownAddress().ext() != u64) // try init old frame counter from config
            {
                m_frameCounterKey = QString("N%1/framecounter").arg(u64, 16, 16, QChar('0'));

                if (m_frameCounter == 0)
                {
                    bool ok = false;
                    QSettings config(deCONZ::getStorageLocation(deCONZ::ConfigLocation), QSettings::IniFormat);

                    uint32_t frameCounter = config.value(m_frameCounterKey, 0).toUInt(&ok);
                    if (ok)
                    {
                        m_frameCounter = frameCounter;
                    }

                    if (deCONZ::master()->deviceProtocolVersion() >= DECONZ_PROTOCOL_VERSION_1_12)
                    {
                        deCONZ::master()->readParameter(ZM_DID_STK_FRAME_COUNTER);
                    }
                }
            }

            if (!net.ownAddress().hasNwk())
            {
                net.ownAddress().setNwk(0x0000); // TODO assumption, nwk + ext is needed to create valid database entries
            }

            if (net.ownAddress().ext() != u64)
            {
                net.ownAddress().setExt(u64);
                updateCount++;
            }
            auto *node = getNode(net.ownAddress(), deCONZ::ExtAddress);

            // create our own device node on index 0
            deCONZ::MacCapabilities macCapabilities = deCONZ::MacDeviceIsFFD | deCONZ::MacIsMainsPowered | deCONZ::MacReceiverOnWhenIdle;
            Address addr = net.ownAddress();
            addr.setExt(u64);

            bool macAddrChanged = false;

            if (!node && !m_nodes.empty() && m_nodes[0].data->isCoordinator()) // replace coordinator with possibly different mac
            {
                node = &m_nodes[0];

                if (node->data->address().ext() != u64)
                {
                    macAddrChanged = true;
                }
            }

            if (!node)
            {
                createNode(addr, macCapabilities);
                node = getNode(addr, deCONZ::ExtAddress);
            }

            if (node)
            {
                DBG_Assert(m_nodes.size() >= 1);
                DBG_Assert(node->data != nullptr);

                deCONZ::Address addr2;
                addr2.setExt(u64);
                addr2.setNwk(0x0000);
                node->data->setMacCapabilities(macCapabilities); // sanity
                node->data->setAddress(addr2);
                node->data->setFetched(deCONZ::ReqIeeeAddr, true);
                node->data->setFetched(deCONZ::ReqNodeDescriptor, false);
                node->data->setFetched(deCONZ::ReqActiveEndpoints, false);
                node->data->setFetched(deCONZ::ReqSimpleDescriptor, false);
                node->data->touch(m_steadyTimeRef);
                node->g->setLastSeen(m_steadyTimeRef.ref);
                checkAddressChange(node->data->address());

                node->g->updateParameters(node->data);
                node->g->requestUpdate();

                if (node != &m_nodes[0])
                {
                    NodeInfo tmp(m_nodes[0]);
                    m_nodes[0] = *node;
                    *node = tmp;
                }

                if (macAddrChanged)
                {
                    emit nodeEvent(NodeEvent(NodeEvent::UpdatedNodeAddress, node->data));
                }
            }

            m_graph->fitInView(m_scene->itemsBoundingRect().adjusted(-20, -20, 20, 20), Qt::KeepAspectRatio);

            for (const auto &node2 : m_nodes) // hide other coordinator nodes if present
            {
                if (node2.data && node2.g &&
                    node2.data->address().nwk() == 0x0000 &&
                    node2.data->nodeDescriptor().deviceType() == deCONZ::Coordinator &&
                    node2.data->address().ext() != u64)
                {
                    node2.g->hide();
                }
            }
        }
    }
        break;

    case ZM_DID_NWK_NETWORK_ADDRESS:
        DBG_Assert(length >= 2);
        get_u16_le(data, &u16);

        if (!net.ownAddress().hasNwk() || net.ownAddress().nwk() != u16)
        {
            net.ownAddress().setNwk(u16);
            updateCount++;
        }

        if (u16 != 0xFFFF && net.ownAddress().hasExt())
        {

            auto *node = getNode(net.ownAddress(), deCONZ::ExtAddress);

            if (node && node->data)
            {
                deCONZ::Address addr = node->data->address();
                addr.setNwk(u16);
                node->data->setAddress(addr);

                node->data->setFetched(deCONZ::ReqNwkAddr, true);
                checkAddressChange(node->data->address());
            }
        }
        break;

    case ZM_DID_NWK_PANID:
        DBG_Assert(length >= 2);
        get_u16_le(data, &u16);
        if (net.pan().nwk() != u16)
        {
            net.pan().setNwk(u16);
            updateCount++;
        }
        break;

    case ZM_DID_STK_PREDEFINED_PANID:
    {
        DBG_Assert(length >= 1);
        bool predefined = (data[0] == 1) ? true : false;
        if (net.predefinedPanId() != predefined)
        {
            net.setPredefinedPanId(predefined);
            updateCount++;
        }
    }
        break;

    case ZM_DID_NWK_EXTENDED_PANID:
        DBG_Assert(length >= 8);
        get_u64_le(data, &u64);
        if (net.pan().ext() != u64)
        {
            net.pan().setExt(u64);
            updateCount++;
        }
        break;

    case ZM_DID_APS_DESIGNED_COORDINATOR:
    {
        DBG_Assert(length >= 1);
        DBG_Assert(data[0] == 1 || data[1] == 0);
        auto deviceType = (data[0] == 1) ? deCONZ::Coordinator : deCONZ::Router;
        if (net.deviceType() != deviceType)
        {
            net.setDeviceType(deviceType);
            updateCount++;
        }

        if (net.ownAddress().hasExt())
        {
            auto *node = getNode(net.ownAddress(), deCONZ::ExtAddress);

            if (node && node->data && node->g)
            {
                if (net.deviceType() == deCONZ::Coordinator && node->data->address().nwk() != 0x0000)
                {
                    deCONZ::Address addr = node->data->address();
                    if (addr.hasNwk() && addr.nwk() != 0)
                    {
                        addr.setNwk(0x0000);
                        node->data->setAddress(addr);
                        node->g->requestUpdate();
                    }
                }
            }
        }
    }
        break;

    case ZM_DID_APS_CHANNEL_MASK:
        DBG_Assert(length >= 4);
        get_u32_le(data, &u32);
        if (net.channelMask() != u32)
        {
            net.setChannelMask(u32);
            updateCount++;
        }
        break;

    case ZM_DID_DEV_WATCHDOG_TTL:
    {
        DBG_Assert(length >= 4);
        // this param is read every 60 seconds
        get_u32_le(data, &u32);
//        DBG_Printf(DBG_INFO, "Device TTL %u s flags: 0x%X\n", u32, m_deviceWatchdogOk);
        if (u32 < DEVICE_TTL_RESET_THRESHOLD && m_deviceWatchdogOk == DEVICE_ALL_OK)
        {
            DBG_Printf(DBG_INFO, "Device reset watchdog %u s\n", DEVICE_TTL_RESET);
            // if all good reset watchdog
            m_deviceWatchdogOk = 0; // mark dirty
            deCONZ::master()->resetDeviceWatchdog(DEVICE_TTL_RESET);
        }
        else if (u32 > DEVICE_TTL_RESET && m_deviceWatchdogOk == DEVICE_ALL_OK)
        {
            // note: deviceWatchdogOk must be set to not disrupt sensor only networks
            DBG_Printf(DBG_INFO, "Device init watchdog %u s\n", DEVICE_TTL_RESET);
            deCONZ::master()->resetDeviceWatchdog(DEVICE_TTL_RESET);
        }
    }
        break;

    case ZM_DID_STK_FRAME_COUNTER:
    {
        DBG_Assert(length >= 4);
        // this param is read every 60 seconds
        get_u32_le(data, &u32);
        m_frameCounter = processFrameCounter(getParameter(deCONZ::ParamMacAddress), m_frameCounter, u32, m_frameCounterKey);
    }
        break;

    case ZM_DID_STK_CURRENT_CHANNEL:
        DBG_Assert(length >= 1);
        if (net.channel() != data[0])
        {
            net.setChannel(data[0]);
            updateCount++;
            DBG_Printf(DBG_INFO, "Current channel %u\n", data[0]);
        }
        break;

    case ZM_DID_APS_USE_EXTENDED_PANID:
        DBG_Assert(length >= 8);
        get_u64_le(data, &u64);
        if (net.panAps().ext() != u64)
        {
            net.panAps().setExt(u64);
            updateCount++;
        }
        break;

    case ZM_DID_APS_TRUST_CENTER_ADDRESS:
        DBG_Assert(length >= 8);
        get_u64_le(data, &u64);
        if (net.trustCenterAddress().ext() != u64)
        {
            net.trustCenterAddress().setExt(u64);
            updateCount++;
        }
        break;

    case ZM_DID_APS_USE_INSECURE_JOIN:
    {
        DBG_Assert(length >= 1);
        bool useInsecureJoin = (data[0] == 1) ? true : false;
        if (net.useInsecureJoin() != useInsecureJoin)
        {
            net.setUseInsecureJoin(useInsecureJoin);
            updateCount++;
        }
    }
        break;

    case ZM_DID_STK_STATIC_NETWORK_ADDRESS:
    {
        DBG_Assert(length >= 1);
        bool staticAddress = (data[0] == 1) ? true : false;
        if (net.staticAddress() != staticAddress)
        {
            net.setStaticAddress(staticAddress);
            updateCount++;
        }
    }
        break;

    case ZM_DID_STK_NETWORK_KEY:
        DBG_Assert(length >= 17);
        if (data[0] == 0)
        {
            if (net.networkKey().size() < 16 ||
                (net.networkKey().size() == 16 && memcmp(net.networkKey().data(), &data[1], 16) != 0))
            {
                net.setNetworkKey(QByteArray((const char*)&data[1], 16));
                updateCount++;
            }
        }
        else
        {
            DBG_Printf(DBG_ERROR, "CTRL got network key with invalid index %u\n", data[0]);
        }
        break;

    case ZM_DID_STK_LINK_KEY:
        if (length == 24)
        {
            if (net.trustCenterLinkKey().size() < 16 ||
                (net.trustCenterLinkKey().size() == 16 && memcmp(net.trustCenterLinkKey().data(), &data[8], 16) != 0))
            {
                net.setTrustCenterLinkKey(QByteArray((const char*)&data[8], 16));
                updateCount++;
            }
        }
        else
        {
            DBG_Printf(DBG_ERROR, "CTRL got link key with invalid length %u\n", length);
        }
        break;

    case ZM_DID_STK_CONNECT_MODE:
        if (length == 1)
        {
            switch (data[0])
            {
            case deCONZ::ConnectModeManual:
            case deCONZ::ConnectModeNormal:
            case deCONZ::ConnectModeZll:
                net.setConnectMode((deCONZ::ConnectMode)data[0]);
                break;

            default:
                DBG_Printf(DBG_ERROR, "CTRL got invalid connect mode %u\n", data[0]);
                break;
            }
        }
        else
        {
            DBG_Printf(DBG_ERROR, "CTRL got connect mode with invalid length %u\n", length);
        }
        break;

    case ZM_DID_ZLL_KEY:
        if (length == 16)
        {
            //net.setZllKey(QByteArray((const char*)&data[0], 16));
        }
        else
        {
            DBG_Printf(DBG_ERROR, "CTRL got zll key with invalid length %u\n", length);
        }
        break;

    case ZM_DID_ZLL_FACTORY_NEW:
        if (length == 1)
        {
            if (data[0] <= 1)
            {
                //net.setZllFactoryNew(data[0] == 1);
            }
            else
            {
                DBG_Printf(DBG_ERROR, "CTRL got zllFactoryNew (bool) with invalid value %u\n", data[0]);
            }
        }
        else
        {
            DBG_Printf(DBG_ERROR, "CTRL got zllFactoryNew (bool) with invalid length %u\n", length);
        }
        break;

    case ZM_DID_STK_KEY_FOR_INDEX:
    {
        if (length == 25)
        {
            uint8_t idx;
            uint64_t extAddr;
            SecKeyPair keyPair;
            SecKey &key = keyPair.key();

            // index
            data = get_u8_le(data, &idx);

            // address
            data = get_u64_le(data, &extAddr);
            keyPair.address().setExt(extAddr);

            // key data
            key.setData(data, SecKey::KeySize128);

//            DBG_Printf(DBG_INFO, "CTRL got key for index: %u, addr: " FMT_MAC ", key: 0x%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
//                       idx, extAddr, key.at(0), key.at(1), key.at(2), key.at(3), key.at(4), key.at(5), key.at(6), key.at(7),
//                                     key.at(8), key.at(9), key.at(10), key.at(11), key.at(12), key.at(13), key.at(14), key.at(15));

            net.securityKeyPairs().push_back(keyPair); // TODO check first if one is in

        }
        else
        {
            DBG_Printf(DBG_ERROR, "CTRL got key with invalid length %u\n", length);
        }
    }
        break;
#if 0
    case ZM_DID_NWK_SECURITY_MATERIAL_SET:
        switch (data[0])
        {
        case ZM_STANDARD_NETWORK_KEY:
        case ZM_HIGH_SECURITY_NETWORK_KEY:
            net.setNetworkKey(QByteArray((const char*)&data[1], 16));
            break;

        case ZM_TRUST_CENTER_LINK_KEY:
            net.setTrustCenterLinkKey(QByteArray((const char*)&data[1], 16));
            break;

        case ZM_MASTER_KEY:
            // this is the last requested
            net.setTrustCenterMasterKey(QByteArray((const char*)&data[1], 16));
            //                m_getConfigInProgress = false;
            break;

        default:
            break;
        }

        break;
#endif

    case ZM_DID_NWK_SECURITY_LEVEL:
        net.setSecurityLevel(data[0]);
        break;

    case ZM_DID_STK_SECURITY_MODE:
        net.setSecurityMode(data[0]);
        break;

    case ZM_DID_STK_ENDPOINT:
    {
        SimpleDescriptor sd;

        DBG_Assert(length >=  (1 + 8));
        if (length >= (1 + 8)) // min simple descriptor length
        {
            const auto arr = QByteArray::fromRawData((const char*)&data[1], int(length));
            QDataStream stream(arr);
            stream.setByteOrder(QDataStream::LittleEndian);
            sd.readFromStream(stream, 0);
        }

        if (!sd.isValid())
        {
            DBG_Printf(DBG_INFO, "Invalid firmware endpoint on index: %u\n", data[0]);
        }

        const auto index = data[0];

        if (index == 1 && sd.isValid() && !(sd.endpoint() == 0x50 || sd.endpoint() == 0x32)) // former old ota endpoints
        {
             // don't overwrite custom set endpoints
        }
        // enforce ZGP endpoint on index 1
        else if (index == 1 && (sd.profileId() != GREEN_POWER_PROFILE_ID || sd.endpoint() != GREEN_POWER_ENDPOINT || sd.deviceId() != 0x0064 || sd.deviceVersion() != 1))
        {
            sd = { };
            sd.setProfileId(GREEN_POWER_PROFILE_ID);
            sd.setEndpoint(GREEN_POWER_ENDPOINT);
            sd.setDeviceId(0x0064); // GP Commissioning Tool
            sd.setDeviceVersion(1);
            sd.outClusters().push_back(deCONZ::zclDataBase()->outCluster(GREEN_POWER_PROFILE_ID, GREEN_POWER_CLUSTER_ID, 0x0000));

            setEndpointConfig(index, sd);
        }

        if (sd.profileId() == HA_PROFILE_ID && sd.endpoint() == 0x01)
        {
            DBG_Assert(index == 0);
            // check if all default clusters are present and fix missing ones
            bool needUpdate = false;
            const int maxClusters = 9; // firmware limitation

            {
                const std::array<uint16_t, 5> inClusters = { 0x0000, 0x0006, 0x000a, 0x0019, 0x0501 }; // basic, on/off, time, ota, ias ace
                for (const auto clusterId : inClusters)
                {
                    if (!sd.cluster(clusterId, deCONZ::ServerCluster) && sd.inClusters().size() < maxClusters)
                    {
                        sd.inClusters().push_back(deCONZ::zclDataBase()->inCluster(HA_PROFILE_ID, clusterId, 0x0000));
                        DBG_Printf(DBG_INFO, "%s server cluster not present, append cluster\n", qPrintable(sd.inClusters().back().name()));
                        needUpdate = true;
                    }
                }
            }

            {
                const std::array<uint16_t, 4> outClusters = {  0x0001, 0x0020, 0x0500, 0x0502 }; // power configuration, ias, poll control, ias warning
                for (const auto clusterId : outClusters)
                {
                    if (!sd.cluster(clusterId, deCONZ::ClientCluster) && sd.outClusters().size() < maxClusters)
                    {
                        sd.outClusters().push_back(deCONZ::zclDataBase()->outCluster(HA_PROFILE_ID, clusterId, 0x0000));
                        DBG_Printf(DBG_INFO, "%s client cluster not present, append cluster\n", qPrintable(sd.outClusters().back().name()));
                        needUpdate = true;
                    }
                }
            }

            if (needUpdate)
            {
                std::sort(sd.inClusters().begin(), sd.inClusters().end(), [](const deCONZ::ZclCluster &a, const deCONZ::ZclCluster &b) { return a.id() < b.id(); });
                std::sort(sd.outClusters().begin(), sd.outClusters().end(), [](const deCONZ::ZclCluster &a, const deCONZ::ZclCluster &b) { return a.id() < b.id(); });
                setEndpointConfig(data[0], sd);
            }
        }

        deCONZ::netEdit()->setSimpleDescriptor(data[0], std::move(sd));

        if (index > 0)
        {
            auto *node = getNode(net.ownAddress(), deCONZ::ExtAddress);

            if (node && node->data)
            {
                node->data->setFetched(deCONZ::ReqActiveEndpoints, false);
                node->data->setFetched(deCONZ::ReqSimpleDescriptor, false);
                node->data->resetItem(deCONZ::ReqActiveEndpoints);
                //node->data->resetItem(deCONZ::ReqSimpleDescriptor);
            }
        }
    }
        break;

    case ZM_DID_STK_PERMIT_JOIN:
    {
        DBG_Assert(length == 1);
        if (length == 1)
        {
            net.setPermitJoin(data[0]);
        }
        else
        {
            DBG_Printf(DBG_ERROR, "CTRL got permit join duration with invalid length %u\n", length);
        }
    }
        break;

    case ZM_DID_STK_PROTOCOL_VERSION:
        break;

    case ZM_DID_STK_DEBUG:
    {
        DBG_Assert(length == 3);
        if (length == 3)
        {
            uint16_t dbgCode = 0;
            get_u16_le(data + 1, &dbgCode);

            if (dbgCode > 0)
            {
                DBG_Printf(DBG_INFO, "CTRL got stack debug assert code: 0x%04X, type: 0x%02X\n", dbgCode, data[0]);
            }
        }
        else
        {
            DBG_Printf(DBG_ERROR, "CTRL got stack debug assert code with invalid length %u\n", length);
        }
    }
        break;

    case ZM_DID_STK_ANT_CTRL:
        DBG_Assert(length == 1);
        if (length == 1)
        {
            DBG_Printf(DBG_INFO, "CTRL ANT_CTRL 0x%02X\n", data[0]);

            if (data[0] == ANTENNA_1_SELECT)
            {
                // UF.l antenna, switch to chip antenna
                uint8_t ant_ctrl = ANTENNA_DEFAULT_SELECT;
                deCONZ::master()->writeParameter(ZM_DID_STK_ANT_CTRL, &ant_ctrl, 1);
            }
            else if (data[0] == ANTENNA_2_SELECT || data[0] == ANTENNA_DEFAULT_SELECT)
            {
                // OK chip antenna
            }
        }
        break;

    case ZM_DID_STK_NO_ZDP_RESPONSE:
        DBG_Assert(length == 2);
        if (length == 2)
        {
            uint16_t clFlags = 0;
            get_u16_le(data, &clFlags);
            DBG_Printf(DBG_INFO, "CTRL ZDP_RESPONSE handler 0x%04X\n", clFlags);

            // 0x0001 Node Descriptor handled by application
            if ((clFlags & 0x0001) == 0 && net.ownAddress().hasExt())
            {
                auto *node = getNode(net.ownAddress(), deCONZ::ExtAddress);
                if (node && node->data && !node->data->nodeDescriptor().isNull())
                {
                    clFlags |= 0x0001;
                    uint8_t buf[2];
                    put_u16_le(buf, &clFlags);
                    DBG_Printf(DBG_INFO, "CTRL reconfigure ZDP_RESPONSE handler 0x%04X\n", clFlags);
                    deCONZ::master()->writeParameter(ZM_DID_STK_NO_ZDP_RESPONSE, &buf[0], 2);
                }
            }
        }
        break;

    case ZM_DID_STK_NWK_UPDATE_ID:
        DBG_Assert(length == 1);
        if (length == 1)
        {
            if (net.nwkUpdateId() != data[0])
            {
                net.setNwkUpdateId(data[0]);
                updateCount++;
                DBG_Printf(DBG_INFO, "CTRL got nwk update id %u\n", data[0]);
            }
        }
        else
        {
            DBG_Printf(DBG_ERROR, "CTRL got nwk update id with invalid length %u\n", length);
        }
        break;

    case ZM_DID_STK_SECURITY_MATERIAL0:
    {
        DBG_Assert(length == 32);
        if (length == 32)
        {
            QMessageAuthenticationCode hmac(QCryptographicHash::Sha256);
            hmac.setKey(QByteArray(reinterpret_cast<const char*>(data), 16));
            hmac.addData(reinterpret_cast<const char*>(data + 16), 16);
            m_securityMaterial0 = hmac.result().toHex();
        }
    }
        break;

    default:
        DBG_Printf(DBG_INFO, "Got read parameter response for unknown parameter id 0x%02X\n", (uint8_t)id);
        break;
    }

    if (updateCount > 0)
    {
        _netModel->setCurrentNetwork(net);
    }

    {
        if (net.ownAddress().ext() > 0 &&
            net.trustCenterAddress().ext() > 0 &&
            net.channel() > 0)
        {
            m_deviceWatchdogOk |= DEVICE_CONFIG_NETWORK_OK;
        }
    }
}

/*!
 * Broadcast a ZDP DeviceAnnce command for own device.
 */
void zmController::sendDeviceAnnce()
{

    if (m_nodes.empty())
    {
        return;
    }

    deCONZ::Node *node = m_nodes[0].data;
    deCONZ::ApsDataRequest req;

    DBG_Assert(node != nullptr);

    if (!node)
    {
        return;
    }

    DBG_Assert(node->address().hasNwk());
    DBG_Assert(node->address().hasExt());

    req.dstAddress().setNwk(BroadcastRxOnWhenIdle);
    req.setDstAddressMode(deCONZ::ApsNwkAddress);
    req.setProfileId(ZDP_PROFILE_ID);
    req.setSrcEndpoint(ZDO_ENDPOINT);
    req.setDstEndpoint(ZDO_ENDPOINT);
    req.setClusterId(ZDP_DEVICE_ANNCE_CLID);
    req.setRadius(0);

    QDataStream stream(&req.asdu(), QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream << genSequenceNumber();
    stream << node->address().nwk();
    stream << (quint64)node->address().ext();
    stream << (quint8)node->nodeDescriptor().macCapabilities();

    apsdeDataRequest(req);
}

/*!
 * Broadcast a ZDP Matchdescriptor request.
 */
bool zmController::sendMatchDescriptorReq(uint16_t clusterId)
{
    if (m_nodes.empty())
    {
        return false;
    }

    deCONZ::ApsDataRequest req;

    req.dstAddress().setNwk(BroadcastRxOnWhenIdle);
    req.setDstAddressMode(deCONZ::ApsNwkAddress);
    req.setProfileId(ZDP_PROFILE_ID);
    req.setSrcEndpoint(ZDO_ENDPOINT);
    req.setDstEndpoint(ZDO_ENDPOINT);
    req.setClusterId(ZDP_MATCH_DESCRIPTOR_CLID);
    req.setRadius(0);

    QDataStream stream(&req.asdu(), QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream << genSequenceNumber();
    stream << (uint16_t)BroadcastRxOnWhenIdle;
    stream << (uint16_t)ZLL_PROFILE_ID;
    //stream << (quint16)0xffff; // wild card profile id
    stream << (uint8_t)0x01; // server cluster count
    stream << clusterId;
    stream << (uint8_t)0x00; // client cluster count

    return apsdeDataRequest(req) == deCONZ::Success;
}

uint8_t zmController::getParameter(U8Parameter parameter)
{
    if (!_netModel)
    {
        return 0;
    }

    const zmNet &net = _netModel->currentNetwork();

    switch (parameter)
    {
    case ParamOtauActive:
        return m_otauActive ? 1 : 0;

    case ParamAutoPollingActive:
        return m_autoPollingActive ? 1 : 0;

    case ParamCurrentChannel:
        return net.channel();

    case ParamDeviceType:
        return net.deviceType();

    case ParamSecurityMode:
        return net.securityMode();

    case ParamPermitJoin:
        return net.permitJoin();

    case ParamNetworkUpdateId:
        return net.nwkUpdateId();

    case ParamFirmwareUpdateActive:
        return m_fwUpdateActive;

    case ParamDeviceConnected:
        return deCONZ::master()->connected() ? 1 : 0;

    case ParamApsAck:
        return netEdit()->apsAcksEnabled() ? 1 : 0;

    case ParamStaticNwkAddress:
        return netEdit()->staticNwkAddress() ? 1 : 0;

    default:
        DBG_Printf(DBG_ERROR, "Unknown 8-bit parameter %d\n", (int)parameter);
        break;
    }
    return 0;
}

bool zmController::setParameter(U8Parameter parameter, uint8_t value)
{
    if (!_netModel)
    {
        return 0;
    }

    zmNet &net = _netModel->currentNetwork();
    uint8_t items[4];
    items[0] = 1; // one item

    switch (parameter)
    {
    case ParamDeviceType: // TODO use aps designed coordinator
        DBG_Assert(value == deCONZ::Coordinator || value == deCONZ::Router);
        if (value == deCONZ::Coordinator || value == deCONZ::Router)
        {
            switch (value)
            {
            case deCONZ::Coordinator:
                net.setDeviceType(deCONZ::Coordinator);
                break;
            case deCONZ::Router:
                net.setDeviceType(deCONZ::Router);
                break;
            }
            _netModel->setCurrentNetwork(net);
        }
        break;

    case ParamSecurityMode:
        DBG_Assert(value == 0 || value == 1 || value == 2 || value == 3);
        if (value == 0 || value == 1 || value == 2 || value == 3)
        {
            net.setSecurityMode(value);
            _netModel->setCurrentNetwork(net);

            items[1] = ZM_DID_STK_SECURITY_MODE;
            setNetworkConfig(net, items);
        }
        break;

    case ParamOtauActive:
        DBG_Assert(value == 0 || value == 1);
        if (value == 0 || value == 1)
        {
            m_otauActive = (value == 1) ? true : false;
            return true;
        }
        break;

    case ParamAutoPollingActive:
        DBG_Assert(value == 0 || value == 1);
        if (value == 0 || value == 1)
        {
            m_autoPollingActive = (value == 1) ? true : false;
            return true;
        }
        break;

    case ParamFirmwareUpdateActive:
        DBG_Assert(value == FirmwareUpdateIdle || value == FirmwareUpdateReadyToStart || value == FirmwareUpdateRunning);
        if (value == FirmwareUpdateIdle || value == FirmwareUpdateReadyToStart || value == FirmwareUpdateRunning)
        {
            m_fwUpdateActive = value;
            return true;
        }
        break;

    case ParamCurrentChannel:
    {
        DBG_Assert(value >= 11 && value <= 26);
        net.setChannel(value);
        net.setChannelMask(1 << (uint)value);
        _netModel->setCurrentNetwork(net);
        items[1] = ZM_DID_APS_CHANNEL_MASK;
        setNetworkConfig(net, items);
    }
        break;

    case ParamNetworkUpdateId:
    {
        net.setNwkUpdateId(value);
        _netModel->setCurrentNetwork(net);
        items[1] = ZM_DID_STK_NWK_UPDATE_ID;
        setNetworkConfig(net, items);
    }
        break;

    case ParamApsAck:
    {
        bool val = (value == 0) ? false : true;
        netEdit()->setApsAcksEnabled(val);
    }
        break;

    case ParamPredefinedPanId:
    {
        bool val = (value == 0) ? false : true;
        netEdit()->predefinedPanIdToggled(val);
    }
        break;

    case ParamCustomMacAddress:
    {
        bool val = (value == 0) ? false : true;
        netEdit()->customMacAddressToggled(val);
    }
        break;

    case ParamStaticNwkAddress:
    {
        bool val = (value == 0) ? false : true;
        netEdit()->staticNwkAddressToggled(val);
    }
        break;
    default:
        break;
    }

    return false;
}

bool zmController::setParameter(U16Parameter parameter, uint16_t value)
{
    if (!_netModel)
    {
        return 0;
    }

    zmNet &net = _netModel->currentNetwork();
    uint8_t items[4];
    items[0] = 1; // one item

    switch (parameter)
    {
    case ParamPANID:
        {
            net.pan().setNwk(value);
            _netModel->setCurrentNetwork(net);
            items[1] = ZM_DID_NWK_PANID;
            setNetworkConfig(net, items);
        }
        break;

    case ParamNwkAddress:
        {
            net.ownAddress().setNwk(value);
            _netModel->setCurrentNetwork(net);
            items[1] = ZM_DID_NWK_PANID;
            setNetworkConfig(net, items);
        }
        break;

    default:
        break;
    }

    return false;
}

bool zmController::setParameter(U32Parameter parameter, uint32_t value)
{
    if (!_netModel)
    {
        return 0;
    }

    zmNet &net = _netModel->currentNetwork();
    uint8_t items[4];
    items[0] = 1; // one item

    switch (parameter)
    {

    case ParamChannelMask:
    {
        net.setChannelMask(1 << (uint)value);
        _netModel->setCurrentNetwork(net);
        items[1] = ZM_DID_APS_CHANNEL_MASK;
        setNetworkConfig(net, items);
        return true;
    }
        break;

    case ParamFrameCounter:
    {
        const auto mac = getParameter(deCONZ::ParamMacAddress);
        if (m_frameCounter <= value && mac != 0 && !m_frameCounterKey.isEmpty())
        {
            m_frameCounter = value;
            QSettings config(deCONZ::getStorageLocation(deCONZ::ConfigLocation), QSettings::IniFormat);
            config.setValue(m_frameCounterKey, m_frameCounter);
            return true;
        }
    }

    default:
        break;
    }

    return false;
}

bool zmController::setParameter(U64Parameter parameter, uint64_t value)
{
    if (!_netModel)
    {
        return 0;
    }

    zmNet &net = _netModel->currentNetwork();
    uint8_t items[4];
    items[0] = 1; // one item

    switch (parameter)
    {

    case ParamMacAddress:
        {
            net.ownAddress().setExt(value);
            _netModel->setCurrentNetwork(net);
            items[1] = ZM_DID_MAC_ADDRESS;
            setNetworkConfig(net, items);
        }
        break;

    case ParamTrustCenterAddress:
        {
            net.trustCenterAddress().setExt(value);
            _netModel->setCurrentNetwork(net);
            items[1] = ZM_DID_APS_TRUST_CENTER_ADDRESS;
            setNetworkConfig(net, items);
        }
        break;

    case ParamExtendedPANID:
        {
            net.pan().setExt(value);
            _netModel->setCurrentNetwork(net);
            items[1] = ZM_DID_NWK_EXTENDED_PANID;
            setNetworkConfig(net, items);
        }
        break;

    case ParamApsUseExtendedPANID:
        {
            net.panAps().setExt(value);
            _netModel->setCurrentNetwork(net);
            items[1] = ZM_DID_APS_USE_EXTENDED_PANID;
            setNetworkConfig(net, items);
        }
        break;

    default:
        break;
    }

    return false;
}

bool zmController::setParameter(ArrayParameter parameter, QByteArray value)
{
    if (!_netModel)
    {
        return 0;
    }

    zmNet &net = _netModel->currentNetwork();
    uint8_t items[4];
    items[0] = 1; // one item

    switch (parameter)
    {
        case ParamNetworkKey:
        {
            net.setNetworkKey(value);
            _netModel->setCurrentNetwork(net);
            items[1] = ZM_DID_STK_NETWORK_KEY;
            setNetworkConfig(net, items);
        }
        break;

        case ParamTrustCenterLinkKey:
        {
            net.setTrustCenterLinkKey(value);
            _netModel->setCurrentNetwork(net);
            items[1] = ZM_DID_STK_LINK_KEY;
            setNetworkConfig(net, items);
        }
        break;

    default:
        break;
    }

    return false;
}

bool zmController::setParameter(VariantMapParameter parameter, QVariantMap value)
{
    switch (parameter)
    {
        case ParamHAEndpoint:
        {
            netEdit()->setHAConfig(value);
        }
        break;

        case ParamLinkKey:
        {
            if (!value.contains(QLatin1String("mac")))
            {
                return false;
            }

            if (!value.contains(QLatin1String("key")))
            {
                return false;
            }

            const uint64_t mac = value.value(QLatin1String("mac")).toULongLong();
            const QByteArray key = QByteArray::fromHex(value.value(QLatin1String("key")).toByteArray());

            DBG_Assert(mac != 0);
            DBG_Assert(key.size() == 16);
            if (mac == 0 || key.size() != 16)
            {
                return false;
            }

            uint8_t buf[8 + 16]; // 8-bytes mac + 16-bytes key
            put_u64_le(buf, &mac);
            memcpy(buf + 8, key.constData(), 16);

            if (deCONZ::master()->writeParameter(ZM_DID_STK_LINK_KEY, buf, sizeof(buf)) == 0)
            {
                return true;
            }
        }
            break;
    }

    return false;
}

bool zmController::setParameter(StringParameter parameter, const QString &value)
{
    switch (parameter)
    {
        case ParamDeviceName:
        {
            m_devName = value;
        }
        break;

    default:
        break;
    }

    return false;
}

QVariantMap zmController::getParameter(VariantMapParameter parameter, int index)
{
    QVariantMap result;

    switch (parameter)
    {
        case ParamHAEndpoint:
        {
            result = netEdit()->getHAConfig(index);
            break;
        }
    default:
        break;
    }

    return result;
}

void zmController::addSourceRoute(const std::vector<zmgNode *> gnodes)
{
    Q_ASSERT(gnodes.size() >= 3);
    Q_ASSERT(gnodes.front() == m_nodes.front().g);

    std::vector<Address> hops;

    for (size_t i = 1; i < gnodes.size() - 1; i++)
    {
        hops.push_back(gnodes.at(i)->data()->address());
    }

    const auto *dest = gnodes.back();
    hops.push_back(dest->data()->address()); // last hop is destination

    while (!dest->data()->sourceRoutes().empty())
    {
        const auto sr = dest->data()->sourceRoutes().back();
        dest->data()->removeSourceRoute(sr.uuidHash());
        emit sourceRouteDeleted(sr.uuid());
    }

    SourceRoute sr(createUuid(QLatin1String("user-")), 0, hops);
    for (size_t i = 0; i < sr.hops().size(); i++)
    {
        sr.m_hopLqi[i] = 200; // initial to work
    }
    const auto ret = dest->data()->addSourceRoute(sr);

    if (ret == 0)
    {
        DBG_Printf(DBG_INFO, "source route added to %s\n", qPrintable(dest->data()->userDescriptor()));
        m_routes.push_back(sr);
        emit sourceRouteChanged(sr);
    }
    else if (ret == 1)
    {
        DBG_Printf(DBG_INFO, "source route updated for %s\n", qPrintable(dest->data()->userDescriptor()));
        emit sourceRouteChanged(sr);
    }
    else
    {
        DBG_Printf(DBG_INFO, "failed to add source route to %s\n", qPrintable(dest->data()->userDescriptor()));
    }

    if (ret == 0 || ret == 1)
    {
        emit sourceRouteCreated(sr);
    }
}

void zmController::removeSourceRoute(zmgNode *gnode)
{
    if (!gnode || !gnode->data())
    {
        return;
    }

    if (gnode->data()->sourceRoutes().empty())
    {
        return;
    }

    QString uuid = gnode->data()->sourceRoutes().front().uuid();
    uint srHash = gnode->data()->sourceRoutes().front().uuidHash();

    if (gnode->data()->removeSourceRoute(srHash) == 0)
    {
        emit sourceRouteDeleted(uuid);
    }
    else
    {
        DBG_Printf(DBG_INFO, "failed to remove source route from %s\n", gnode->data()->extAddressString().c_str());
    }
}

void zmController::activateSourceRoute(const SourceRoute &sourceRoute)
{
    if (sourceRoute.hops().size() < 2 || !sourceRoute.isValid())
    {
        return; // at least two hops
    }

    auto *dest = getNode(sourceRoute.hops().back(), deCONZ::ExtAddress);
    if (dest && dest->data)
    {
        auto *sr = SR_GetRouteForUuidHash(m_routes, sourceRoute.uuidHash());

        if (!sr)
        {
            SourceRoute sr1 = sourceRoute;
            for (size_t i = 0; i < sr1.hops().size() && i < SourceRoute::MaxHops; i++)
            {
                sr1.m_hopLqi[i] = 210; // hack to get route working after restart
            }

            m_routes.push_back(sr1);
            sr = &m_routes.back();
        }

        if (dest->data->sourceRoutes().empty())
        {
           dest->data->addSourceRoute(*sr);
           emit sourceRouteChanged(*sr);
        }
    }
}

void zmController::addBinding(const Binding &binding)
{
    deCONZ::Address addr;
    addr.setExt(binding.srcAddress());

    auto *node = getNode(addr, deCONZ::ExtAddress);

    if (node && node->data)
    {
        if (node->data->bindingTable().add(binding))
        {
            if (binding.dstAddress().hasExt())
            {
                const auto i = std::find(m_bindLinkQueue.cbegin(), m_bindLinkQueue.cend(), addr);
                if (i == m_bindLinkQueue.cend())
                {
                    m_bindLinkQueue.push_back(addr);
                }
            }
        }
    }
}

void zmController::removeBinding(const Binding &binding)
{
    deCONZ::Address addr;
    addr.setExt(binding.srcAddress());

    auto *node = getNode(addr, deCONZ::ExtAddress);

    if (node && node->data)
    {
        if (node->data->bindingTable().remove(binding))
        {
            if (binding.dstAddress().hasExt())
            {
                const auto i = std::find(m_bindLinkQueue.cbegin(), m_bindLinkQueue.cend(), addr);
                if (i == m_bindLinkQueue.cend())
                {
                    m_bindLinkQueue.push_back(addr);
                }
            }
        }
    }
}

uint16_t zmController::getParameter(U16Parameter parameter)
{
    if (!_netModel)
    {
        return 0;
    }

    const zmNet &net = _netModel->currentNetwork();

    switch (parameter)
    {
    case ParamPANID:
        return net.pan().nwk();

    case ParamNwkAddress:
        return net.ownAddress().nwk();

    case ParamHttpPort:
        return deCONZ::master()->httpServerPort();

    default:
        DBG_Printf(DBG_ERROR, "Unknown 16-bit parameter %d\n", (int)parameter);
        break;
    }
    return 0;
}

uint32_t zmController::getParameter(U32Parameter parameter)
{
    if (!_netModel)
    {
        return 0;
    }

    const zmNet &net = _netModel->currentNetwork();

    switch (parameter)
    {
    case ParamChannelMask:
        return net.channelMask();

    case ParamFirmwareVersion:
    {
        if (deCONZ::master()->connected())
        {
            return deCONZ::master()->deviceFirmwareVersion();
        }
        else
        {
            return 0;
        }
    }

    case ParamFrameCounter:
        if (deCONZ::master()->connected())
        {
            return m_frameCounter;
        }
        break;

    default:
        DBG_Printf(DBG_ERROR, "Unknown 32-bit parameter %d\n", (int)parameter);
        break;
    }
    return 0;
}

uint64_t zmController::getParameter(U64Parameter parameter)
{
    if (!_netModel)
    {
        return 0;
    }

    const zmNet &net = _netModel->currentNetwork();

    switch (parameter)
    {
    case ParamApsUseExtendedPANID:
        return net.panAps().ext();

    case ParamExtendedPANID:
        return net.pan().ext();

    case ParamMacAddress:
        return net.ownAddress().ext();

    case ParamTrustCenterAddress:
        return net.trustCenterAddress().ext();

    default:
        DBG_Printf(DBG_ERROR, "Unknown 64-bit parameter %d\n", (int)parameter);
        break;
    }

    return 0;
}

QString zmController::getParameter(StringParameter parameter)
{
    switch (parameter)
    {
    case deCONZ::ParamHttpRoot:
        return deCONZ::master()->httpServerRoot();

    case deCONZ::ParamDeviceName:
        if (!deCONZ::master()->deviceName().isEmpty())
        {
            return deCONZ::master()->deviceName();
        }
        return m_devName;

    case deCONZ::ParamDevicePath:
        return deCONZ::master()->devicePath();

    default:
        DBG_Printf(DBG_ERROR, "Unknown string parameter %d\n", (int)parameter);
        break;
    }
    return {};
}

QByteArray zmController::getParameter(ArrayParameter parameter)
{
    if (!_netModel)
    {
        return 0;
    }

    const zmNet &net = _netModel->currentNetwork();

    switch (parameter)
    {
    case ParamNetworkKey:
        return net.networkKey();

    case ParamTrustCenterLinkKey:
        return net.trustCenterLinkKey();

    case ParamSecurityMaterial0:
        return m_securityMaterial0;

    default:
        DBG_Printf(DBG_ERROR, "Unknown array parameter %d\n", (int)parameter);
        break;
    }

    return QByteArray();
}

/*!
    Fills missing fields (nwk or ext) of the addr object.

    \returns Success - if missing part was filled in
             ErrorNotFound - if nwk or extended address is unknown
 */

int zmController::resolveAddress(Address &addr)
{
    NodeInfo *ni = 0;

    if (addr.isNwkUnicast())
    {
        ni = getNode(addr, deCONZ::NwkAddress);
    }

    if (!ni && addr.hasExt() && (addr.ext() != 0))
    {
        ni = getNode(addr, deCONZ::ExtAddress);
    }

    if (ni)
    {
        if (addr.hasExt() && ni->data->address().isNwkUnicast())
        {
            addr.setNwk(ni->data->address().nwk());
            return 0;
        }

        if (addr.isNwkUnicast() && ni->data->address().hasExt())
        {
            addr.setExt(ni->data->address().ext());
            return deCONZ::Success;
        }
    }

    if (!addr.hasExt() && addr.hasNwk())
    {
        for (size_t i = 0; i < m_nodes.size(); i++)
        {
            ni = &m_nodes[i];
            deCONZ::Node *ndata = ni->data;
            if (!ndata)
                continue;

            for (size_t j = 0; j < ndata->neighbors().size(); j++)
            {
                const deCONZ::NodeNeighbor &neib = ndata->neighbors()[j];
                if (neib.address().nwk() == addr.nwk())
                {
                    addr.setExt(neib.address().ext());
                    return deCONZ::Success;
                }
            }
        }
    }

    return deCONZ::ErrorNotFound;
}

State zmController::networkState()
{
    return deCONZ::master()->netState();
}

int zmController::setNetworkState(State state)
{
    if (deCONZ::master()->connected())
    {
        if (state == deCONZ::InNetwork)
        {
            deCONZ::master()->joinNetwork();
            return deCONZ::Success;
        }
        else if (state == deCONZ::NotInNetwork)
        {
            deCONZ::master()->leaveNetwork();
            return deCONZ::Success;
        }
    }

    return deCONZ::ErrorNotConnected;
}

int zmController::setPermitJoin(uint8_t duration)
{
    if (deCONZ::master()->connected())
    {
        if (deCONZ::master()->writeParameter(ZM_DID_STK_PERMIT_JOIN, &duration, sizeof(duration)) == 0)
        {
            zmNetDescriptorModel *model = deCONZ::netModel();

            // update value in net descriptor model
            if (model)
            {
                zmNet &net = model->currentNetwork();
                if (net.permitJoin() != duration)
                {
                    net.setPermitJoin(duration);
                    model->setCurrentNetwork(net);
                }
            }

            return deCONZ::Success;
        }
    }
    return deCONZ::ErrorNotConnected;
}

int zmController::getNode(int index, const Node **node)
{
    DBG_Assert(node != 0);

    if (!node)
    {
        return -1;
    }

    if ((index >= 0) && (index < (int)m_nodes.size()))
    {
        DBG_Assert(m_nodes[index].data != 0);

        if (m_nodes[index].data)
        {
            *node = m_nodes[index].data;
            return 0;
        }
    }

    return -1;
}

bool zmController::updateNode(const Node &node)
{
    std::vector<NodeInfo>::iterator i = m_nodes.begin();
    std::vector<NodeInfo>::iterator end = m_nodes.end();

    for (;i != end; ++i)
    {
        if (i->data->address().ext() == node.address().ext())
        {
            *(i->data) = node; // copy public api parts
            return true;
        }
    }

    return false;
}

void zmController::deviceConnected()
{
    if (m_nodes.empty())
    {
        return;
    }
}

void zmController::deviceDisconnected(int)
{

}

int zmController::apsQueueSize()
{
    return int(m_apsRequestQueue.size());
}

/* ensure unique not currently used id */
uint8_t zmController::nextRequestId()
{
    static uint8_t apsDataRequestId = 0;

    for (int i = 0; i < 255; i++)
    {
        // ID must be non-zero
        if (apsDataRequestId == 0)
        {
            apsDataRequestId = 1;
        }
        else
        {
            apsDataRequestId++;
        }

        const auto r = std::find_if(m_apsRequestQueue.cbegin(), m_apsRequestQueue.cend(), [&](const deCONZ::ApsDataRequest &x)
        {
            return x.id() == apsDataRequestId;
        });

        if (r == m_apsRequestQueue.cend())
            break;

        DBG_Printf(DBG_APS, "APS prevent duplicate req id: %u\n", apsDataRequestId);
    }

    return apsDataRequestId;
}

/*!
    APSDE-DATA.request.

    \retval Success - request is enqued and will be processed
    \retval ErrorNotConnected - not connected
    \retval ErrorQueueIsFull - queue is full
    \retval ErrorNodeIsZombie - destionation node is zombie node, only ZDP requests are allowed
 */
int zmController::apsdeDataRequest(const deCONZ::ApsDataRequest &req)
{
    if (!deCONZ::master()->connected())
        return deCONZ::ErrorNotConnected;

    if (m_master->hasFreeApsRequest())
    {
        //m_apsBusyCounter /= 2;
    }
    else if (m_apsBusyCounter > 0)
    {
        sendNextLater();
        return deCONZ::ErrorQueueIsFull;
    }

    if (checkIdOverFlowApsDataRequest(req))
    {
        sendNextLater();
        return deCONZ::ErrorQueueIsFull;
    }

    NodeInfo *node = nullptr;
    bool enableApsAck = false;

    if (!req.dstAddress().isNwkBroadcast() && !req.dstAddress().hasGroup())
    {
        node = getNode(req.dstAddress(), deCONZ::NoAddress);

        if (node && node->data)
        {
            if (node->data->isCoordinator() && node->data != m_nodes[0].data)
            {
                return deCONZ::ErrorNodeIsZombie;
            }

            if (node->data->isZombie())
            {
                if (req.profileId() != ZDP_PROFILE_ID)
                {
                    DBG_Printf(DBG_APS, "APS-DATA.request rejected, destination %s is zombie node\n", node->data->extAddressString().c_str());
                    return deCONZ::ErrorNodeIsZombie;
                }
            }

            if (node->data == m_nodes[0].data)
            {
                // not for us
            }
            else if (!deCONZ::netEdit()->apsAcksEnabled())
            {
                // leave as is
            }
            else if (node->data->nodeDescriptor().receiverOnWhenIdle() && node->data->recvErrors() > 0)
            {
                if (!(req.txOptions() & deCONZ::ApsTxAcknowledgedTransmission))
                {
                    enableApsAck = true;
                }
            }

            if (m_sourceRoutingEnabled && m_sourceRouteRequired)
            {
                if (node->data->sourceRoutes().empty() && node->data->isRouter())
                {
                    return deCONZ::ErrorQueueIsFull;
                }
            }
        }        
    }

    {
        // count requests which are not yet sent
        auto i = m_apsRequestQueue.cbegin();
        const auto end = m_apsRequestQueue.cend();

        int queueSize = 0;
        int queueSizeIdle = 0;

        for (; i != end; ++i)
        {
            if (node && node->data && !i->confirmed())
            {
                if ((i->dstAddress().hasExt() && i->dstAddress().ext() == node->data->address().ext()) ||
                    (i->dstAddress().hasNwk() && i->dstAddress().nwk() == node->data->address().nwk()))
                {
                    queueSize++;
                }
            }

            if (i->state() == deCONZ::IdleState)
            {
                queueSizeIdle++;
            }
        }

        if (queueSizeIdle > MaxApsRequests)
        {
            DBG_Printf(DBG_APS, "reject aps request queue is full (%d)\n", (int)m_apsRequestQueue.size());
            sendNextLater();
            return deCONZ::ErrorQueueIsFull;
        }

        if (node && node->data)
        {
            const auto tl = node->data->lastSeen();
            if (!isValid(tl) && queueSize > 1)
            {
                sendNextLater();
                return deCONZ::ErrorQueueIsFull;
            }

            if (!(node->data->macCapabilities() & deCONZ::MacReceiverOnWhenIdle) && (queueSize > 3))
            {
                DBG_Printf(DBG_APS, "reject aps request to enddevice node queue is full (%d)\n", queueSize);
                sendNextLater();
                return deCONZ::ErrorQueueIsFull;
            }
        }
    }

    if (DBG_IsEnabled(DBG_APS))
    {
        char addr[24];
        if (req.dstAddressMode() == deCONZ::ApsNwkAddress)
            snprintf(addr, sizeof(addr), "0x%04X", req.dstAddress().nwk());
        else if (req.dstAddressMode() == deCONZ::ApsGroupAddress)
            snprintf(addr, sizeof(addr), "0x%04X", req.dstAddress().group());
        else if (req.dstAddressMode() == deCONZ::ApsExtAddress)
            snprintf(addr, sizeof(addr), FMT_MAC, FMT_MAC_CAST(req.dstAddress().ext()));
        addr[18] = '\0';

        DBG_Printf(DBG_APS, "APS-DATA.request id: %u, addrmode: 0x%02X, addr: %s, profile: 0x%04X, cluster: 0x%04X, ep: 0x%02X -> 0x%02X queue: %d len: %d tx.options 0x%02X\n", req.id(), (uint8_t)req.dstAddressMode(), addr, req.profileId(), req.clusterId(), req.srcEndpoint(), req.dstEndpoint(), (int)m_apsRequestQueue.size(), req.asdu().size(), (uint8_t)req.txOptions());

        if (DBG_IsEnabled(DBG_APS_L2))
        {
            unsigned asduSize = (unsigned)req.asdu().size();
            if (asduSize > 0 && asduSize < 127)
            {
                char asdu[256];
                if (DBG_HexToAscii((const uint8_t*)req.asdu().constData(), uint8_t(asduSize), (uint8_t*)&asdu[0]))
                {
                    DBG_Printf(DBG_APS_L2, "\tasdu (length: %u): %s\n", asduSize, asdu);
                }
            }
        }
    }

    if (deCONZ::master()->connected() && (deCONZ::master()->netState() == deCONZ::InNetwork))
    {
        m_steadyTimeRef = deCONZ::steadyTimeRef();
        m_apsRequestQueue.push_back(req);
        auto &req2 = m_apsRequestQueue.back();

        if (req.clusterId() == 0x0019 && req.asdu().length() > 3 && req.asdu().at(2) == 0x05) // treat OTA img block response as high priority
        {
            m_otauActivity = (3000 / TickMs);
            m_zombieDelay = (MAX_ZOMBIE_DELAY / TickMs);

            if (node && node->g)
            {
                node->g->setOtauActive(m_steadyTimeRef);
            }
        }
        else
        {
            if (req2.dstAddressMode() == deCONZ::ApsGroupAddress || req2.dstAddress().isNwkBroadcast())
            {
            }
            else if (enableApsAck)
            {
                req2.setTxOptions(deCONZ::ApsTxAcknowledgedTransmission);
            }

            emit apsdeDataRequestEnqueued(req2);
        }

        req2.setSendAfter(m_steadyTimeRef + deCONZ::TimeMs{req2.sendDelay()});

        if (!sendNextApsdeDataRequest())
        {
            sendNextLater();
        }

        return deCONZ::Success;
    }

    return deCONZ::ErrorNotConnected;
}

int zmController::checkIdOverFlowApsDataRequest(const deCONZ::ApsDataRequest &req)
{
    auto i = std::find_if(m_apsRequestQueue.cbegin(), m_apsRequestQueue.cend(), [req](const deCONZ::ApsDataRequest &x) {
        return x.id() == req.id();
    });

    return i != m_apsRequestQueue.cend();
}

/*!
    APSDE-DATA.request was handled by device.
 */
void zmController::apsdeDataRequestDone(uint8_t id, uint8_t status)
{
    switch (status)
    {
    case ZM_STATE_SUCCESS:
        // ok do nothing
        m_apsBusyCounter /= 2;
        break;

    default:
    {
        if (status == ZM_STATE_BUSY)
        {
            m_apsBusyCounter++;
            DBG_Printf(DBG_APS, "APS-DATA.request id: %u, status: BUSY (counter: %d)\n", id, m_apsBusyCounter);

            if (m_apsBusyCounter > 50)
            {
                DBG_Printf(DBG_ERROR, "APS TX not working, force leave/join network to reset stack\n");

                m_apsBusyCounter = 0;

                setDeviceState(deCONZ::NotInNetwork);

                QTimer::singleShot(0, deCONZ::master(), SLOT(leaveNetwork()));
                QTimer::singleShot(5000, deCONZ::master(), SLOT(joinNetwork()));
            }
        }
        else
        {
            // giveup
            DBG_Printf(DBG_APS, "APS-DATA.request id: %u, status: 0x%02X giveup\n", id, status);

        }

        if (apsdeDataRequestQueueSetStatus(id, deCONZ::FailureState))
        {
            emitApsDataConfirm(id, deCONZ::ApsTableFullStatus);
        }
    }
        break;
    }
}

bool zmController::apsdeDataRequestQueueSetStatus(int id, deCONZ::CommonState state)
{
    auto i = m_apsRequestQueue.begin();
    const auto end = m_apsRequestQueue.end();

    for (; i != end; ++i)
    {
        if (i->id() == id)
        {
            DBG_Printf(DBG_APS, "APS-DATA.request id: %u, set state: 0x%02X\n", id, state);
            i->setState(state);
            return true;
        }
    }

    return false;
}

/*!
    APSDE-DATA.confirm.
 */
void zmController::onApsdeDataConfirm(const deCONZ::ApsDataConfirm &confirm)
{
    m_steadyTimeRef = deCONZ::steadyTimeRef();
    emit apsdeDataConfirm(confirm);

    deCONZ::clusterInfo()->apsDataConfirm(confirm);

    if (m_nodes.empty())
    {
        return;
    }

    NodeInfo *node = nullptr;
    deCONZ::Indication indication = deCONZ::IndicateNone;
    m_nodes[0].data->touch(m_steadyTimeRef);

    uint match = 0;
    DBG_Printf(DBG_APS, "APS-DATA.confirm id: %u, status: 0x%02X %s\n", confirm.id(), confirm.status(), deCONZ::ApsStatusToString(confirm.status()));

    if (confirm.status() != deCONZ::ZdpSuccess && confirm.dstEndpoint() == ZDO_ENDPOINT)
    {
        for (FastDiscover &fd : m_fastDiscover)
        {
            if ((confirm.dstAddress().hasExt() && confirm.dstAddress().ext() == fd.addr.ext()) ||
                (confirm.dstAddress().hasNwk() && confirm.dstAddress().nwk() == fd.addr.nwk()))
            {
                fd.errors++;
                break;
            }
        }
    }

    if (confirm.status() == deCONZ::NwkBroadcastTableFullStatus)
    {
        m_apsGroupDelayMs = MaxGroupDelay; // TODO FLS large network don't send NWK address req. broadcasts
    }

    auto i = m_apsRequestQueue.begin();
    const auto end = m_apsRequestQueue.end();

    for (; i != end; ++i)
    {
        if (i->state() == deCONZ::BusyState)
        {
            if (i->id() == confirm.id())
            {
                if (i->srcEndpoint() != confirm.srcEndpoint() ||
                    i->dstEndpoint() != confirm.dstEndpoint())
                {
                    DBG_Printf(DBG_APS, "APS-DATA.confirm id: %u, no match [1]\n", confirm.id());
                    continue;
                }

                if (i->dstAddress().hasNwk() && confirm.dstAddress().hasNwk() &&
                    i->dstAddress().nwk() != confirm.dstAddress().nwk())
                {
                    DBG_Printf(DBG_APS, "APS-DATA.confirm id: %u, no match [2]\n", confirm.id());
                    continue;
                }

                if (i->dstAddress().hasGroup() && confirm.dstAddress().hasGroup() &&
                    i->dstAddress().group() != confirm.dstAddress().group())
                {
                    DBG_Printf(DBG_APS, "APS-DATA.confirm id: %u, no match [3]\n", confirm.id());
                    continue;
                }

                match++;
                i->setConfirmed(true);

                if (confirm.dstAddress().isNwkBroadcast() &&
                    i->profileId() == ZDP_PROFILE_ID && (i->clusterId() == ZDP_NWK_ADDR_CLID))
                {
                    // wait response
                    i->setState(deCONZ::ConfirmedState);
                }
                else if (confirm.dstAddress().isNwkUnicast())
                {
                    node = getNode(confirm.dstAddress(), deCONZ::NoAddress);
                }

                if (node && node->data)
                {
                    switch (confirm.status())
                    {
                    case deCONZ::ApsSuccessStatus:
                    {
                        m_apsBusyCounter /= 2;

                        if (i->txOptions() & ApsTxAcknowledgedTransmission)
                        {
                            node->g->setLastSeen(m_steadyTimeRef.ref);
                            node->data->touch(m_steadyTimeRef);
                            node->data->resetRecErrors();
                        }

                        if (i->profileId() == ZDP_PROFILE_ID && (i->clusterId() & 0x8000))
                        {
                            // was a response
                            i->setState(deCONZ::FinishState);
                        }
                        else if (i->dstAddress().isNwkBroadcast() || i->dstAddress().hasGroup())
                        {
                            i->setState(deCONZ::FinishState);
                        }

                        if (node->data->state() != deCONZ::WaitState)
                        {
                            if (i->profileId() == ZDP_PROFILE_ID)
                            {
                                if (i->clusterId() != ZDP_MGMT_LQI_REQ_CLID)
                                {
                                    node->data->setWaitState(MaxZdpTimeout);
                                }
                            }
                            else
                            {
                                node->data->setState(deCONZ::IdleState);
                            }
                        }
                    }
                        break;

                    case deCONZ::ApsNoAckStatus:
                    case deCONZ::MacNoAckStatus:
                    case deCONZ::NwkRouteDiscoveryFailedStatus:
                    {
                        int errorCount = node->data->recvErrorsIncrement();

                        if (node->data->state() != deCONZ::FailureState)
                        {
                            if (errorCount >= MaxRecvErrors)
                            {
                                DBG_Printf(DBG_INFO, "max transmit errors for node %s, last seen by neighbors %d s\n", node->data->extAddressString().c_str(), (int)node->data->lastSeenByNeighbor() / 1000);

                                if (confirm.status() == deCONZ::MacNoAckStatus)
                                {
                                    node->data->setWaitState(60);
                                }
                                else if (confirm.status() == deCONZ::ApsNoAckStatus)
                                {
                                    node->data->setWaitState(120);
                                }
                                else
                                {
                                    node->data->setWaitState(180);
                                }
                            }
                            else
                            {
                                if (confirm.status() == deCONZ::MacNoAckStatus)
                                {
                                    node->data->setWaitState(2);
                                }
                                else if (confirm.status() == deCONZ::ApsNoAckStatus)
                                {
                                    node->data->setWaitState(30);
                                }
                                else
                                {
                                    node->data->setWaitState(60);
                                }
                            }
                        }

                        if (i->sourceRouteUuidHash() != 0)
                        {
                            auto *sourceRoute = SR_GetRouteForUuidHash(m_routes, i->sourceRouteUuidHash());
                            if (sourceRoute)
                            {
                                sourceRoute->incrementErrors();
                            }
                        }

                        if (i->profileId() == ZDP_PROFILE_ID)
                        {
                            deCONZ::RequestId curItem = node->data->curFetchItem();
                            node->data->retryIncr(curItem);
                        }
                    }
                        break;

//                    case deCONZ::ApsInvalidParameterStatus:
                    case deCONZ::MacTransactionExpiredStatus:
                    {
                        DBG_Printf(DBG_APS, "APS-DATA.confirm id: %u status: transaction expired\n", confirm.id());
                        node->data->setWaitState(20);
                    }
                        break;

                    default:
                    {
                        DBG_Printf(DBG_ERROR, "unhandled APS-DATA.confirm id: %u status 0x%02X\n", confirm.id(), confirm.status());
                        if (node->data->state() != deCONZ::FailureState && confirm.status() == deCONZ::MacNoChannelAccess)
                        {
                            node->data->setWaitState(60);
                        }
                    }
                        break;
                    }

                    // deCONZ::nodeModel()->updateNode(*node);
                }

                if (confirm.status() != deCONZ::ApsSuccessStatus)
                {
                    m_apsRequestQueue.erase(i);
                    indication = deCONZ::IndicateError;
                }
                else
                {
                    aps_frames_tx++;

                    if (i->dstAddress().isNwkBroadcast() || i->dstAddress().hasGroup())
                    {
                        if (i->state() == deCONZ::BusyState)
                        {
                            i->setState(FinishState);
                        }

                        if (m_apsGroupDelayMs > MinGroupDelay)
                        {
                            m_apsGroupDelayMs = qMax(MinGroupDelay, m_apsGroupDelayMs - (MaxGroupDelay / 3));
                        }
                    }
                    else if (i->profileId() == ZDP_PROFILE_ID)
                    {
                        if (i->state() != deCONZ::FinishState)
                        {
                            DBG_Assert(i->state() == deCONZ::BusyState);
                            if (i->state() != deCONZ::BusyState)
                            {
                                DBG_Printf(DBG_APS, "APS-DATA.request id: %d -> confirmed, unexpected state %d\n", i->id(), i->state());
                            }

                            if (i->responseClusterId() == 0xfffful)
                            {
                                i->setState(deCONZ::ConfirmedState);
                                //DBG_Printf(DBG_APS, "APS-DATA.confirm request id: %d -> confirmed, timeout %" PRId64 "\n", i->id(), i->timeout().ref);
                            }
                            else // response already received
                            {
                                i->setState(deCONZ::FinishState);
                                //DBG_Printf(DBG_APS, "APS-DATA.confirm request id: %d -> finished, timeout %" PRId64 "\n", i->id(), i->timeout().ref);
                            }
                        }
                    }
                    else
                    {
                        DBG_Printf(DBG_APS, "APS-DATA.confirm request id: %d -> erase from queue\n", i->id());
                        i->setState(FinishState);
                    }
                    indication = deCONZ::IndicateSendDone;
                }
                break;
            }
        }
    }

    if (match != 1)
    {
        DBG_Printf(DBG_APS, "APS-DATA.confirm id: %u, status: 0x%02X, match: %u\n", confirm.id(), confirm.status(), match);
    }

    sendNext();

    visualizeNodeIndication(node, indication);
}

int checkDirectNeighbor(const deCONZ::ApsDataIndication &ind, std::vector<NodeInfo> &nodes)
{
    if (!ind.srcAddress().hasNwk())
    {
        return -1;
    }

    if (ind.srcAddress().nwk() == 0x0000)
    {
        return -2;
    }

    if (ind.srcAddress().nwk() != ind.previousHop())
    {
        return -3;
    }

    auto node = std::find_if(nodes.begin(), nodes.end(), [nodes, &ind](const NodeInfo &n)
    {
        return n.data && n.data->address().nwk() == ind.srcAddress().nwk();
    });

    if (node == nodes.end())
    {
        DBG_Printf(DBG_INFO, "unknown node " FMT_MAC " (0x%04X), lqi: %u\n", FMT_MAC_CAST(ind.srcAddress().ext()), ind.srcAddress().nwk(), ind.linkQuality());
        return 3;
    }

    auto self = nodes.at(0);
    Q_ASSERT(self.data);

    if (self.data->getNeighbor(ind.srcAddress()))
    {
//        DBG_Printf(DBG_INFO, "known neighbor " FMT_MAC " (0x%04X), lqi: %u, rxOnWhenIdle: %u\n", neib.address().ext(), neib.address().nwk(), neib.lqi(), neib.rxOnWhenIdle());
        return 1;
    }
    else
    {
//        DBG_Printf(DBG_ROUTING, "unknown neighbor " FMT_MAC " (0x%04X), lqi: %u, rssi: %d\n", ind.srcAddress().ext(), ind.srcAddress().nwk(), ind.linkQuality(), ind.rssi());
        return 2;
    }
}

static deCONZ::ZclCluster *addMissingCluster(NodeInfo *node, SimpleDescriptor *sd, const deCONZ::ApsDataIndication &ind, const deCONZ::ZclFrame &zclFrame)
{
    deCONZ::ZclCluster *result = nullptr;

    if (zclFrame.isProfileWideCommand() && zclFrame.commandId() == deCONZ::ZclReadAttributesResponseId)
    {
        // this is handled in zclReadAttributesResponse() and won't add clusters for unsupported attributes
        return result;
    }

    if (zclFrame.isDefaultResponse())
    {
        return result;
    }

    if (!sd || !node || !node->isValid())
    {
        return result;
    }

    if (!ind.srcAddress().hasNwk() || ind.srcAddress().nwk() == 0x0000)
    {
        return result;
    }

    if (node->data->nodeDescriptor().isNull())
    {
        return result;
    }

    DBG_Printf(DBG_INFO, "%s missing cluster 0x%04X, frame control 0x%08X\n", node->data->extAddressString().c_str(), ind.clusterId(), zclFrame.frameControl());

    const auto clusterSide = (zclFrame.frameControl() & deCONZ::ZclFCDirectionServerToClient)
            ? deCONZ::ServerCluster : deCONZ::ClientCluster;

    result = sd->cluster(ind.clusterId(), clusterSide);

    if (result)
    {
        return result;
    }

    ZclDataBase *db = deCONZ::zclDataBase();
    if (!db)
    {
        return result;
    }

    // try to append unknown cluster
    const deCONZ::ZclCluster cl = db->inCluster(ind.profileId(), ind.clusterId(), node->data->nodeDescriptor().manufacturerCode());

    if (!cl.isValid())
    {
        return result;
    }

    if (clusterSide == deCONZ::ServerCluster)
    {
        sd->inClusters().push_back(cl);
        result = &sd->inClusters().back();
    }
    else
    {
        sd->outClusters().push_back(cl);
        result = &sd->outClusters().back();
    }

    if (node->g)
    {
        node->g->updated(deCONZ::ReqSimpleDescriptor);
    }

    auto *ctrl = deCONZ::controller();

    NodeEvent event(NodeEvent::UpdatedSimpleDescriptor, node->data, sd->endpoint());
    emit ctrl->nodeEvent(event);

    return result;
}

/*!
    APSDE-DATA.indication.
 */
void zmController::onApsdeDataIndication(const deCONZ::ApsDataIndication &ind)
{
    using namespace deCONZ;

    aps_frames_rx++;

    if (m_nodes.empty())
    {
        return;
    }

    // HACK in rare cases we go out of network despite everything working (TODO check why)
    if (deviceState() != deCONZ::InNetwork)
    {
        setDeviceState(deCONZ::InNetwork);
    }

    m_steadyTimeRef = deCONZ::steadyTimeRef();

    if (ind.dstAddressMode() == deCONZ::ApsGroupAddress || ind.dstAddress().isNwkBroadcast())
    {
        m_apsGroupIndicationTimeRef = m_steadyTimeRef;
    }

    checkDirectNeighbor(ind, m_nodes);

    char srcAddrStr[24];

    if (ind.srcAddress().hasExt())
    {
        snprintf(srcAddrStr, sizeof(srcAddrStr), FMT_MAC, FMT_MAC_CAST(ind.srcAddress().ext()));
    }
    else
    {
        snprintf(srcAddrStr, sizeof(srcAddrStr), "0x%04X", ind.srcAddress().nwk());
    }

    if (ind.srcAddress().hasExt() && !isValidMacAddress(ind.srcAddress().ext()))
    {
        DBG_Printf(DBG_INFO, "ignore packet from invalid mac address: %s\n", srcAddrStr);
        return; // ignore
    }

    if (ind.profileId() == GREEN_POWER_PROFILE_ID && ind.clusterId() == GREEN_POWER_CLUSTER_ID && ind.srcEndpoint() == GREEN_POWER_ENDPOINT)
    {
        deCONZ::ZclFrame &zclFrame = m_zclFrame;
        zclFrame.reset();
        QDataStream stream(ind.asdu());
        stream.setByteOrder(QDataStream::LittleEndian);

        zclFrame.readFromStream(stream);

        if (zclFrame.commandId() == GppCommandIdNotification ||
            zclFrame.commandId() == GppCommandIdCommissioningNotification)
        {
            deCONZ::GreenPowerController *gpCtrl = deCONZ::GreenPowerController::instance();

            if (gpCtrl)
            {
                gpCtrl->processIncomingProxyNotification(zclFrame.payload());
            }
        }
        else
        {
            DBG_Printf(DBG_ZGP, "ZGP proxy command 0x%02X not handled\n", zclFrame.commandId());
        }
    }

    NodeInfo *node = nullptr;
    deCONZ::Indication indication = deCONZ::IndicateNone;
    m_nodes[0].data->touch(m_steadyTimeRef);

    ApsDataRequest apsReq;
    QDataStream stream(ind.asdu());
    stream.setByteOrder(QDataStream::LittleEndian);

    for (auto i = m_apsRequestQueue.begin(); i != m_apsRequestQueue.end(); ++i)
    {
        if ((i->state() != deCONZ::ConfirmedState) && (i->state() != deCONZ::BusyState))
        {
            continue;
        }

        if (!ind.dstAddress().isNwkUnicast())
        {
            continue;
        }

        if (ind.srcAddress().hasNwk() && i->dstAddress().hasNwk() &&
            ind.srcAddress().nwk() != i->dstAddress().nwk())
        {
            continue;
        }

        if (ind.profileId() == ZDP_PROFILE_ID)
        {
            // response?
            if ((i->clusterId() | 0x8000) == ind.clusterId())
            {
                if (i->asdu().size() > 0 && ind.asdu().size() > 0
                        && ((uint8_t)i->asdu()[0] == (uint8_t)ind.asdu()[0])) // check sequence number
                {
                    if (i->confirmed())
                    {
                        i->setState(deCONZ::FinishState);
                        DBG_Printf(DBG_APS, "APS-DATA.indication request id: %d -> finished\n", i->id());
                    }
                    else
                    {
                        DBG_Printf(DBG_APS, "APS-DATA.indication request id: %d -> finished? not confirmed\n", i->id());
                    }
                    i->setResponseClusterId(ind.clusterId()); // mark response received
                    apsReq = *i;
                }

                if (i->dstAddress().hasExt())
                {
                    // mark watchdog when a response from own node is received
                    if (i->dstAddress().ext() == m_nodes[0].data->address().ext())
                    {
                        m_deviceWatchdogOk |= DEVICE_ZDP_LOOPBACK_OK;
                    }
                    // mark watchdog when a response from a network node is received
                    else
                    {
                        // m_deviceWatchdogOk |= DEVICE_RX_NETWORK_OK; // too restrictive in some setups
                    }
                }

                if (!node) // TODO this is duplicated below
                {
                    node = getNode(ind.srcAddress(), deCONZ::NoAddress);
                }

                if (node && i->sourceRouteUuidHash() != 0)
                {
                    auto *sourceRoute = SR_GetRouteForUuidHash(m_routes, i->sourceRouteUuidHash());
                    if (sourceRoute)
                    {
                        sourceRoute->incrementTxOk();
                        if (sourceRoute->txOk() == 1)
                        {
                            node->data->addSourceRoute(*sourceRoute);
                            onSourceRouteChanged(*sourceRoute);
                        }

                        if (sourceRoute->needSave() && !m_otauActive)
                        {
                            emit sourceRouteCreated(*sourceRoute);
                            sourceRoute->saved();
                        }
                    }
                }

                // mark watchdog when any response is received
                m_deviceWatchdogOk |= DEVICE_RX_NETWORK_OK;
                break;
            }
        }
        else if ((i->profileId() == ind.profileId()) &&
                 (i->clusterId() == ind.clusterId())) // TODO: check address
        {
            if (i->confirmed())
            {
                DBG_Printf(DBG_APS, "APS-DATA.indication request id: %d -> finished [2]\n", i->id());
                i->setState(deCONZ::FinishState);
            }
            apsReq = *i;
            break;
        }
        else if ((i->profileId() == ind.profileId()) &&
                 (i->responseClusterId() == ind.clusterId())) // TODO: check address
        {
            if (i->confirmed())
            {
                DBG_Printf(DBG_APS, "APS-DATA.indication request id: %d -> finished [3]\n", i->id());
                i->setState(deCONZ::FinishState);
            }
            apsReq = *i;
            break;
        }
    }

    if (!node)
    {
        node = getNode(ind.srcAddress(), deCONZ::NoAddress);
    }

    if (!node && ind.profileId() != GREEN_POWER_PROFILE_ID)
    {
        DBG_Printf(DBG_INFO, "APS-DATA.indication from unknown node %s\n", srcAddrStr);
    }

    if (node && node->data)
    {
        if (node->g)
        {
            node->g->setLastSeen(m_steadyTimeRef.ref);
        }

        if (ind.dstAddressMode() == deCONZ::ApsGroupAddress)
        {
            for (auto &bnd : node->data->bindingTable())
            {
                if (bnd.clusterId() != ind.clusterId())
                {
                    continue;
                }

                if (bnd.srcEndpoint() != ind.srcEndpoint())
                {
                    continue;
                }

                if (bnd.dstAddress().group() != ind.dstAddress().group())
                {
                    continue;
                }

                if (bnd.dstAddressMode() == deCONZ::ApsGroupAddress)
                {
                    bnd.setConfirmedTimeRef(m_steadyTimeRef);
                    break;
                }
            }
        }

        checkAddressChange(ind.srcAddress(), node);

        if (ind.version() >= 3 && node->data->macCapabilities() != 0 && node->data->isEndDevice())
        {
            if (ind.srcAddress().nwk() == ind.previousHop())
            {
                DBG_Printf(DBG_INFO_L2, "APS-DATA.indication from child 0x%04X\n", ind.srcAddress().nwk());
                verifyChildNode(node);
            }
            else if (node->data->parentAddress().ext() == m_nodes.front().data->address().ext())
            {
                // clear parent address
                node->data->parentAddress().setExt(0);
                node->data->parentAddress().setNwk(0);
            }
        }

        if (node->data->simpleDescriptors().empty() ||
            node->data->endpoints().empty())
        {
            if (node->data->nodeDescriptor().receiverOnWhenIdle())
            {
                fastPrope(node->data->address().ext(),
                          node->data->address().nwk(),
                          node->data->nodeDescriptor().macCapabilities());
            }
        }

        if (node->data->isZombie())
        {
            wakeNode(node);
        }
        else
        {
            node->data->touch(m_steadyTimeRef);
        }

        node->data->resetRecErrors();

        switch (node->data->state())
        {
        case deCONZ::FailureState:
        case deCONZ::BusyState:
        {
            node->data->setState(deCONZ::IdleState);
        }
            break;

        default:
            break;
        }
    }

    for (auto i = m_apsRequestQueue.begin(); i != m_apsRequestQueue.end(); )
    {
        if (i->state() == deCONZ::FinishState)
        {
            if (!i->confirmed())
            {
                i->setTimeout({0}); // emit confirm in timeout handler
                ++i;
            }
            else
            {
                DBG_Printf(DBG_APS, "APS-DATA.request id: %d erase from queue\n", i->id());
                i = m_apsRequestQueue.erase(i);
            }
        }
        else
        {
            ++i;
        }
    }

    if (ind.profileId() == ZDP_PROFILE_ID)
    {
        uint16_t nwk;
        uint8_t seqNum;
        uint8_t status = deCONZ::ZdpSuccess;
        Address addr;
        stream >> seqNum;

        // enable fast progress
        if (node && node->data)
        {
            node->data->setState(deCONZ::IdleState);
        }

        if (ind.clusterId() & 0x8000)
        {
            stream >> status;

            for (deCONZ::ApsDataRequest &req : m_apsRequestQueue)
            {
                if (apsReq.clusterId() == ind.clusterId() && apsReq.state() == deCONZ::FinishState)
                    continue;

                if (req.state() != deCONZ::ConfirmedState)
                    continue;

                if ((req.clusterId() | 0x8000) != ind.clusterId())
                    continue;

                if (req.dstAddress().hasExt() && ind.srcAddress().hasExt() &&
                    req.dstAddress().ext() != ind.srcAddress().ext())
                {
                    continue;
                }
                else if (req.dstAddress().hasNwk() && ind.srcAddress().hasNwk() &&
                         req.dstAddress().nwk() != ind.srcAddress().nwk())
                {
                    continue;
                }

                if (!req.asdu().isEmpty() && (uint8_t)req.asdu().at(0) == seqNum)
                {
                    DBG_Printf(DBG_ZDP, "APS-DATA.request id: %u -> finish [4]\n", req.id());
                    req.setState(deCONZ::FinishState);

                    if (apsReq.id() != req.id())
                    {
                        apsReq = req;
                    }
                    break;
                }
                else
                {
                    //DBG_Printf(DBG_ZDP, "cont [5]\n");
                }
            }
        }

        if (ind.clusterId() & 0x8000)
        {
            DBG_Printf(DBG_ZDP, "ZDP %s cluster: 0x%04X status = 0x%02X -> %s\n", srcAddrStr, ind.clusterId(), status, deCONZ::ApsStatusToString(status));
        }

        switch (ind.clusterId())
        {
        case ZDP_END_DEVICE_BIND_REQ_CLID:
        {
        }
            break;

        case ZDP_IEEE_ADDR_CLID:
        case ZDP_NWK_ADDR_CLID:
        {
        }
            break;

        case ZDP_DEVICE_ANNCE_CLID:
            {
                quint64 ext;
                uint8_t macCapabilities;

                stream >> nwk;
                stream >> ext;
                stream >> macCapabilities;

                if (!isValidMacAddress(ext))
                {
                    return; // ignore
                }

                MacCapabilities cap;

                if (macCapabilities & MacAlternatePanCoordinator) cap |= MacAlternatePanCoordinator;
                if (macCapabilities & MacDeviceIsFFD)             cap |= MacDeviceIsFFD;
                if (macCapabilities & MacIsMainsPowered)          cap |= MacIsMainsPowered;
                if (macCapabilities & MacReceiverOnWhenIdle)      cap |= MacReceiverOnWhenIdle;
                if (macCapabilities & MacSecuritySupport)         cap |= MacSecuritySupport;
                if (macCapabilities & MacAllocateAddress)         cap |= MacAllocateAddress;

                addr.setExt(ext);
                addr.setNwk(nwk);

                DBG_Printf(DBG_ZDP, "ZDP device announce: " FMT_MAC ", 0x%04X, 0x%02X\n", FMT_MAC_CAST(ext), nwk, macCapabilities);

                if (!node)
                {
                    node = getNode(addr, deCONZ::NoAddress);
                }

                checkDeviceAnnce(addr, cap);

                if (node && node->data && node->g)
                {
                    node->data->setMacCapabilities(cap);
                    node->data->touch(m_steadyTimeRef);

                    if (macCapabilities & MacDeviceIsFFD)
                    {
                        //node->data->setWaitState(5); // wait a few seconds
                        //node->data->resetItem(deCONZ::ReqActiveEndpoints);
                        node->data->setFetched(deCONZ::ReqActiveEndpoints, false);
                        node->data->setActiveEndpoints(node->data->endpoints());
                    }
                }
                else
                {
                    NodeInfo n = createNode(addr, cap);
                    if (!n.isValid())
                    {
                        return;
                    }                   
                }
                fastPrope(ext, nwk , macCapabilities);
            }
            break;

        case ZDP_PARENT_ANNOUNCE_CLID:
        {
            uint8_t numberOfChildren;
            quint64 ext;

            stream >> numberOfChildren;

            DBG_Printf(DBG_ZDP, "Parent_annce from %s child count: %u\n", srcAddrStr, unsigned(numberOfChildren));

            for (unsigned i = 0; i < numberOfChildren && !stream.atEnd(); i++)
            {
                stream >> ext;
                DBG_Printf(DBG_ZDP, "\t [%u] " FMT_MAC "\n", i, FMT_MAC_CAST(ext));

                if (!isValidMacAddress(ext))
                {
                    continue; // ignore
                }
            }
        }
            break;

        case ZDP_IEEE_ADDR_RSP_CLID:
        case ZDP_NWK_ADDR_RSP_CLID:    // responses are identical
        {
            deCONZ::RequestId reqId = (ind.clusterId() == ZDP_IEEE_ADDR_RSP_CLID) ? deCONZ::ReqIeeeAddr : deCONZ::ReqNwkAddr;

            if (status == deCONZ::ZdpSuccess)
            {
                quint64 ext;

                stream >> ext;
                stream >> nwk;

                addr.setExt(ext);
                addr.setNwk(nwk);

                DBG_Printf(DBG_ZDP, "ZDP %s_addr_rsp: ext: " FMT_MAC ", nwk: 0x%04X\n",
                           (ind.clusterId() == ZDP_IEEE_ADDR_RSP_CLID ? "IEEE" : "NWK"), FMT_MAC_CAST(ext), nwk);

                checkAddressChange(addr);

                // check if this is a extended response
                if (ind.asdu().size() > (1 + 1 + 8 + 2 + 1 + 1))
                {
                    uint8_t numAssocDev;
                    uint8_t startIndex;

                    stream >> numAssocDev;
                    stream >> startIndex;

                    DBG_Printf(DBG_ZDP, "(IEEE | NWK )_addr_rsp extended %s numAssocDev %u startIndex %u\n", srcAddrStr, numAssocDev, startIndex);

                    uint8_t i = numAssocDev;

                    if (startIndex < numAssocDev)
                    {
                        i -= startIndex;
                    }

                    while (!stream.atEnd() && i)
                    {
                        uint16_t assocNwk;
                        AddressPair addrPair;

                        stream >> assocNwk;
                        addrPair.aAddr = addr;
                        addrPair.bAddr.setExt(0);
                        addrPair.bAddr.setNwk(assocNwk);

                        node = getNode(addr, deCONZ::NwkAddress);

                        if (node && node->data)
                        {
                            addrPair.bAddr.setExt(node->data->address().ext());
                            addDeviceDiscover(addrPair);
                            if (!gHeadlessVersion) // no links here
                            {
                                m_createLinkQueue.append(addrPair);
                            }
                        }
                        i--;

                        DBG_Printf(DBG_ZDP, "(IEEE | NWK )_addr_rsp %s - 0x%04X\n", srcAddrStr, assocNwk);
                    }
                }

                node = getNode(addr, deCONZ::ExtAddress);

                if (node && node->data)
                {
                    node->data->setFetched(reqId, true);
                    node->data->touch(m_steadyTimeRef);
                }
            }
            else
            {
                if (node && node->data)
                {
                    node->data->retryIncr(reqId);
                }
            }
        }
            break;
        case ZDP_NODE_DESCRIPTOR_CLID:
            break;

        case ZDP_NODE_DESCRIPTOR_RSP_CLID:
        {
            stream >> nwk;
            addr.setNwk(nwk);
            node = getNode(addr, deCONZ::NwkAddress);

            DBG_Assert(node);
            DBG_Printf(DBG_ZDP, "ZDP Node_Descriptor_rsp %s - 0x%04X\n", srcAddrStr, nwk);

            if (status == deCONZ::ZdpSuccess)
            {
                if (node)
                {
                    NodeDescriptor nd;
                    nd.readFromStream(stream);
                    if (!nd.isNull())
                    {
                        node->data->setNodeDescriptor(nd);
                        node->data->setMacCapabilities(nd.macCapabilities());
                        node->data->setFetched(ReqNodeDescriptor, true);
                        node->g->requestUpdate(); // redraw

                        NodeEvent event(NodeEvent::UpdatedNodeDescriptor, node->data);
                        emit nodeEvent(event);
                    }
                }
            }
            else
            {
                if (node)
                {
                    node->data->retryIncr(deCONZ::ReqNodeDescriptor);
                }
            }
        }
            break;

        case ZDP_POWER_DESCRIPTOR_RSP_CLID:
        {
            stream >> nwk;
            addr.setNwk(nwk);
            node = getNode(addr, deCONZ::NwkAddress);

            if (status == deCONZ::ZdpSuccess)
            {
                if (node)
                {

                    QByteArray arr(ind.asdu());
                    arr.remove(0, 4); // seq, status, nwk
                    node->data->setPowerDescriptor(arr);
                    node->data->setFetched(deCONZ::ReqPowerDescriptor, true);
                    node->g->requestUpdate(); // redraw

                    NodeEvent event(NodeEvent::UpdatedPowerDescriptor, node->data);
                    emit nodeEvent(event);
                }
            }
            else
            {
                if (node)
                {
                    node->data->retryIncr(deCONZ::ReqPowerDescriptor);
                }
            }
        }
            break;

        case ZDP_SIMPLE_DESCRIPTOR_RSP_CLID:
        {
            stream >> nwk;
            addr.setNwk(nwk);
            node = getNode(addr, deCONZ::NwkAddress);

            DBG_Assert(node);
            DBG_Printf(DBG_ZDP, "ZDP Simple_Descriptor_rsp %s - 0x%04X\n", srcAddrStr, nwk);

            if (!node || !node->data)
            {

            }
            else if (status == deCONZ::ZdpSuccess)
            {
                uint8_t len;
                stream >> len;
                SimpleDescriptor sd;
                sd.readFromStream(stream, node->data->nodeDescriptor().manufacturerCode());
                node->data->removeFetchEndpoint(sd.endpoint());

                if (!sd.isValid())
                {
                    DBG_Printf(DBG_ZDP, "ZDP Simple_Descriptor_rsp %s is invalid\n", srcAddrStr);
                }
                else
                {
                    // seen for Bosch devices wich use 0xFFFF but this s also the mark that it is invalid
                    if (sd.deviceId() == 0xFFFF)
                    {
                        // change to 0xFFFE to keep things going
                        sd.setDeviceId(0xFFFE);
                    }
                }

                if (sd.isValid() && node->data->setSimpleDescriptor(sd))
                {
                    node->g->updated(deCONZ::ReqSimpleDescriptor);
                    queueSaveNodesState();
                    m_saveNodesTimer->stop();
                    m_saveNodesTimer->start(10000);

                }
                if (node->data->getNextUnfetchedEndpoint() == -1)
                {
                    node->data->setFetched(deCONZ::ReqSimpleDescriptor, true);
                }

                if (sd.isValid())
                {
                    NodeEvent event(NodeEvent::UpdatedSimpleDescriptor, node->data, sd.endpoint(), ind.profileId(), ind.clusterId());
                    emit nodeEvent(event);
                }
            }
            else if (status == deCONZ::ZdpNotActive)
            {
                const auto notActiveEndpoint = node->data->getNextUnfetchedEndpoint();
                if (notActiveEndpoint > 0 && notActiveEndpoint <= 255)
                {
                    DBG_Printf(DBG_ZDP, "ZDP endpoint 0x%02X not active on %s, remove from list\n", notActiveEndpoint, srcAddrStr);
                    std::vector<uint8_t> activeEndpoints;
                    std::copy_if(node->data->endpoints().begin(), node->data->endpoints().end(),
                                 std::back_inserter(activeEndpoints), [notActiveEndpoint](quint8 ep)
                    {
                        return notActiveEndpoint != ep;
                    });
                    node->data->setActiveEndpoints(activeEndpoints);
                }
            }
            else
            {
                node->data->removeFetchEndpoint(255);
                node->data->retryIncr(deCONZ::ReqSimpleDescriptor);
            }
        }
            break;

        case ZDP_ACTIVE_ENDPOINTS_RSP_CLID:
        {
            stream >> nwk;
            addr.setNwk(nwk);
            node = getNode(addr, deCONZ::NwkAddress);

            if (node)
            {
                DBG_Printf(DBG_ZDP, "ZDP active ep response for %s\n", srcAddrStr);
            }

            if (status == deCONZ::ZdpSuccess)
            {
                if (node)
                {
                    uint8_t epCount;
                    std::vector<uint8_t> activeEndpoints;

                    stream >> epCount;

                    for (int i = 0; i < epCount; i++)
                    {
                        if (!stream.atEnd())
                        {
                            uint8_t ep;
                            stream >> ep;
                            activeEndpoints.push_back(ep);
                            DBG_Printf(DBG_ZDP, "\tep: 0x%02X\n", ep);
                        }
                    }

                    node->data->setFetched(deCONZ::ReqActiveEndpoints, true);

                    if (activeEndpoints != node->data->endpoints())
                    {
                        // remove simple descriptors which which are no longer present
                        auto i = node->data->simpleDescriptors().begin();

                        for ( ;i != node->data->simpleDescriptors().end(); )
                        {
                            if (std::find(activeEndpoints.begin(), activeEndpoints.end(), i->endpoint()) == activeEndpoints.end())
                            {
                                // endpoint not available anymore
                                i = node->data->simpleDescriptors().erase(i);
                            }
                            else
                            {
                                ++i;
                            }
                        }

                        node->g->updated(deCONZ::ReqSimpleDescriptor);
                        NodeEvent event(NodeEvent::UpdatedClusterData, node->data, ind);
                        emit nodeEvent(event);
                    }

                    node->data->setActiveEndpoints(activeEndpoints);
                    if (node->data->nodeDescriptor().receiverOnWhenIdle() /*&&
                        node->data->nodeDescriptor().manufacturerCode() == 0x1135*/)
                    {
                        // always fetch simple descriptors, there might be some new/changed ones
                        node->data->setFetched(deCONZ::ReqSimpleDescriptor, false);
                    }
                }
                else
                {
                    DBG_Printf(DBG_ZDP, "ZDP %s active ep response for unknown address: 0x%04X\n", Q_FUNC_INFO, nwk);
                }
            }
            else
            {
                if (node)
                {
                    node->data->retryIncr(deCONZ::ReqActiveEndpoints);
                }
            }
        }
            break;

        case ZDP_MGMT_RTG_RSP_CLID:
        {
            if (status != deCONZ::ZdpSuccess)
            {
                return;
            }

            uint8_t rtgEntries;
            uint8_t startIndex;
            uint8_t rtgListCount;

            stream >> rtgEntries;
            stream >> startIndex;
            stream >> rtgListCount;

            if (stream.status() == QDataStream::ReadPastEnd)
            {
                return;
            }

            DBG_Printf(DBG_ZDP, "ZDP Mgmt_Rtg_rsp zdpSeq: %u from %s total: %u, startIndex: %u, listCount: %u\n", seqNum, srcAddrStr, rtgEntries, startIndex, rtgListCount);

            if (!node || !node->g || !node->data)
            {
                DBG_Printf(DBG_ZDP, "\tno NodeInfo found, abort\n");
                return;
            }

            if (startIndex == 0)
            {
                node->data->routes().clear(); // new reading
            }

            for (size_t i = 0; i < rtgListCount; i++)
            {
                uint8_t info;
                deCONZ::RoutingTableEntry e;

                stream >> e.dstAddress;
                stream >> info;
                stream >> e.nextHopAddress;

                e.status = info & 0x7;
                e.memConstraint = info & (1 << 3);
                e.manyToOne = info & (1 << 4);
                e.routeRecordRequired = info & (1 << 5);

                // info bits:
                //    0-2 status (0 = active, 1 = discovery under way, 2 = discovery failed, 3 = inactive, 4 = validation under way)
                //      3 memory constraint
                //      4 many-to-one
                //      5 route record required
                //    6-7 reserved

                DBG_Printf(DBG_ZDP, "\tdst: 0x%04X, status %u, mem-constraint: %u, many-to-one %u, route-record-required %u, next-hop: 0x%04X\n",
                           e.dstAddress, e.status, e.memConstraint, e.manyToOne, e.routeRecordRequired, e.nextHopAddress);

                node->data->routes().push_back(e);

                if (e.status != 0)
                {
                    continue; // only handle active
                }

                deCONZ::Address dstAddr;
                dstAddr.setNwk(e.nextHopAddress);
                NodeInfo *nextHop = getNode(dstAddr, deCONZ::NwkAddress);
                if (!nextHop || !nextHop->g)
                {
                    continue; // node not known
                }

                NodeSocket *nodeSock = node->g->socket(zmgNode::NeighborSocket);
                NodeSocket *nextSock = nextHop->g->socket(zmgNode::NeighborSocket);
                if (!nextSock || !nodeSock)
                {
                    continue;
                }

                std::vector<NodeLink*> links;

                for (int lnk = 0; lnk < node->g->linkCount(); lnk++)
                {
                    links.push_back(node->g->link(lnk));
                }

                for (int lnk = 0; lnk < nextHop->g->linkCount(); lnk++)
                {
                    links.push_back(nextHop->g->link(lnk));
                }

                for (NodeLink *&link : links)
                {
                    if (!link || !nextSock || !nodeSock)
                    {
                        continue;
                    }

                    if ((link->src() == nextSock && link->dst() == nodeSock) ||
                        (link->src() == nodeSock && link->dst() == nextSock))
                    {
                        link->setLinkType(NodeLink::LinkRouting);
                    }
                    else
                    {
                        //link->setLinkType(NodeLink::LinkNormal);
                    }
                }
            }

            if ((startIndex + rtgListCount) < rtgEntries)
            {
                sendMgtmRtgRequest(node, startIndex + rtgListCount);
            }
        }
            break;

        case ZDP_MGMT_LQI_RSP_CLID:
        {
            addr = apsReq.dstAddress();

            if (node && node->data && (ind.asdu().size() > 4) && status == deCONZ::ZdpSuccess)
            {
                uint8_t neighEntries;
                uint8_t startIndex;
                uint8_t listCount;

                if (node->data->simpleDescriptors().empty() && !node->data->nodeDescriptor().isNull())
                {
                    fastPrope(node->data->address().ext(), node->data->address().nwk(), node->data->nodeDescriptor().macCapabilities());
                }

                stream >> neighEntries;
                stream >> startIndex;
                stream >> listCount;

                const uint permitJoin = getParameter(ParamPermitJoin);

                if ((startIndex + listCount) >= neighEntries || listCount == 0)
                {
                    // finish
                    node->data->setFetched(deCONZ::ReqMgmtLqi, true);
                    node->data->setMgmtLqiStartIndex(0x00);

                    if (permitJoin == 0)
                    {
                        m_fetchLqiTickMsCounter.start();
                    }
                }
                else
                {
                    // next entries
                    node->data->setMgmtLqiStartIndex(node->data->mgmtLqiStartIndex() + listCount);
                    node->data->setFetched(deCONZ::ReqMgmtLqi, false);
                    if (m_lqiIter > 0)
                    {
                        m_lqiIter--; // select same node again
                    }
                    // fast query of next items
                    if (permitJoin == 0)
                    {
                        m_fetchLqiTickMsCounter.start();

                        bool isCoord = ind.srcAddress().hasNwk() && ind.srcAddress().nwk() == 0x0000;

                        if (isCoord)
                        {
                            // fast discovery
                            m_fetchLqiTickMsCounter.invalidate();
                            deviceDiscoverTick();
                        }
                        else if (isValid(m_lastNodeAdded) && m_steadyTimeRef - m_lastNodeAdded < deCONZ::TimeSeconds{2 * 60})
                        {

                            // fast discovery
                            deviceDiscoverTick();
                        }
                    }
                }

                DBG_Printf(DBG_ZDP, "ZDP Mgmt_Lqi_rsp zdpSeq: %u from %s total: %u, startIndex: %u, listCount: %u\n", seqNum, srcAddrStr, neighEntries, startIndex, listCount);

                AddressPair addrPair;
                addrPair.aAddr = node->data->address();

                const int NeighEntrySize = 22;
                const uint64_t myPan = _netModel->currentNetwork().pan().ext();
                node->data->setMgtmLqiLastRsp(m_steadyTimeRef);

                const char *p = ind.asdu().constData() + 5;

                for (int i = 0; i < listCount; i++)
                {
                    if (ind.asdu().size() >= ((i + 1) * NeighEntrySize) + 4)
                    {
                        zmNeighbor neib(p, NeighEntrySize);
                        neib.setLastSeen(m_steadyTimeRef);
                        addrPair.bAddr = neib.address();
#if QT_VERSION < QT_VERSION_CHECK(5,14,0)
                        addrPair.bMacCapabilities = nullptr;
#endif

                        // valid ext address?
                        if (!neib.address().hasNwk() || !neib.address().hasExt() ||
                            !isValidMacAddress(neib.address().ext()) ||
                            (neib.address().ext() == 0) ||
                            (neib.address().ext() == 0xFFFFFFFFFFFFFFFFLL))
                        {
                            DBG_Printf(DBG_ZDP, "    * ignore neighbor: " FMT_MAC " (0x%04X), LQI: %u, relation: 0x%02X rxOnWHenIdle: %u\n",  FMT_MAC_CAST(neib.address().ext()), neib.address().nwk(), neib.m_lqi, neib.relationship(), neib.rxOnWhenIdle());
                            p += NeighEntrySize;
                            continue;
                        }

                        if (neib.deviceType() == deCONZ::Coordinator ||
                            neib.deviceType() == deCONZ::Router)
                        {
                            addrPair.bMacCapabilities |= MacDeviceIsFFD;
                            addrPair.bMacCapabilities |= MacIsMainsPowered; // assumption
                        }

                        NodeInfo *neibNode = getNode(neib.address(), deCONZ::ExtAddress);

                        if (neib.rxOnWhenIdle() == 1)
                        {
                            addrPair.bMacCapabilities |= MacReceiverOnWhenIdle; // assumption
                        }

                        DBG_Printf(DBG_ZDP, "    * neighbor: " FMT_MAC " (0x%04X), LQI: %u, relation: 0x%02X, depth: %u, rxOnWHenIdle: %u\n",  FMT_MAC_CAST(neib.address().ext()), neib.address().nwk(), neib.m_lqi, neib.relationship(), neib.depth(), neib.rxOnWhenIdle());

                        if (neib.relationship() == deCONZ::UnauthenticatedChildRelation)
                        {
                            DBG_Printf(DBG_ZDP, "    * unauth child: " FMT_MAC "\n", FMT_MAC_CAST(neib.address().ext()));
                        }
                        else if (neib.relationship() == deCONZ::PreviousChildRelation)
                        {
                            DBG_Printf(DBG_ZDP, "    * previous child: " FMT_MAC "\n", FMT_MAC_CAST(neib.address().ext()));
                        }
                        else if (neib.extPanId() == myPan)
                        {
                            node->data->updateNeighbor(neib);

                            if (neib.m_lqi > 0)
                            {
                                if (neibNode && neibNode->data)
                                {
                                    neibNode->data->touchAsNeighbor();

                                    if (neibNode->data->macCapabilities() == 0 /*||
                                        ((!neibNode->data->macCapabilities().testFlag(deCONZ::MacReceiverOnWhenIdle)) && neib.rxOnWhenIdle() > 0)*/)
                                    {
                                        deCONZ::MacCapabilities macCapa;

                                        if (neib.rxOnWhenIdle() == 1)
                                        {
                                            macCapa |= deCONZ::MacReceiverOnWhenIdle;
                                        }

                                        if (neib.deviceType() == deCONZ::Coordinator || neib.deviceType() == deCONZ::Router)
                                        {
                                            macCapa |= deCONZ::MacDeviceIsFFD;
                                            if (neib.rxOnWhenIdle() == 2) // unknown
                                            {
                                                macCapa |= deCONZ::MacReceiverOnWhenIdle; // assumption
                                            }
                                        }
                                        else if (neib.deviceType() == deCONZ::EndDevice)
                                        {
                                            macCapa |= deCONZ::MacAllocateAddress; // assume
                                        }

                                        DBG_Printf(DBG_ZDP, "    * seems to have invalid mac capabilities: " FMT_MAC ", 0x%02X\n", FMT_MAC_CAST(neib.address().ext()), (int)neibNode->data->macCapabilities());
                                        neibNode->data->setMacCapabilities(macCapa);
                                    }

                                    if (!neibNode->data->nodeDescriptor().isNull() &&
                                        neibNode->data->macCapabilities().testFlag(deCONZ::MacReceiverOnWhenIdle) &&
                                         neibNode->data->nodeDescriptor().receiverOnWhenIdle() != neibNode->data->macCapabilities().testFlag(deCONZ::MacReceiverOnWhenIdle))
                                    {
                                        DBG_Printf(DBG_ZDP, "    * may have invalid node descriptor: " FMT_MAC ", rxOnWhenIdle\n", FMT_MAC_CAST(neib.address().ext()));
                                        // TODO Device state machine should handle this
                                        //neibNode->data->setNodeDescriptor({});
                                        //neibNode->data->setFetched(deCONZ::ReqNodeDescriptor, false);
                                    }

                                    if (neib.relationship() == deCONZ::ChildRelation &&
                                            neibNode->data->isEndDevice())
                                    {
                                        neibNode->data->touch(m_steadyTimeRef);
                                    }
                                    else if (neib.deviceType() == deCONZ::Router)
                                    {
                                        addDeviceDiscover(addrPair);
                                    }

                                    if (neibNode->data->address().nwk() != neib.address().nwk())
                                    {
                                        DBG_Printf(DBG_INFO_L2, "    * different nwk address 0x%04X / 0x%04X\n",
                                                   neibNode->data->address().nwk(), neib.address().nwk());
                                    }
                                }
                                else
                                {
                                    // put non existing nodes and zombies in discover queue
                                    addDeviceDiscover(addrPair);
                                }
                            }
                        }

                        if ((neib.relationship() == deCONZ::ParentRelation) ||
                            (neib.relationship() == deCONZ::ChildRelation) ||
                            (neib.relationship() == deCONZ::SiblingRelation) ||
                            (neib.relationship() == deCONZ::UnknownRelation))
                        {
                            if (neib.extPanId() == myPan)
                            {
                                if (neib.relationship() == deCONZ::ChildRelation)
                                {
                                    NodeInfo *child = getNode(neib.address(), deCONZ::NwkAddress);
                                    if (child)
                                    {
                                        if (child->data->parentAddress() != node->data->address())
                                        {
                                            // TODO update ui, remove old links
                                            child->data->parentAddress() = node->data->address();
                                        }
                                    }
                                    else
                                    {
                                        DBG_Printf(DBG_INFO_L2, "neighbor " FMT_MAC " is unknown child\n", FMT_MAC_CAST(neib.address().ext()));
                                    }
                                }
                                else if (neib.relationship() == deCONZ::ParentRelation)
                                {
                                    if (node->data->parentAddress() != neib.address())
                                    {
                                        // TODO update ui, remove old links
                                        node->data->parentAddress() = neib.address();
                                    }
                                }

                                if (neib.address().hasExt() && (neib.address().ext() != 0))
                                {
                                    if (!gHeadlessVersion) // no links here
                                    {
                                        m_createLinkQueue.push_back(addrPair);
                                        //linkCreateTick(); // fast creation
                                    }
                                }
                            }
                        }

                        p += NeighEntrySize;
                    }
                }

                NodeEvent event(NodeEvent::UpdatedClusterData, node->data, ind);
                emit nodeEvent(event);
            }
            else
            {
                if (node && node->data)
                {
                    node->data->setMgmtLqiStartIndex(0x00);
                    node->data->setFetched(deCONZ::ReqMgmtLqi, true);
                }
            }

        }
            break;

        case ZDP_MATCH_DESCRIPTOR_CLID:
        case ZDP_MATCH_DESCRIPTOR_RSP_CLID:
        {

        }
            break;

        case ZDP_MGMT_BIND_RSP_CLID:
        {
            addr = ind.srcAddress();
            node = getNode(addr, deCONZ::NoAddress);

            if (node && (ind.asdu().size() > 4) && status == deCONZ::ZdpSuccess)
            {
                auto &bindingTable = node->data->bindingTable();
                uint8_t entries;
                uint8_t startIndex;
                uint8_t listCount;

                stream >> entries;
                stream >> startIndex;
                stream >> listCount;

                if (startIndex == 0)
                {
                    bindingTable.setResponseIndex0TimeRef(m_steadyTimeRef);
                }

                for (uint i = 0; i < listCount && !stream.atEnd(); i++)
                {
                    deCONZ::Binding bnd;

                    if (!bnd.readFromStream(stream))
                    {
                        break;
                    }

                    bindingTable.add(bnd);

                    for (auto &bnd0 : bindingTable)
                    {
                        if (bnd0 == bnd)
                        {
                            // refresh timestamp
                            bnd0.setConfirmedTimeRef(m_steadyTimeRef);
                            break;
                        }
                    }
                }

                if (entries > 0)
                {
                    const auto bi = std::find(m_bindLinkQueue.cbegin(), m_bindLinkQueue.cend(), node->data->address());
                    if (bi == m_bindLinkQueue.cend())
                    {
                        m_bindLinkQueue.push_back(node->data->address());
                    }
                }

                if (startIndex + listCount >= entries)
                {
                    bindingTable.clearOldBindings();
                }

                node->data->setFetched(deCONZ::ReqMgmtBind, true);
            }
        }
            break;

        case ZDP_MGMT_PERMIT_JOINING_RSP_CLID:
        {
        }
            break;

        case ZDP_MGMT_LEAVE_RSP_CLID:
        {
            DBG_Printf(DBG_ZDP, "ZDP Mgmt_Leave_rsp zdpSeq: %u status 0x%02X from %s\n", seqNum, status, srcAddrStr);
        }
            break;

        case ZDP_MGMT_NWK_UPDATE_RSP_CLID:
        {
            if (status == deCONZ::ZdpSuccess && node && node->data)
            {
                uint32_t scanChannels;
                uint16_t totalTransmissions;
                uint16_t failedTransmissions;
                uint8_t scanChannelsListCount;

                stream >> scanChannels;
                stream >> totalTransmissions;
                stream >> failedTransmissions;
                stream >> scanChannelsListCount;

                /*
                   The ED result shall be reported to the MLME using PLME-ED.confirm (see 6.2.2.4) as an 8 bit integer
                   ranging from 0x00 to 0xff. The minimum ED value (zero) shall indicate received power less than 10 dB
                   above the specified receiver sensitivity (see 6.5.3.3 and 6.6.3.4), and the range of received power spanned by
                   the ED values shall be at least 40 dB. Within this range, the mapping from the received power in decibels to
                   ED value shall be linear with an accuracy of ± 6 dB.
                */

                DBG_Printf(DBG_ZDP, "ZDP Mgmt_NWK_update_notify from %s, scan channels 0x%04X\n", srcAddrStr, scanChannels);

                for (unsigned i = 0; i < scanChannelsListCount; i++)
                {
                    if (stream.status() != QDataStream::Ok)
                        break;

                    int8_t ed;
                    stream >> ed;

                    if (ed > 0)
                    {
                        ed = -ed;
                    }

                    if (ed < -5 && scanChannelsListCount == 1)
                    {
                        node->data->pushEdScan(ed);

                        DBG_Printf(DBG_ZDP, "  ED value: %d (0x%02X)\n", ed, uint8_t(ed & 0xFF));
                    }
                }               
            }
        }
            break;

        case ZDP_USER_DESCRIPTOR_RSP_CLID:
        {
            stream >> nwk;
            addr.setNwk(nwk);
            node = getNode(addr, deCONZ::NwkAddress);

            if (status == deCONZ::ZdpSuccess)
            {
                if (node)
                {
                    uint8_t len;
                    stream >> len;

                    if (len < 17 && (ind.asdu().size() >= (5 + len)))
                    {
                        char buf[17];
                        for (uint i = 0; i < len; i++)
                        {
                            buf[i] = ind.asdu()[i + 5];
                        }
                        buf[len] = '\0';

                        node->data->setUserDescriptor(buf);
                        node->data->setFetched(deCONZ::ReqUserDescriptor, true);
                        node->g->requestUpdate(); // redraw
                        NodeEvent event(NodeEvent::UpdatedUserDescriptor, node->data);
                        emit nodeEvent(event);
                    }
                }
            }
            else
            {
                if (node)
                {
                    node->data->retryIncr(deCONZ::ReqUserDescriptor);
                }
            }

        }
            break;

        case ZDP_USER_DESCRIPTOR_CONF_CLID:
        {
            stream >> nwk;
            addr.setNwk(nwk);
            node = getNode(addr, deCONZ::NwkAddress);

            // refetch
            if (node)
            {
                if (status == deCONZ::ZdpSuccess)
                {
                    node->data->setFetched(deCONZ::ReqUserDescriptor, false);
                    node->g->updated(deCONZ::ReqUserDescriptor);
                }
            }
        }
            break;

        case ZDP_BIND_RSP_CLID:
        case ZDP_UNBIND_RSP_CLID:
        {
            deCONZ::bindDropBox()->bindIndCallback(ind);
//            if (apsReq.state() == deCONZ::FinishState)
            {
                // this is the response to our request
                // unpack the data again
                /*
                Address addr;
                deCONZ::BindReq bindReq;
                quint64 u64;

                QDataStream stream(apsReq.asdu());
                stream.setByteOrder(QDataStream::LittleEndian);
                bindReq.rspState = (deCONZ::ZdpState)status;
                stream >> bindReq.dstAddrMode; // dummy discard seqNum
                stream >> u64;
                bindReq.srcAddr = u64;
                stream >> bindReq.srcEndpoint;
                stream >> bindReq.clusterId;
                stream >> bindReq.dstAddrMode;

                if (bindReq.dstAddrMode == deCONZ::ApsExtAddress)
                {
                    stream >> u64;
                    bindReq.dstExtAddr = u64;
                    stream >> bindReq.dstEndpoint;
                }
                else if (bindReq.dstAddrMode == deCONZ::ApsGroupAddress)
                {
                    stream >> bindReq.dstGroupAddr;
                }

                if (stream.status() == QDataStream::ReadPastEnd)
                {
                    DBG_Printf(DBG_ZDP, "invalid bind/unbind response\n");
                    return;
                }

                addr.setExt(bindReq.srcAddr);
                node = getNode(addr, deCONZ::ExtAddress);

                if (node && node->data && (status == deCONZ::ZdpSuccess || status == deCONZ::ZdpNoEntry))
                {
                    Binding binding;
                    binding.srcAddress().setExt(bindReq.srcAddr);
                    binding.setSrcEndpoint(bindReq.srcEndpoint);
                    binding.setCluster(bindReq.clusterId);
                    binding.setDstAddressMode((deCONZ::ApsAddressMode)bindReq.dstAddrMode);

                    if (bindReq.dstAddrMode == deCONZ::ApsExtAddress)
                    {
                        binding.dstAddress().setExt(bindReq.dstExtAddr);
                        binding.setDstEndpoint(bindReq.dstEndpoint);
                    }
                    else if (bindReq.dstAddrMode == deCONZ::ApsGroupAddress)
                    {
                        binding.dstAddress().setGroup(bindReq.dstGroupAddr);
                    }

                    if (ind.clusterId() == ZDP_UNBIND_RSP_CLID)
                    {
                        node->data->bindingTable().removeBinding(binding);
                        removeBindingLink(binding);
                    }
                    else if (status == deCONZ::ZdpSuccess)
                    {
                        node->data->bindingTable().addBinding(binding);
                    }

                    m_bindLinkQueue.append(binding.srcAddress());

                    deCONZ::bindDropBox()->bindCallback(bindReq);
                    NodeEvent event(NodeEvent::UpdatedClusterData, node->data, ind);
                    emit nodeEvent(event);
                }
                */
            }
        }
            break;

        default:
            if (!m_nodes.empty() && node && node->data &&
                 node->data == m_nodes[0].data && node->data->address().nwk() == 0x0000) // ignore loopback command
            {
                return;
            }
            DBG_Printf(DBG_ZDP, "ZDP got response for unknown cluster 0x%04X\n", ind.clusterId());
            break;
        }

        // check ZDP status
        switch (status)
        {
        case deCONZ::ZdpSuccess:
            indication = deCONZ::IndicateDataUpdate;
            break;

        case deCONZ::ZdpNoDescriptor:
            indication = deCONZ::IndicateError;
            break;

        case deCONZ::ZdpDeviceNotFound:
            indication = deCONZ::IndicateError;
            break;

        case deCONZ::ZdpInvalidRequestType:
            indication = deCONZ::IndicateError;
            break;

        default:
        {
            indication = deCONZ::IndicateError;
            DBG_Printf(DBG_ZDP, "ZDP error status 0x%02X\n", status);
        }
            break;
        }

        if (!node)
        {
            node = getNode(ind.srcAddress(), deCONZ::NoAddress);
        }
    } // end ZDP profile
    else
    {
        if (!node)
        {
            node = getNode(ind.srcAddress(), deCONZ::NoAddress);
        }

        if (!node || !node->data)
        {
            return;
        }

        deCONZ::ZclFrame &zclFrame = m_zclFrame;

        zclFrame.reset();
        zclFrame.readFromStream(stream);

        if (!zclFrame.isValid())
        {
            return;
        }

        deCONZ::ZclCluster *cl = nullptr;
        SimpleDescriptor *sd = node->data->getSimpleDescriptor(ind.srcEndpoint());
        if (sd)
        {
            bool ok = false;

            if (zclFrame.frameControl() & deCONZ::ZclFCDirectionServerToClient)
            {
                cl = sd->cluster(ind.clusterId(), deCONZ::ServerCluster);
            }
            else
            {
                cl = sd->cluster(ind.clusterId(), deCONZ::ClientCluster);
            }

            if (!cl)
            {
                cl = addMissingCluster(node, sd, ind, zclFrame);
            }

            if (cl && zclFrame.isClusterCommand())
            {
                ok = cl->readCommand(zclFrame);
            }

            if (ok)
            {
                deCONZ::clusterInfo()->refreshNodeCommands(node->data, cl);
            }
        }

        if (zclFrame.isProfileWideCommand())
        {
            switch (zclFrame.commandId())
            {
            case deCONZ::ZclReadAttributesId:
                break;

            case deCONZ::ZclReadAttributesResponseId:
            {
                NodeEvent event(NodeEvent::UpdatedClusterDataZclRead, node->data, ind);
                zclReadAttributesResponse(node, ind, zclFrame, event);
                deCONZ::clusterInfo()->zclCommandResponse(ind, zclFrame);
                indication = deCONZ::IndicateDataUpdate;
                emit nodeEvent(event);
            }
            break;

            case deCONZ::ZclReportAttributesId:
            {
                if (node)
                {
                    NodeEvent event(NodeEvent::UpdatedClusterDataZclReport, node->data, ind);
                    zclReportAttributesIndication(node, ind, zclFrame, event);
                    deCONZ::clusterInfo()->zclCommandResponse(ind, zclFrame);
                    indication = deCONZ::IndicateDataUpdate;
                    emit nodeEvent(event);
                }
            }
            break;

            case deCONZ::ZclDiscoverAttributesResponseId:
            {
                if (node)
                {
                    zclDiscoverAttributesResponse(node, ind, zclFrame);
                    deCONZ::clusterInfo()->zclCommandResponse(ind, zclFrame);
                    indication = deCONZ::IndicateDataUpdate;

                    //NodeEvent event(NodeEvent::UpdatedClusterDataZclReport, node->data, ind);
                    //emit nodeEvent(event);
                }
            }
            break;

            case deCONZ::ZclReadReportingConfigResponseId:
            case deCONZ::ZclWriteAttributesResponseId:
            case deCONZ::ZclConfigureReportingResponseId:
            case deCONZ::ZclDefaultResponseId:
            {
                if (zclFrame.commandId() == deCONZ::ZclReadReportingConfigResponseId)
                {
                    if (zclReadReportConfigurationResponse(node, ind, zclFrame))
                    {
                    }
                }

                deCONZ::clusterInfo()->zclCommandResponse(ind, zclFrame);
                indication = deCONZ::IndicateDataUpdate;
            }
            break;

            default:
                if (DBG_IsEnabled(DBG_INFO_L2))
                {
                    DBG_Printf(DBG_ZCL, "ZCL unknown response, cluster: 0x%04X command: 0x%02X\n", ind.clusterId(), zclFrame.commandId());
                }
                break;
            }

        }
        else // cluster command
        {
            if (!sd || sd->deviceId() == 0xffff)
            {
                if (getParameter(deCONZ::ParamPermitJoin) == 0)
                {
                    if (sendSimpleDescriptorRequest(node, ind.srcEndpoint()))
                    {

                    }
                }
            }

            deCONZ::clusterInfo()->zclCommandResponse(ind, zclFrame);
            indication = deCONZ::IndicateDataUpdate;
        }
    }

    visualizeNodeIndication(node, indication);
    emit apsdeDataIndication(ind);

    if (!sendNextApsdeDataRequest(node))
    {
        sendNextLater();
    }
}

const ApsDataRequest *zmController::getApsRequest(uint id) const
{
    auto i = std::find_if(m_apsRequestQueue.cbegin(), m_apsRequestQueue.cend(), [id](const auto &req){ return req.id() == id; });

    if (i != m_apsRequestQueue.cend())
    {
        return &*i;
    }

    return nullptr;
}

/*! Update various node attributes. */
void zmController::onRestNodeUpdated(quint64 extAddress, const QString &item, const QString &value)
{
    deCONZ::Address addr;
    addr.setExt(extAddress);
    NodeInfo *node = getNode(addr, deCONZ::ExtAddress);
    if (!node || !node->data || !node->g)
    {
        return;
    }

    bool needRedraw = node->data->needRedraw();

    if (item == QLatin1String("name"))
    {
        if (node->data->userDescriptor() != value)
        {
            node->data->setUserDescriptor(value);
            node->g->setName(value);
            needRedraw = true;
        }
    }
    else if (item == QLatin1String("version"))
    {
        if (node->data->swVersion() != value)
        {
            node->data->setVersion(value);
        }
    }
    else if (item == QLatin1String("modelid"))
    {
        if (node->data->modelId() != value)
        {
            node->data->setModelId(value);
        }
    }
    else if (item == QLatin1String("hasddf"))
    {
        int v = value.toInt();
        U_ASSERT(v >= 0 && v <= 2);

        node->data->setHasDDF(v);
        if (node->g)
        {
            node->g->setHasDDF(node->data->hasDDF());
        }
    }
    else if (item == QLatin1String("vendor"))
    {
        if (node->data->vendor() != value)
        {
            node->data->setVendor(value);
        }
    }
    else if (item == QLatin1String("deleted"))
    {
        deleteNode(node, NodeRemoveFinally);
        return;
    }   
    else if (item == QLatin1String("config/battery") || item == QLatin1String("state/battery"))
    {
        bool ok;
        const int bat = value.toInt(&ok);

        if (ok && bat >= 0 && bat <= 100 && node->data->battery() != bat)
        {
            node->data->setBattery(bat);
            node->g->setBattery(bat);
            needRedraw = true;
        }
    }

    if (needRedraw)
    {
        node->data->setNeedRedraw(false);
        node->g->updateParameters(node->data); // TODO remove, gnode shouldn't know about data node
        node->g->requestUpdate();
        deCONZ::nodeModel()->updateNode(*node);
    }
}

void zmController::checkDeviceAnnce(const Address &address, deCONZ::MacCapabilities macCapabilities)
{
    checkAddressChange(address);

    // not found new node
    AddressPair addrPair;
    addrPair.bAddr = address;
    addrPair.bMacCapabilities = macCapabilities;
    addDeviceDiscover(addrPair);

    const uint8_t permitJoin = getParameter(ParamPermitJoin);

    if (macCapabilities & deCONZ::MacDeviceIsFFD)
    {
        // fasten neighbor discovery
        m_lastNodeAdded = m_steadyTimeRef;
        if (permitJoin > 0)
        {
            m_fetchLqiTickMsCounter.restart(); // prevent lqi requests
        }
    }
    else if (permitJoin > 0)
    {
        m_lastEndDeviceAnnounce = m_steadyTimeRef;
    }
}

NodeInfo zmController::createNode(const Address &addr, deCONZ::MacCapabilities macCapabilities)
{
    NodeInfo info;

    { // check existing
        auto i = m_nodes.begin();
        auto end = m_nodes.end();
        for (; i != end; ++i)
        {
            NodeInfo &n = *i;
            if (n.data && n.data->address().hasExt() && addr.hasExt()
                    && n.data->address().ext() == addr.ext())
            {
                return n;
            }

            if (n.data && n.data->address().hasNwk() && addr.hasNwk()
                    && n.data->address().nwk() == addr.nwk())
            {
                return n;
            }
        }
    }

    {
        const zmNet &net = _netModel->currentNetwork();

        if (addr.hasExt() && net.ownAddress().ext() == addr.ext())
        { } // allow creation of own node
        else if (getParameter(deCONZ::ParamPermitJoin) == 0 && !DB_ExistsRestDevice(addr.ext()))
        {
            DBG_Printf(DBG_INFO_L2, "CTRL skip creating node " FMT_MAC " while permit join is disabled\n", FMT_MAC_CAST(addr.ext()));
            return info;
        }
    }

    if (m_lastNodeDeleted.isValid())
    {
        if (!m_lastNodeDeleted.hasExpired(10000))
        {
            return info;
        }
        m_lastNodeDeleted.invalidate();
    }

    info.data = new deCONZ::zmNode(macCapabilities);
    info.g = new zmgNode(info.data, nullptr);

    connect(info.g, &zmgNode::contextMenuRequest, this, &zmController::onNodeContextMenuRequest);
    connect(info.g, SIGNAL(moved()), this, SLOT(queueSaveNodesState()));

    info.id = m_nodes.size() + 1;

    QPointF p;
    // TODO improve initial node position
    const auto r0 = m_steadyTimeRef.ref;
    int r = (r0 % 201);
    p.setX((r & 1) ? r : -r);
    r = (r0 % 140);
    p.setY((r & 1) ? r : -r);
    info.g->setPos(p);
    info.g->setNeedSaveToDatabase(true);
    queueSaveNodesState();
    m_saveNodesTimer->start(1000 * 10);

    info.data->setAddress(addr);

    // XBees don't provide a user descriptor, at least we could show a human readable "XBee"
    if ((info.data->address().ext() & 0x0013a20000000000LLU) == 0x0013a20000000000LLU)
    {
        info.data->setUserDescriptor("XBee");
    }

    info.g->updated(deCONZ::ReqSimpleDescriptor);
    m_nodes.push_back(info);
    if (!info.g->scene())
    {
        m_scene->addItem(info.g);
    }
    info.g->show();
    info.g->updateParameters(info.data);
    info.g->requestUpdate();

    if (addr.hasExt() && (addr.ext() != 0))
    {
        info.data->setFetched(deCONZ::ReqIeeeAddr, true);
    }

    if (addr.hasNwk())
    {
        info.data->setFetched(deCONZ::ReqNwkAddr, true);
    }

    DBG_Printf(DBG_INFO, "CTRL create node %s, nwk: 0x%04X\n", info.data->extAddressString().c_str(), info.data->address().nwk());

    info.id = m_nodes.size();
    deCONZ::nodeModel()->addNode(info);
    NodeEvent event(NodeEvent::NodeAdded, info.data);
    emit nodeEvent(event);

    if (!info.data->nodeDescriptor().isNull())
    {
        NodeEvent event(NodeEvent::UpdatedNodeDescriptor, info.data, ZDO_ENDPOINT, ZDP_PROFILE_ID, ZDP_NODE_DESCRIPTOR_CLID);
        emit nodeEvent(event);
    }

    if (info.data->powerDescriptor().isValid())
    {
        NodeEvent event(NodeEvent::UpdatedPowerDescriptor, info.data, ZDO_ENDPOINT, ZDP_PROFILE_ID, ZDP_POWER_DESCRIPTOR_CLID);
        emit nodeEvent(event);
    }

    if (!info.data->simpleDescriptors().empty())
    {
        for (const auto &sd : info.data->simpleDescriptors())
        {
            NodeEvent event(NodeEvent::UpdatedSimpleDescriptor, info.data, sd.endpoint());
            emit nodeEvent(event);
        }
    }

    if (m_nodes.size() == 1)
    {
        m_graph->ensureVisible(info.g, 250, 250);
    }

    return m_nodes.back();
}

/*!
      Delete or hide a node (set inactiv)

      \param node - the node to delete
      \param finally - delete finally or just hide
  */
void zmController::deleteNode(NodeInfo *node, NodeRemoveMode finally)
{
    if (!node || !node->data)
    {
        return;
    }

    NodeInfo cpy = *node;

    // clear all requests addressed to this node
    clearAllApsRequestsToNode(cpy);

    std::vector<NodeInfo>::iterator i = m_nodes.begin();
    std::vector<NodeInfo>::iterator end = m_nodes.end();
    for (; i != end; ++i)
    {
        // self
        if ((i->data == cpy.data) && (i->g == cpy.g))
        {
            if (i->data == m_nodes[0].data)
            {
                continue; // don't delete our self
            }

            cpy.data->setZombieInternal(true);

            if (cpy.g)
            {
                cpy.g->requestUpdate();
            }

            if (finally == NodeRemoveFinally)
            {
                deCONZ::nodeModel()->removeNode(cpy);
                // remove all binding links for this node
                // one per iteration
                bool found = true;

                while (found)
                {
                    found = false;

                    QList<BindLinkInfo>::iterator ib = m_bindings.begin();
                    QList<BindLinkInfo>::iterator endb = m_bindings.end();

                    for (; ib != endb; ++ib)
                    {
                        if (ib->binding.srcAddress() == cpy.data->address().ext())
                        {
                            found = true;
                        }
                        else if (ib->binding.dstAddress().ext() == cpy.data->address().ext())
                        {
                            found = true;
                        }

                        if (found)
                        {
                            removeBindingLink(ib->binding);
                            break;
                        }
                    }
                }

                if (cpy.g)
                {
                    cpy.g->hide();
                }

                deleteSourcesRouteWith(node->data->address());

                NodeEvent event(NodeEvent::NodeRemoved, cpy.data);
                emit nodeEvent(event);
                m_nodesDead.push_back(*i);
                m_nodes.erase(i);

                // prevent node being added again before removed from database
                m_lastNodeDeleted.start();
                m_deviceDiscoverQueue.clear();
            }
            else
            {
                deCONZ::nodeModel()->updateNode(cpy);

                if (finally == NodeRemoveHide)
                {
                    if (cpy.g)
                    {
                        DBG_Printf(DBG_INFO, "hide node: 0x%04X\n", cpy.data->address().nwk());
                        cpy.g->hide();
                    }
                }
            }

            QList<LinkInfo>::iterator il = m_neighbors.begin();
            QList<LinkInfo>::iterator endl = m_neighbors.end();

            for (; il != endl; ++il)
            {
                if ((il->a == cpy.g) || (il->b == cpy.g))
                {

                    if (il->link)
                    {
                        il->link->hide();
                        if (il->a)
                        {
                            il->a->remLink(il->link);
                        }
                        if (il->b)
                        {
                            il->b->remLink(il->link);
                        }
                        m_neighborsDead.append(*il);
//                        i = m_neighbors.erase(il);
                        il->linkAgeUnix = {};
                        il->link = 0;
                        il->a = 0;
                        il->b = 0;
                    }
                }
            }

            break;
        }
        else
        {
            i->data->removeNeighbor(cpy.data->address());
        }
    }
}


bool zmController::sendNwkAddrRequest(NodeInfo *node)
{
    if (!node || !node->data)
    {
        return false;
    }

    deCONZ::ApsDataRequest req;

    req.setDstEndpoint(ZDO_ENDPOINT);
    req.setSrcEndpoint(ZDO_ENDPOINT);
    req.setDstAddressMode(deCONZ::ApsNwkAddress);
    req.dstAddress().setExt(node->data->address().ext()); // reference
    req.dstAddress().setNwk(deCONZ::BroadcastRxOnWhenIdle);
    req.setProfileId(ZDP_PROFILE_ID);
    req.setClusterId(ZDP_NWK_ADDR_CLID);
    req.setRadius(0);

    QDataStream stream(&req.asdu(), QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << genSequenceNumber(); // seq no.
    stream << (quint64)node->data->address().ext(); // device address

    //uint8_t requestType = 0x01; // extended request
    uint8_t requestType = 0x00; // single device request
    uint8_t startIndex = 0x00; // only extended request

    stream << requestType;
    stream << startIndex;

    if (apsdeDataRequest(req) == deCONZ::Success)
    {
//        DBG_Printf(DBG_ZDP, "send NWK_addr_req to %s, last seen %" PRId64 " s, last seen by neighbors %" PRId64 " s\n",
//                   node->data->extAddressString().c_str(), (int64_t)(node->data->lastSeenElapsed() / 1000), (node->data->lastSeenByNeighbor() / 1000));
        return true;
    }
    else
    {
        DBG_Printf(DBG_ZDP, "failed to send NWK_Addr_req to %s\n", node->data->extAddressString().c_str());
    }

    return false;
}

bool ZDP_SendIeeeAddrRequest(zmController *apsCtrl, const deCONZ::Address &dst)
{
    if (!apsCtrl || !dst.hasNwk())
    {
        return false;
    }

    // unicast to the node, if the node answers it will be marked as non-zombie
    // in the APS-DATA.indication
    ApsDataRequest req;
    QDataStream stream(&req.asdu(), QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    req.dstAddress().setExt(dst.ext());
    req.dstAddress().setNwk(dst.nwk());
    req.setDstAddressMode(deCONZ::ApsNwkAddress);

    req.setDstEndpoint(ZDO_ENDPOINT);
    req.setSrcEndpoint(ZDO_ENDPOINT);
    req.setProfileId(ZDP_PROFILE_ID);
    if (deCONZ::netEdit()->apsAcksEnabled())
    {
        req.setTxOptions(deCONZ::ApsTxAcknowledgedTransmission);
    }
    req.setRadius(0);
    req.setClusterId(ZDP_IEEE_ADDR_CLID);
    stream << apsCtrl->genSequenceNumber();
    stream << dst.nwk();

    stream << (uint8_t)0x00; // single request type
    stream << (uint8_t)0x00; // ignore start index

    if (apsCtrl->apsdeDataRequest(req) == deCONZ::Success)
    {
        return true;
    }
    return false;
}

bool zmController::sendIeeeAddrRequest(NodeInfo *node)
{
    if (!node || !node->data)
    {
        return false;
    }

    // unicast to the node, if the node answers it will be marked as non-zombie
    // in the APS-DATA.indication
    ApsDataRequest req;
    QDataStream stream(&req.asdu(), QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    req.dstAddress().setNwk(node->data->address().nwk());
    req.setDstAddressMode(deCONZ::ApsNwkAddress);

    req.setDstEndpoint(ZDO_ENDPOINT);
    req.setSrcEndpoint(ZDO_ENDPOINT);
    req.setProfileId(ZDP_PROFILE_ID);
//    req.setTxOptions(deCONZ::ApsTxAcknowledgedTransmission);
    req.setRadius(0);
    req.setClusterId(ZDP_IEEE_ADDR_CLID);
    stream << genSequenceNumber();
    stream << node->data->address().nwk();

    stream << (uint8_t)0x00; // single request type
    stream << (uint8_t)0x00; // ignore start index

    if (apsdeDataRequest(req) == deCONZ::Success)
    {
        return true;
    }

    return false;
}

bool zmController::sendMgtmLqiRequest(NodeInfo *node)
{
    if (!node || !node->data)
    {
        return false;
    }

    ApsDataRequest req;
    QDataStream stream(&req.asdu(), QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    if (node->data->recvErrors() > 0 || deCONZ::netEdit()->apsAcksEnabled())
    {
        req.setTxOptions(deCONZ::ApsTxAcknowledgedTransmission);
    }

    // ZDP Header
    req.dstAddress() = node->data->address();
    req.setDstAddressMode(deCONZ::ApsExtAddress);
    req.setDstEndpoint(ZDO_ENDPOINT);
    req.setSrcEndpoint(ZDO_ENDPOINT);
    req.setProfileId(ZDP_PROFILE_ID);
    req.setRadius(0);
    req.setClusterId(ZDP_MGMT_LQI_REQ_CLID);

    stream << genSequenceNumber();
    stream << node->data->mgmtLqiStartIndex();

    DBG_Printf(DBG_ZDP, "Mgmt_Lqi_req zdpSeq: %u to %s start index %u\n", (uint8_t)req.asdu()[0], node->data->extAddressString().c_str(), node->data->mgmtLqiStartIndex());

    if (apsdeDataRequest(req) == deCONZ::Success)
    {
        m_fetchLqiTickMsCounter.restart();
        return true;
    }

    return false;
}

bool zmController::sendMgtmRtgRequest(NodeInfo *node, uint8_t startIndex)
{
    if (!node || !node->data)
    {
        return false;
    }

    ApsDataRequest req;
    QDataStream stream(&req.asdu(), QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    // ZDP Header
    req.dstAddress() = node->data->address();
    req.setDstAddressMode(deCONZ::ApsNwkAddress);
    req.setDstEndpoint(ZDO_ENDPOINT);
    req.setSrcEndpoint(ZDO_ENDPOINT);
    req.setProfileId(ZDP_PROFILE_ID);
    req.setRadius(0);
    req.setClusterId(ZDP_MGMT_RTG_REQ_CLID);

    stream << genSequenceNumber();
    stream << startIndex;

    DBG_Printf(DBG_ZDP, "Mgmt_Rtg_req zdpSeq: %u to %s start index %u\n", unsigned(req.asdu()[0]), node->data->extAddressString().c_str(), startIndex);

    if (apsdeDataRequest(req) == deCONZ::Success)
    {
        return true;
    }

    return false;
}

bool zmController::sendNodeDescriptorRequest(NodeInfo *node)
{
    if (!node || !node->data)
    {
        return false;
    }

    ApsDataRequest req;
    QDataStream stream(&req.asdu(), QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    if (deCONZ::netEdit()->apsAcksEnabled())
    {
        req.setTxOptions(deCONZ::ApsTxAcknowledgedTransmission);
    }

    // ZDP Header
    req.dstAddress() = node->data->address();
    req.setDstAddressMode(deCONZ::ApsNwkAddress);
    req.setDstEndpoint(ZDO_ENDPOINT);
    req.setSrcEndpoint(ZDO_ENDPOINT);
    req.setProfileId(ZDP_PROFILE_ID);
    req.setRadius(0);
    req.setClusterId(ZDP_NODE_DESCRIPTOR_CLID);

    stream << genSequenceNumber();
    stream << node->data->address().nwk();

    DBG_Printf(DBG_ZDP, "Node_Descriptor_req zdpSeq: %u to %s\n", unsigned(req.asdu()[0]), node->data->extAddressString().c_str());

    if (apsdeDataRequest(req) == deCONZ::Success)
    {
        return true;
    }

    return false;
}

bool zmController::sendPowerDescriptorRequest(NodeInfo *node)
{
    if (!node || !node->data)
    {
        return false;
    }

    ApsDataRequest req;
    QDataStream stream(&req.asdu(), QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    if (node->data->recvErrors() > 0)
    {
    }
    req.setTxOptions(deCONZ::ApsTxAcknowledgedTransmission);

    // ZDP Header
    req.dstAddress() = node->data->address();
    req.setDstAddressMode(deCONZ::ApsNwkAddress);
    req.setDstEndpoint(ZDO_ENDPOINT);
    req.setSrcEndpoint(ZDO_ENDPOINT);
    req.setProfileId(ZDP_PROFILE_ID);
    req.setRadius(0);
    req.setClusterId(ZDP_POWER_DESCRIPTOR_CLID);

    stream << genSequenceNumber();
    stream << node->data->address().nwk();

    if (apsdeDataRequest(req) == deCONZ::Success)
    {
        return true;
    }

    return false;
}

bool zmController::sendActiveEndpointsRequest(NodeInfo *node)
{
    if (!node || !node->data)
    {
        return false;
    }

    ApsDataRequest req;
    QDataStream stream(&req.asdu(), QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    if (node->data->recvErrors() > 0)
    {
        req.setTxOptions(deCONZ::ApsTxAcknowledgedTransmission);
    }

    // ZDP Header
    req.dstAddress() = node->data->address();
    req.setDstAddressMode(deCONZ::ApsNwkAddress);
    req.setDstEndpoint(ZDO_ENDPOINT);
    req.setSrcEndpoint(ZDO_ENDPOINT);
    req.setProfileId(ZDP_PROFILE_ID);
    req.setRadius(0);
    req.setClusterId(ZDP_ACTIVE_ENDPOINTS_CLID);

    stream << genSequenceNumber();
    stream << node->data->address().nwk();

    if (apsdeDataRequest(req) == deCONZ::Success)
    {
        return true;
    }

    return false;
}

bool zmController::sendUpdateNetworkRequest(NodeInfo *node)
{
    if (!node || !node->data)
    {
        return false;
    }

    const uint8_t nwkUpdateId = getParameter(deCONZ::ParamNetworkUpdateId);
    const uint8_t channel = getParameter(deCONZ::ParamCurrentChannel);
    const uint32_t scanChannels = (1 << static_cast<uint32_t>(channel));
    const uint8_t scanDuration = 0xfe; //special value = channel change

    deCONZ::ApsDataRequest req;

    req.setDstEndpoint(ZDO_ENDPOINT);
    req.setDstAddressMode(deCONZ::ApsNwkAddress);
    req.dstAddress().setNwk(deCONZ::BroadcastRxOnWhenIdle);
    //req.dstAddress().setNwk(node->data->address().nwk());
    req.setProfileId(ZDP_PROFILE_ID);
    req.setClusterId(ZDP_MGMT_NWK_UPDATE_REQ_CLID);
    req.setSrcEndpoint(ZDO_ENDPOINT);
    req.setRadius(0);

    QDataStream stream(&req.asdu(), QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << genSequenceNumber();
    stream << scanChannels;
    stream << scanDuration;
    stream << nwkUpdateId;

    DBG_Printf(DBG_ZDP, "Update_Network_req zdpSeq: %u to " FMT_MAC "\n", unsigned(req.asdu()[0]), FMT_MAC_CAST(node->data->address().ext()));

    if (apsdeDataRequest(req) == deCONZ::Success)
    {
        return true;
    }

    return false;
}

bool zmController::sendSimpleDescriptorRequest(NodeInfo *node, uint8_t endpoint)
{
    if (!node || !node->data)
    {
        return false;
    }

    ApsDataRequest req;
    QDataStream stream(&req.asdu(), QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    if (node->data->recvErrors() > 0)
    {
        req.setTxOptions(deCONZ::ApsTxAcknowledgedTransmission);
    }

    // ZDP Header
    req.dstAddress() = node->data->address();
    req.setDstAddressMode(deCONZ::ApsNwkAddress);
    req.setDstEndpoint(ZDO_ENDPOINT);
    req.setSrcEndpoint(ZDO_ENDPOINT);
    req.setProfileId(ZDP_PROFILE_ID);
    req.setRadius(0);
    req.setClusterId(ZDP_SIMPLE_DESCRIPTOR_CLID);

    stream << genSequenceNumber();
    stream << node->data->address().nwk();
    stream << endpoint;

    DBG_Printf(DBG_ZDP, "Simple_Descr_req zdpSeq: %u to " FMT_MAC " endpoint %u\n", unsigned(req.asdu()[0]), FMT_MAC_CAST(node->data->address().ext()), endpoint);

    if (apsdeDataRequest(req) == deCONZ::Success)
    {
        return true;
    }

    return false;
}

bool zmController::sendEdScanRequest(NodeInfo *node, uint32_t channels)
{
    if (!node || !node->data)
    {
        return false;
    }

    ApsDataRequest req;
    QDataStream stream(&req.asdu(), QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    if (node->data->recvErrors() > 0)
    {
        req.setTxOptions(deCONZ::ApsTxAcknowledgedTransmission);
    }

    // ZDP Header
    req.dstAddress() = node->data->address();
    req.setDstAddressMode(deCONZ::ApsNwkAddress);
    req.setDstEndpoint(ZDO_ENDPOINT);
    req.setSrcEndpoint(ZDO_ENDPOINT);
    req.setProfileId(ZDP_PROFILE_ID);
    req.setRadius(0);
    req.setClusterId(ZDP_MGMT_NWK_UPDATE_REQ_CLID);

    // U32 scan channels
    // U8 scan duration 0x00-0x05
    // U8 scan count 0x00-0x01

    //channels |= (1U << 11U) | (1U << 15U) | (1U << 20);

    uint8_t scanDuration = 5;
    uint8_t scanCount = 1;

    stream << genSequenceNumber();
    stream << channels;
    stream << scanDuration;
    stream << scanCount;

    DBG_Printf(DBG_ZDP, "Mgmt_NWK_Update_req (ED scan) zdpSeq: %u to %s\n", unsigned(req.asdu()[0]), node->data->extAddressString().c_str());

    if (apsdeDataRequest(req) == deCONZ::Success)
    {
        return true;
    }

    return false;
}

bool zmController::sendZclDiscoverAttributesRequest(NodeInfo *node, const deCONZ::SimpleDescriptor &sd, uint16_t clusterId, uint16_t startAttribute)
{
    if (!node || !node->data)
    {
        return false;
    }

    deCONZ::ZclFrame zclFrame;
    deCONZ::ApsDataRequest req;

    req.setDstEndpoint(sd.endpoint());
    req.setSrcEndpoint(1);
    req.setDstAddressMode(deCONZ::ApsNwkAddress);
    req.dstAddress().setNwk(node->data->address().nwk());
    if (sd.profileId() == ZLL_PROFILE_ID)
    {
        req.setProfileId(HA_PROFILE_ID);
    }
    else
    {
        req.setProfileId(sd.profileId());
    }
    req.setClusterId(clusterId);
    req.setRadius(0);

    zclFrame.setSequenceNumber(m_steadyTimeRef.ref & 0xFF);
    zclFrame.setCommandId(deCONZ::ZclDiscoverAttributesId);
    zclFrame.setFrameControl(deCONZ::ZclFCProfileCommand |
                             deCONZ::ZclFCDirectionClientToServer /*| deCONZ::ZclFCManufacturerSpecific*/);

    {
        QDataStream stream(&zclFrame.payload(), QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream << startAttribute;
        stream << (uint8_t)16; // max attributes
    }

    {
        QDataStream stream(&req.asdu(), QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        zclFrame.writeToStream(stream);
    }

    if (apsdeDataRequest(req) == deCONZ::Success)
    {
        return true;
    }

    return false;
}

void zmController::zclReadAttributesResponse(NodeInfo *node, const deCONZ::ApsDataIndication &ind, deCONZ::ZclFrame &zclFrame, NodeEvent &event)
{
    if (!node || !node->data)
    {
        return;
    }

    QDataStream stream(zclFrame.payload());
    stream.setByteOrder(QDataStream::LittleEndian);

    uint16_t id;
    uint8_t status;
    uint8_t dataType = deCONZ::ZclNoData;

    SimpleDescriptor *simpleDescr = nullptr;
    deCONZ::ZclCluster *cluster = nullptr;
    deCONZ::ZclClusterSide clusterSide = (zclFrame.frameControl() & deCONZ::ZclFCDirectionServerToClient)
                                          ? deCONZ::ServerCluster : deCONZ::ClientCluster;

    simpleDescr = node->data->getSimpleDescriptor(ind.srcEndpoint());
    if (simpleDescr)
    {
        cluster = simpleDescr->cluster(ind.clusterId(), clusterSide);
    }

    // when a response from a network node is received mark watchdog ok
    if (node->data != m_nodes[0].data)
    {
        m_deviceWatchdogOk |= DEVICE_RX_NETWORK_OK;
    }

    while (!stream.atEnd())
    {
        stream >> id;
        stream >> status;

        // search the attribute
        deCONZ::ZclAttribute *attr = nullptr;

        if (simpleDescr && !cluster && status == deCONZ::ZclSuccessStatus)
        {
            clusterSide = (clusterSide == deCONZ::ClientCluster)
                    ? deCONZ::ServerCluster : deCONZ::ClientCluster;
            // this is more a hack to get the cluster for wrong ZCL implementations
            cluster = simpleDescr->cluster(ind.clusterId(), clusterSide);

            // try to append unknown cluster
            if (!cluster && zclFrame.frameControl() & deCONZ::ZclFCDirectionServerToClient)
            {
                ZclDataBase *db = deCONZ::zclDataBase();
                if (!db)
                {
                    return;
                }

                const deCONZ::ZclCluster cl = db->inCluster(ind.profileId(), ind.clusterId(), node->data->nodeDescriptor().manufacturerCode());
                if (cl.isValid())
                {
                    simpleDescr->inClusters().push_back(cl);
                    cluster = node->data->getCluster(ind.srcEndpoint(), ind.clusterId(), deCONZ::ServerCluster);

                    if (node->g)
                    {
                        node->g->updated(deCONZ::ReqSimpleDescriptor);
                    }
                    NodeEvent event(NodeEvent::UpdatedSimpleDescriptor, node->data, simpleDescr->endpoint());
                    emit nodeEvent(event);
                    queueSaveNodesState();
                }
            }
        }

        if (cluster)
        {
            for (uint i = 0; i < cluster->attributes().size(); i++)
            {
                auto &a = cluster->attributes()[i];
                if (a.id() == id)
                {
                    if (a.isManufacturerSpecific() && a.manufacturerCode() != zclFrame.manufacturerCode())
                    {
                        continue;
                    }

                    attr = &a;
                    break;
                }
            }
        }

        if (!attr)
        {
            DBG_Printf(DBG_ZCL, "ZCL Read Attributes attribute 0x%04X unknown, abort\n", id);
            break;
        }

        if (status == deCONZ::ZclSuccessStatus)
        {
            attr->setAvailable(true);
            stream >> dataType;

            if (dataType != attr->dataType())
            {
                DBG_Printf(DBG_ZCL, "ZCL Read Attributes node=0x%04X, error assumed data type "
                       " 0x%02X but got 0x%02X for at=0x%04X\n",
                       node->data->address().nwk(),
                       attr->dataType(), dataType, attr->id());

                // disabled by stack
                if (dataType == deCONZ::ZclNoData)
                {
                    DBG_Printf(DBG_ZCL, "  --> disabled by stack, skip and disable\n");
                    attr->setAvailable(false);
                    continue;
                }
                else if (deCONZ::zclDataBase()->knownDataType(dataType))
                {
                    // convert
                    DBG_Printf(DBG_ZCL, "  --> update to new data type\n");
                    attr->setDataType(dataType);
                    attr->setAvailable(true);
                }
                else
                {
                    attr->setAvailable(false);
                    break;
                }
            }

            if (!attr->readFromStream(stream))
            {
                const deCONZ::ZclDataType &type = deCONZ::zclDataBase()->dataType(attr->dataType());

                if (!deCONZ::zclDataBase()->knownDataType(attr->dataType()))
                {
                    DBG_Printf(DBG_ZCL, "ZCL Read Attributes Datatype 0x%02X %s"
                           " not supported yet, abort\n",
                           type.id(), qPrintable(type.name()));
                    break;
                }

                DBG_Printf(DBG_ZCL, "ZCL Read Attributes Datatype 0x%02X %s"
                       " discard not supported data\n",
                       type.id(), qPrintable(type.name()));

                // no handler, discard
                uint8_t byte;
                for (int i = 0; i < type.length() && !stream.atEnd(); i++)
                {
                    stream >> byte;
                }
            }
            else if (cluster)
            {
                attr->setLastRead(m_steadyTimeRef.ref);

                if (node->data->updatedClusterAttribute(simpleDescr, cluster, attr))
                {
                    if (ind.clusterId() == 0x0000 && ind.profileId() == HA_PROFILE_ID)
                    {
                        deCONZ::nodeModel()->updateNode(*node);
                    }
                }

                event.addAttributeId(id);
            }
        }
        else if (status == deCONZ::ZclUnsupportedAttributeStatus)
        {
            //dataType = deCONZ::ZclNoData;
            DBG_Printf(DBG_ZCL, "ZCL got unsupported status: 0x%02X for mandatory attribute\n", status);
            attr->setAvailable(false); // disable fetching
        }

        DBG_Printf(DBG_ZCL, "ZCL got data for node=0x%04X, cl=0x%04X, at=0x%04X, status=0x%02X, type=0x%02X\n",
               node->data->address().nwk(),
               ind.clusterId(),
               attr->id(), status, dataType);
    }

    DBG_Assert(node && node->data && cluster);
    if (node && node->data && cluster)
    {
        deCONZ::clusterInfo()->refreshNodeAttributes(node->data, ind.srcEndpoint(), cluster);
    }
}

void zmController::zclDiscoverAttributesResponse(NodeInfo *node, const ApsDataIndication &ind, ZclFrame &zclFrame)
{
    if (!node || !node->data)
    {
        return;
    }

    deCONZ::ZclClusterSide side = (zclFrame.frameControl() & deCONZ::ZclFCDirectionServerToClient) ? deCONZ::ServerCluster : deCONZ::ClientCluster;
    deCONZ::ZclCluster *cluster = node->data->getCluster(ind.srcEndpoint(), ind.clusterId(), side);

    QDataStream stream(zclFrame.payload());
    stream.setByteOrder(QDataStream::LittleEndian);

    uint8_t complete;
    stream >> complete;

    DBG_Printf(DBG_INFO, "ZCL discover attributes response from %s (complete = %u)\n", node->data->extAddressString().c_str(), complete);

    while (!stream.atEnd())
    {
        uint16_t attrId;
        uint8_t dataType;

        stream >> attrId;
        stream >> dataType;

        if (stream.status() == QDataStream::ReadPastEnd)
        {
            break;
        }

        DBG_Printf(DBG_INFO, "\t attribute 0x%04X type 0x%02X\n", attrId, dataType);

        if (!cluster)
        {
            continue;
        }

        bool found = false;;
        for (deCONZ::ZclAttribute &attr : cluster->attributes())
        {
            if (attr.id() == attrId)
            {
                attr.setAvailable(true);
                found = true;
                break;
            }
        }

        if (!found)
        {
            //deCONZ::ZclAttribute attr(attrId, dataType, QLatin1String("Unknown"), deCONZ::ZclReadWrite, false);
            //cluster->attributes().push_back(attr);
        }
    }

    deCONZ::clusterInfo()->refresh();
}

bool zmController::zclReadReportConfigurationResponse(NodeInfo *node, const ApsDataIndication &ind, const deCONZ::ZclFrame &zclFrame)
{
    if (!node || !node->data)
    {
        return false;
    }

    deCONZ::ZclClusterSide side =
            // check  if this is always teh reverse case ?
            (zclFrame.frameControl() & deCONZ::ZclFCDirectionServerToClient) ? deCONZ::ServerCluster : deCONZ::ClientCluster;
    deCONZ::ZclCluster *cluster = node->data->getCluster(ind.srcEndpoint(), ind.clusterId(), side);

    deCONZ::SimpleDescriptor *sd = node->data->getSimpleDescriptor(ind.srcEndpoint());

    if (!cluster || !sd)
    {
        return false;
    }

    QDataStream stream(zclFrame.payload());
    stream.setByteOrder(QDataStream::LittleEndian);

    uint8_t status = deCONZ::ZclSuccessStatus;
    uint8_t direction;
    uint16_t attrId;
    uint8_t dataType;
    uint16_t minInterval;
    uint16_t maxInterval;

    int count = 0;

    while (!stream.atEnd() && stream.status() == QDataStream::Ok)
    {
        stream >> status;
        stream >> direction;
        stream >> attrId;

        if (status == deCONZ::ZclSuccessStatus)
        {
            auto attr = std::find_if(cluster->attributes().begin(), cluster->attributes().end(), [&](const deCONZ::ZclAttribute &i)
            {
                return i.id() == attrId;
            });

            if (attr == cluster->attributes().end())
                return false;

            if (direction == 0x00)
            {
                stream >> dataType;
                stream >> minInterval;
                stream >> maxInterval;

                if (stream.status() != QDataStream::Ok)
                {
                    return false;
                }

                deCONZ::ZclDataType type = deCONZ::zclDataBase()->dataType(dataType);

                if (type.isValid() && type.isAnalog())
                {
                    if (!attr->readReportableChangeFromStream(stream))
                    {
                        return false;
                    }
                }
                else
                {
                    deCONZ::NumericUnion val;
                    val.u64 = 0;
                    attr->setReportableChange(val);
                }

                attr->setMinReportInterval(minInterval);
                attr->setMaxReportInterval(maxInterval);

                count++;
            }
            else
            {
                return false; // TODO
            }
        }
    }

    if (count > 0)
    {
        deCONZ::clusterInfo()->refreshNodeAttributes(node->data, ind.srcEndpoint(), cluster);
    }

    return count > 0;
}

void zmController::queueSaveNodesState()
{
    if (m_saveNodesChanges < INT_MAX)
    {
        m_saveNodesChanges++;
    }
}

NodeInfo *zmController::getNode(const Address &addr, deCONZ::AddressMode mode)
{
    if (mode == deCONZ::ExtAddress || ((mode == deCONZ::NoAddress) && addr.hasExt()))
    {
        auto i = m_nodes.begin();
        auto end = m_nodes.end();

        for (; i != end; ++i)
        {
            if (i->data->address().hasExt())
            {
                if (i->data->address().ext() == addr.ext())
                {
                    return &(*i);
                }
            }
        }
    }

    if ((mode == deCONZ::NwkAddress) || ((mode == deCONZ::NoAddress) && addr.hasNwk()))
    {
        auto i = m_nodes.begin();
        auto end = m_nodes.end();

        for (; i != end; ++i)
        {
            if (i->data->address().hasNwk())
            {
                if (i->data->address().nwk() == addr.nwk())
                {
                    return &(*i);
                }
            }
        }
    }

    return 0;
}

NodeInfo *zmController::getNode(deCONZ::zmNode *dnode)
{
    if (dnode)
    {
        for (size_t i = 0; i < m_nodes.size(); i++)
        {
            if (m_nodes[i].data == dnode)
            {
                return &m_nodes[i];
            }
        }
    }

    return 0;
}

void zmController::bindReq(deCONZ::BindReq *req)
{
    m_bindQueue.append(*req);
}

void zmController::tick()
{
    // blur ticks
    static int slice = 0;
    tickCounter++;

    if (slice > 5)
    {
        m_steadyTimeRef = deCONZ::steadyTimeRef();
        slice = 0;
    }

    if (m_devState == deCONZ::InNetwork)
    {
        fetchZdpTick();
        bindTick();
    }
    else
    {
        auto i = m_apsRequestQueue.begin();
        const auto end = m_apsRequestQueue.end();

        for (; i != end; ++i)
        {
            i->setTimeout({0});
        }
    }

    if (m_devState == deCONZ::InNetwork)
    {
        if (m_otauActivity > 0)
            m_otauActivity--;

        if (m_zombieDelay > 0)
            m_zombieDelay--;

        if (m_sourceRoutingEnabled)
        {
            // TODO(mpi): Temporary disable source routing for ConBee III
            // The firmware support for it is still in development as of version 0x264e0900
            uint32_t fwVersion = deCONZ::master()->deviceFirmwareVersion();
            if ((fwVersion & 0xFF00) == 0x0900)
            {
                if ((fwVersion >> 16) <= 0x264e)
                {
                    setSourceRoutingEnabled(false);
                    return;
                }
            }

            static int sourceRoutesTick = 0;
            sourceRoutesTick++;

#ifdef ARCH_ARM
            if (sourceRoutesTick >= 3) // slow down a bit
#else
            if (m_fastDiscovery || sourceRoutesTick > 3)
#endif
            {
                SR_CalculateRouteForNode(m_nodes, m_routes, m_sourceRouteMinLqi, m_sourceRouteMaxHops, tickCounter);
                sourceRoutesTick = 0;
            }
        }

        if (slice == 1)
            zombieTick();

        else if (slice == 2)
            linkTick();

        else if (slice == 3)
            bindLinkTick();

        else if (slice == 4)
            deviceDiscoverTick();

        else if (slice == 5)
            linkCreateTick();

        slice++;
    }

    DBG_FlushLazy();
}

/*! Returns a vector of nwk address relays.
    The function checks if a source route exists where all relays are available.
 */
uint32_t getSourceRoute(const std::vector<SourceRoute> &sourceRoutes, const std::vector<NodeInfo> &nodes, std::array<uint16_t, 9> *result, size_t *resultSize)
{
    uint32_t srHash = 0;
    *resultSize = 0;

    if (nodes.empty())
    {
        return srHash;
    }

    if (!nodes.front().data->isCoordinator())
    {
        return srHash;
    }

    const auto &coordAddr = nodes.front().data->address();

    for (const auto &sr : sourceRoutes)
    {
        if (!sr.isValid())
        {
            continue;
        }

        if (!sr.isOperational())
        {
            continue;
        }

        for (const auto &addr : sr.hops())
        {
            const auto nodeInfo = std::find_if(nodes.begin(), nodes.end(), [&addr](const NodeInfo &n)
            {
                return (n.isValid() && n.data->address().ext() == addr.ext());
            });

            // check relay node known
            if (nodeInfo == nodes.end())
            {
                *resultSize = 0;
                break;
            }

            const auto *hop = nodeInfo->data;

            if (hop->isCoordinator())
            {
                continue;
            }

            if (hop->address().nwk() == coordAddr.nwk() || !hop->address().hasNwk())
            {
                *resultSize = 0;
                break;
            }

            // check node is FFD and reachable
            if (hop->isZombie() || hop->isEndDevice())
            {
                *resultSize = 0;
                break;
            }

            if (*resultSize < result->size())
            {
                (*result)[*resultSize] = hop->address().nwk();
                *resultSize += 1;
            }
        }

        if (*resultSize != 0)
        {
            srHash = sr.uuidHash();
            std::reverse(result->begin(), result->begin() + *resultSize);
            break; // found valid route
        }
    }

    return srHash;
}

//! Returns true if \p req contains a specific or ZCL Default Response for \p indZclFrame.
static bool ZCL_IsDefaultResponse(const deCONZ::ApsDataRequest &req)
{
    if (req.asdu().size() < 3) // need at least frame control | seqno | command id
    {
        return false;
    }

    if (req.profileId() == ZDP_PROFILE_ID)
    {
        return false;
    }

    // frame control | [manufacturer code] | seqno | command id
    uint8_t fc;
    uint8_t commandId;

    fc = static_cast<uint8_t>(req.asdu().at(0));

    if (fc & deCONZ::ZclFCClusterCommand)
    {
        return false;
    }

    if (req.asdu().size() >= 5 && req.asdu().at(0) & deCONZ::ZclFCManufacturerSpecific)
    {

        commandId = static_cast<uint8_t>(req.asdu().at(4));
    }
    else
    {
        commandId = static_cast<uint8_t>(req.asdu().at(2));
    }

    if (commandId == deCONZ::ZclDefaultResponseId)
    {
        return true;
    }

    return false;
}

bool zmController::sendNextApsdeDataRequest(NodeInfo *dst)
{
    int ret = 0;
    NodeInfo *node = 0;

    if (m_apsRequestQueue.empty())
    {
        return false;
    }

    if (!deCONZ::master()->hasFreeApsRequest())
    {
        return false;
    }

    if (APS_RequestsBusyCount(m_apsRequestQueue) > MaxApsBusyRequests)
    {
        return false;
    }

    auto i = m_apsRequestQueue.begin();
    const auto end = m_apsRequestQueue.end();

    if (m_otauActivity > 0)
    {
        i = std::find_if(m_apsRequestQueue.begin(), m_apsRequestQueue.end(),
                     [](const deCONZ::ApsDataRequest &req) {
                         return req.clusterId() == 0x0019 && req.state() == deCONZ::IdleState;
                     });
        if (i == m_apsRequestQueue.end())
        {
            i = m_apsRequestQueue.begin();
        }
        else
        {
            dst = nullptr;
        }
    }

    for (; i != end; ++i)
    {
        if (!(i->state() == deCONZ::IdleState))
            continue;

        node = nullptr; // reset
        ApsDataRequest &apsReq = *i;

        // get node
        if (apsReq.dstAddress().isNwkUnicast() || (apsReq.dstAddressMode() == deCONZ::ApsExtAddress))
        {
            if (!node)
            {
                node = getNode(apsReq.dstAddress(), deCONZ::NoAddress);
            }
        }

        if (dst && dst != node)
        {
            continue; // filter
        }

        // try to set nwk address if ext address is used
        if (apsReq.dstAddressMode() == deCONZ::ApsExtAddress)
        {
            if (node && node->data->address().hasNwk() /*&& node->data != m_nodes[0].data*/)
            {
                apsReq.dstAddress().setNwk(node->data->address().nwk());
                apsReq.setDstAddressMode(deCONZ::ApsNwkAddress);
            }
        }
        else if (node && apsReq.dstAddress().isNwkUnicast() && !apsReq.dstAddress().hasExt())
        {
            apsReq.dstAddress().setExt(node->data->address().ext());
        }


            // check send delay,
            if (/*node && */apsReq.sendDelay() > 0)
            {
                if (m_steadyTimeRef < apsReq.sendAfter())
                {
                    const deCONZ::TimeMs toWait = apsReq.sendAfter() - m_steadyTimeRef;
                    DBG_Printf(DBG_APS_L2, "Delay APS request id: %u delayed, %d ms till send\n", unsigned(apsReq.id()), int(toWait.val));
                    continue;
                }
            }

            uint busy = 0;

            if (apsReq.dstAddress().hasExt() && (apsReq.dstAddress().isNwkUnicast() || apsReq.dstAddressMode() == deCONZ::ApsExtAddress))
            {
                for (const deCONZ::ApsDataRequest &req: m_apsRequestQueue)
                {
                    if (req.state() != deCONZ::BusyState || !req.dstAddress().hasExt())
                        continue;

                    if (req.dstAddress().ext() == apsReq.dstAddress().ext())
                    {
                        busy++;
                    }
                }
            }

            if (apsReq.dstAddress().isNwkBroadcast() || apsReq.dstAddress().hasGroup())
            {
                const auto dt = deCONZ::steadyTimeRef() - m_apsGroupIndicationTimeRef;

                if (dt < deCONZ::TimeMs{m_apsGroupDelayMs})
                {
                    continue;
                }
            }
            else if (busy < 3 && apsReq.clusterId() == 0x0019) // let OTA be more agressive
            {

            }
            else if (busy < 3 && ZCL_IsDefaultResponse(apsReq))
            {

            }
            else if (busy >= m_maxBusyApsPerNode)
            {
                DBG_Printf(DBG_APS_L2, "Delay APS request id: %u to 0x%04X, profile: 0x%04X cluster: 0x%04X node already has busy %u\n",
                           apsReq.id(), apsReq.dstAddress().nwk(), apsReq.profileId(), apsReq.clusterId(), busy);
                continue;
            }
            else if (busy > 0 && node && node->data->isZombie())
            {
                continue;
            }
            else if (busy > 0 && node && !(node->data->macCapabilities() & deCONZ::MacReceiverOnWhenIdle) && getParameter(deCONZ::ParamPermitJoin) == 0)
            {
                continue;
            }

            if (deCONZ::master()->deviceProtocolVersion() >= DECONZ_PROTOCOL_VERSION_1_1)
            {
                // since protocol version 1.1 aps request version 2 is supported
                // aps request version 2 supports the node id
                apsReq.setVersion(2);
                if (node)
                {
                    apsReq.setNodeId(static_cast<uint16_t>(node->id));

                    if (m_sourceRoutingEnabled && deCONZ::master()->deviceProtocolVersion() >= DECONZ_PROTOCOL_VERSION_1_12)
                    {
                        std::array<uint16_t, 9> reqRelays{};
                        size_t resultSize = 0;
                        const auto srHash = getSourceRoute(node->data->sourceRoutes(), m_nodes, &reqRelays, &resultSize);

                        if (srHash != 0)
                        {
                            apsReq.setSourceRoute(reqRelays, resultSize, srHash);
                        }
                    }
                }
            }

            apsReq.setState(deCONZ::BusyState);

            // remember time of sending
            apsReq.setTimeout(m_steadyTimeRef);

            ret = m_master->apsdeDataRequest(apsReq);

            if (ret == 0)
            {
                if (dst && DBG_IsEnabled(DBG_APS))
                {
                    DBG_Printf(DBG_APS, "APS-DATA.request id: %u, addr: " FMT_MAC " profile: 0x%04X, cluster: 0x%04X, ep: 0x%02X/0x%02X queue: %d len: %d (send, fast lane)\n", apsReq.id(), FMT_MAC_CAST(apsReq.dstAddress().ext()), apsReq.profileId(), apsReq.clusterId(), apsReq.srcEndpoint(), apsReq.dstEndpoint(), (int)m_apsRequestQueue.size(), apsReq.asdu().size());
                }

                // note time of last send
                if (node)
                {
                    node->data->setLastApsRequestTime(m_steadyTimeRef);
                }
                else if (apsReq.dstAddress().isNwkBroadcast() || apsReq.dstAddressMode() == deCONZ::ApsGroupAddress)
                {
                    m_apsGroupIndicationTimeRef = deCONZ::steadyTimeRef();
                }
                return true;
            }
            else if (ret == -1)
            {
                DBG_Printf(DBG_APS, "CORE can't send APS data request id: %u\n", apsReq.id());
                apsReq.setState(deCONZ::IdleState);
            }
            else if (ret == -3)
            {
                // aps queue full retry later
                apsReq.setState(deCONZ::IdleState);
            }
            else if (ret == -2)
            {
                // not joined to a network
                apsReq.setState(deCONZ::FinishState);
            }
            else
            {
                DBG_Printf(DBG_INFO, "unknown master return state\n");
                // discard
                apsReq.setState(deCONZ::FinishState);
            }

            break;

    }

    return false;
}

/*! Emits a APSDE-DATA.confirm.
    \param id the APSDE-DATA.request id
    \param status the APS status for the confirmation
*/
void zmController::emitApsDataConfirm(uint8_t id, uint8_t status)
{
    DBG_Printf(DBG_APS, "emit artificial APSDE-DATA.confirm id: %u 0x%02X\n", id, status);

    bool found = false;
    for (deCONZ::ApsDataRequest &req : m_apsRequestQueue)
    {
        if (req.id() == id)
        {
            found = true;
            if (req.confirmed() || req.state() == deCONZ::IdleState)
            {
                continue;
            }

            req.setConfirmed(true);
            deCONZ::ApsDataConfirm conf(req, status);
            emit apsdeDataConfirm(conf);
            return;
        }
    }

    if (!found)
    {
        deCONZ::ApsDataConfirm conf(id, status);
        emit apsdeDataConfirm(conf);
    }
}


void zmController::verifyChildNode(NodeInfo *node)
{
    if (!node || !node->data)
    {
        return;
    }

    // we are the parent
    if (node->data->parentAddress() != m_nodes[0].data->address())
    {
        node->data->parentAddress() = m_nodes[0].data->address();
    }

    uint8_t cap = 0x80; // assumption
    if (node->data->macCapabilities() != 0)
    {
        cap = static_cast<uint8_t>(node->data->macCapabilities());
    }

    const deCONZ::TimeSeconds verifyChildNodeOffset{10};
    const deCONZ::TimeMs lastTry = node->data->lastDiscoveryTryMs(m_steadyTimeRef);
    if (lastTry.val == 0 || verifyChildNodeOffset < lastTry)
    {
        DBG_Printf(DBG_ZDP, "CORE: verify %s is child node\n", node->data->extAddressString().c_str());
        m_master->verifyChildNode(node->data->address(), cap);
        node->data->discoveryTimerReset(m_steadyTimeRef);
    }

    node->data->touch(m_steadyTimeRef);
}

void zmController::onNodeContextMenuRequest()
{
    auto *node = dynamic_cast<zmgNode*>(sender());
    Q_ASSERT(node);
    Q_ASSERT(node->data());

    if (!node->isSelected())
    {
        node->setSelected(true);
        NodeEvent event(NodeEvent::NodeSelected, node->data());
        emit nodeEvent(event);
    }

    NodeEvent event(NodeEvent::NodeContextMenu, node->data());
    emit nodeEvent(event);
}

void zmController::onSourceRouteChanged(const SourceRoute &sourceRoute)
{
    Q_ASSERT(!m_nodes.empty());

    const Address &destAddress = sourceRoute.hops().back();

    auto i = std::find_if(m_gsourceRoutes.begin(), m_gsourceRoutes.end(), [&sourceRoute](const zmgSourceRoute *sr)
    {
        return sr->uuidHash() == sourceRoute.uuidHash();
    });

    if (i != m_gsourceRoutes.end())
    {
        (*i)->updatePath();
        return; // already there
    }

    std::vector<zmgNode*> nodes;

    auto coord = m_nodes.begin();

    nodes.push_back(coord->g); // first hop coordinator

    for (const auto &relay : sourceRoute.hops())
    {
        if (relay.ext() == coord->data->address().ext())
        {
            continue; // coordinator already added
        }

        auto ni = getNode(relay, deCONZ::ExtAddress);
        if (!ni || !ni->isValid())
        {

            DBG_Printf(DBG_ROUTING, "can't create graphic source route, due missing relay node " FMT_MAC ", uuid: %s\n", FMT_MAC_CAST(relay.ext()), qPrintable(sourceRoute.uuid()));
            return;
        }

        nodes.push_back(ni->g);
    }

    auto dest = getNode(destAddress, deCONZ::ExtAddress);
    if (!dest || !dest->isValid())
    {
        DBG_Printf(DBG_ROUTING, "can't create graphic source route, due missing dest node " FMT_MAC ", uuid: %s\n", FMT_MAC_CAST(destAddress.ext()), qPrintable(sourceRoute.uuid()));
        return;
    }

    nodes.push_back(dest->g); // last hop destination

    zmgSourceRoute *gsr = new zmgSourceRoute(sourceRoute.uuidHash(), nodes, this);
    Q_ASSERT(gsr);
    m_scene->addItem(gsr);
    m_gsourceRoutes.push_back(gsr);
    DBG_Printf(DBG_ROUTING, "create graphic source route to dest node " FMT_MAC ", uuid: %s\n", FMT_MAC_CAST(destAddress.ext()), qPrintable(sourceRoute.uuid()));
}

void zmController::onSourceRouteDeleted(const QString &uuid)
{
    uint srHash = SR_HashUuid(uuid);

    auto i = std::find_if(m_gsourceRoutes.begin(), m_gsourceRoutes.end(), [srHash](const zmgSourceRoute *sr)
    {
        return sr->uuidHash() == srHash;
    });

    // remove old item if existing
    if (i != m_gsourceRoutes.end())
    {
        DBG_Printf(DBG_ROUTING, "on source route removed, uuid: %s\n", qPrintable(uuid));
        (*i)->hide();
        m_scene->removeItem(*i);
        (*i)->deleteLater();
        m_gsourceRoutes.erase(i);
    }

    auto i2 = std::find_if(m_routes.begin(), m_routes.end(), [srHash](const SourceRoute &route)
    {
        return srHash == route.uuidHash();
    });

    if (i2 != m_routes.end())
    {
        if (!i2->hops().empty())
        {
            auto *node = getNode(i2->hops().back(), deCONZ::ExtAddress);
            if (node && node->isValid())
            {
                node->data->removeSourceRoute(i2->uuidHash());
            }
        }
        m_routes.erase(i2);
    }
}

void zmController::initSourceRouting(const QSettings &config)
{
    bool ok = false;
    if (config.contains("source-routing/enabled"))
    {
        m_sourceRoutingEnabled = config.value("source-routing/enabled").toBool();
    }

    if (config.contains("source-routing/required"))
    {
        m_sourceRouteRequired = config.value("source-routing/required").toBool();
    }

    if (config.contains("source-routing/min-lqi"))
    {
        auto minLqi = config.value("source-routing/min-lqi").toUInt(&ok);
        if (ok && minLqi >= 60 && minLqi <= 255)
        {
            m_sourceRouteMinLqi = minLqi;
        }
    }

    if (config.contains("source-routing/max-hops"))
    {
        auto maxHops = config.value("source-routing/max-hops").toUInt(&ok);
        if (ok && maxHops > 1 && maxHops <= 9)
        {
            m_sourceRouteMaxHops = maxHops;
        }
    }

    if (config.contains("source-routing/min-lqi-display"))
    {
        int minLqiDisp = config.value("source-routing/min-lqi-display").toInt(&ok);
        if (ok && minLqiDisp >= 0 && minLqiDisp <= 255)
        {
            m_minLqiDisplay = minLqiDisp;
        }
    }
}

void zmController::storeSourceRoutingConfig(QSettings *config)
{
    config->setValue("source-routing/enabled", m_sourceRoutingEnabled);
    config->setValue("source-routing/min-lqi", m_sourceRouteMinLqi);
    config->setValue("source-routing/max-hops", m_sourceRouteMaxHops);
    config->setValue("source-routing/min-lqi-display", m_minLqiDisplay);

    for (auto &route : m_routes)
    {
        if (route.needSave())
        {
            emit sourceRouteCreated(route);
            route.saved();
        }
    }
}

void zmController::deleteSourcesRouteWith(const Address &addr)
{
    while (1)
    {
        auto i = std::find_if(m_routes.begin(), m_routes.end(), [&addr](const SourceRoute &sr) { return sr.hasHop(addr); });
        if (i != m_routes.end())
        {
            for (const auto &n : m_nodes)
            {
                if (n.isValid())
                {
                    n.data->removeSourceRoute(i->uuidHash());
                }
            }

            emit sourceRouteDeleted(i->uuid());
            m_routes.erase(i);
            continue;
        }
        break;
    }
}

/*! Handle mac data request (end device polling),
    \param address - the polling node address
*/
void zmController::onMacPoll(const Address &address, uint32_t lifeTime)
{
    NodeInfo *node = getNode(address, deCONZ::NoAddress);

    m_deviceWatchdogOk |= DEVICE_RX_NETWORK_OK;

    if (!node || !node->data)
    {
        return;
    }

    visualizeNodeIndication(node, deCONZ::IndicateReceive);

    emit nodeEvent(NodeEvent(NodeEvent::NodeMacDataRequest, node->data));

    // try to query missing ZDP pieces

    if (getParameter(deCONZ::ParamPermitJoin) > 0)
    {
        return;
    }

    if (lifeTime >= 0xfffffffcUL) // unknown child
    {
        // directly add to child table since leave with rejoin is broken
        verifyChildNode(node);
        return;
    }

    if (node->data->nodeDescriptor().isNull() && node->data->retryCount(deCONZ::ReqNodeDescriptor) < 2)
    {
        if (sendNodeDescriptorRequest(node))
        {
            node->data->retryIncr(deCONZ::ReqNodeDescriptor);
            return;
        }
    }

    if (node->data->endpoints().empty() && node->data->retryCount(deCONZ::ReqActiveEndpoints) < 2)
    {
        if (sendActiveEndpointsRequest(node))
        {
            node->data->retryIncr(deCONZ::ReqActiveEndpoints);
            return;
        }
    }

    for (const auto &sd: node->data->simpleDescriptors())
    {
        if (sd.deviceId() == 0xffff && node->data->retryCount(deCONZ::ReqSimpleDescriptor) < 2)
        {
            if (sendSimpleDescriptorRequest(node, sd.endpoint()))
            {
                node->data->retryIncr(deCONZ::ReqSimpleDescriptor);
                return;
            }
        }
    }
}

void zmController::onBeacon(const Beacon &beacon)
{
    DBG_Printf(DBG_INFO, "Beacon src: 0x%04X ch: %u updateId: %u\n", beacon.source, beacon.channel, beacon.updateId);

    const uint8_t updateId = getParameter(deCONZ::ParamNetworkUpdateId);

    if (beacon.updateId < updateId)
    {
        DBG_Printf(DBG_INFO, "* node has lower updateId should be %u\n", updateId);

        deCONZ::Address addr;
        addr.setNwk(beacon.source);

        NodeInfo *node = getNode(addr, deCONZ::NwkAddress);
        if (!node || !node->data)
        {
            return;
        }

        node->data->setNeedRejoin(true);
    }
    else if (beacon.updateId > updateId)
    {
        DBG_Printf(DBG_INFO, "* node has higher updateId should be %u, TODO handle\n", updateId);
    }
}

void zmController::timeoutTick()
{
    // process running APS data requests
    auto i = m_apsRequestQueue.begin();
    const auto end = m_apsRequestQueue.end();

    for (; i != end; ++i)
    {
        if (i->state() == deCONZ::BusyState || i->state() == deCONZ::ConfirmedState)
        {
            const deCONZ::SteadyTimeRef t = i->timeout() + (i->state() == deCONZ::ConfirmedState ? MaxConfirmedTimeOut : MaxTimeOut);

            if (t <= m_steadyTimeRef)
            {
                DBG_Printf(DBG_APS, "aps request id: %d prf: 0x%04X cl: 0x%04X timeout (confirmed: %u) to " FMT_MAC " (0x%04X)\n",
                 i->id(), i->profileId(), i->clusterId(), i->confirmed(), FMT_MAC_CAST(i->dstAddress().ext()), i->dstAddress().nwk());

                if (i->confirmed())
                {
                    i->setState(deCONZ::FinishState);
                }
                else
                {
                    DBG_Printf(DBG_ERROR, "aps request id: %d prf: 0x%04X cl: 0x%04X timeout NOT confirmed to " FMT_MAC " (0x%04X)\n",
                                          i->id(), i->profileId(), i->clusterId(), FMT_MAC_CAST(i->dstAddress().ext()), i->dstAddress().nwk());

                    i->setState(deCONZ::FailureState);
                }

                if (i->profileId() == ZDP_PROFILE_ID &&
                    i->clusterId() == ZDP_NWK_ADDR_CLID)
                {
                    NodeInfo *node = getNode(i->dstAddress(), deCONZ::ExtAddress);

                    if (node && node->data &&
                       (!isValid(node->data->lastSeen()) || deCONZ::TimeSeconds{30} < (m_steadyTimeRef - node->data->lastSeen())))
                    {
                        node->data->recvErrorsIncrement();
                        visualizeNodeIndication(node, deCONZ::IndicateError);
                    }
                }
            }
        }
        else if (i->state() == deCONZ::FinishState)
        {
            DBG_Printf(DBG_APS, "aps request id: %d finished, erase from queue\n", i->id());

            if (!i->confirmed())
            {
                uint8_t status = (deCONZ::master()->netState() == deCONZ::InNetwork)
                                       ? (uint8_t)deCONZ::ApsNoAckStatus
                                       : (uint8_t)deCONZ::NwkNoNetworkStatus;
                emitApsDataConfirm(i->id(), status);
                i->setConfirmed(true);
                return;
            }
            m_apsRequestQueue.erase(i);
            return; // don't proceed since queue might be modified now
        }
        else if (i->state() == deCONZ::FailureState)
        {
            DBG_Printf(DBG_APS, "aps request id: %d failed, erase from queue\n", i->id());

            if (!i->confirmed())
            {
                emitApsDataConfirm(i->id(), deCONZ::ApsNoAckStatus);
                i->setConfirmed(true);
                return;
            }
            m_apsRequestQueue.erase(i);
            return; // don't proceed since queue might be modified now
        }
    }
}

void zmController::fetchZdpTick()
{
    if (!m_master->connected())
        return;

    if (m_nodes.empty())
        return;

    if (!deCONZ::master()->hasFreeApsRequest())
        return;

    // only fetch if we know who we are
    if (!m_nodes[0].data->address().hasExt() || !m_nodes[0].data->address().hasNwk())
        return;

    deCONZ::zmNode *node = nullptr;
    FastDiscover *fastDiscoverNode = nullptr;

    if (!m_fastDiscover.empty())
    {
        if (m_fastDiscover.front().done)
        {
            DBG_Printf(DBG_ZDP, "ZDP finished fast discover for " FMT_MAC "\n", FMT_MAC_CAST(m_fastDiscover.front().addr.ext()));
            if (m_fastDiscover.size() > 1)
            {
                m_fastDiscover.front() = m_fastDiscover.back();
            }
            m_fastDiscover.pop_back();
            return;
        }

        for (FastDiscover &fd : m_fastDiscover)
        {
            if (deCONZ::TimeSeconds{180} < m_steadyTimeRef - fd.tAnnounce)
            {
                fd.clusters = {}; // release
            }

            if (fd.errors > 2)
            {
                continue;
            }

            NodeInfo *ni = getNode(fd.addr, deCONZ::ExtAddress);
            if (ni && ni->data)
            {
                node = ni->data;
                fastDiscoverNode = &fd;
                break;
            }
        }
    }

    deCONZ::RequestId fastFetchItem = deCONZ::ReqUnknown;
    if (fastDiscoverNode && node)
    {
        unsigned done = 0;
        for (uint16_t clusterId : fastDiscoverNode->clusters)
        {
            if (clusterId == ZDP_NODE_DESCRIPTOR_CLID)
            {
                if (!node->nodeDescriptor().isNull())
                {
                    done++;
                }
                else
                {
                    fastFetchItem = deCONZ::ReqNodeDescriptor;
                    break;
                }
            }
            else if (clusterId == ZDP_ACTIVE_ENDPOINTS_CLID)
            {
                if (!node->endpoints().empty())
                {
                    done++;
                }
                else
                {
                    fastFetchItem = deCONZ::ReqActiveEndpoints;
                    break;
                }
            }
            else if (clusterId == ZDP_SIMPLE_DESCRIPTOR_CLID)
            {
                if (!node->simpleDescriptors().empty() &&
                     node->endpoints().size() == node->simpleDescriptors().size())
                {
                    done++;
                }
                else
                {
                    fastFetchItem = deCONZ::ReqSimpleDescriptor;
                    break;
                }
            }
            else
            {
                done++;
            }
        }

        if (done == fastDiscoverNode->clusterCount)
        {
            fastDiscoverNode->done = 1;
            return;
        }
    }

    if (m_fetchCurNode >= (int)m_nodes.size())
    {
        m_fetchCurNode = 0;
    }

    if (!node)
    {
        node = m_nodes[m_fetchCurNode].data;
    }

    if (!node)
    {
        m_fetchCurNode++;
        return;
    }

    if (!(node->macCapabilities() & deCONZ::MacReceiverOnWhenIdle))
    {
        // quirk mode for Xiami QBKG03LM, with wrong "rx on when idle"
        // repair node descriptor
        // TODO(mpi): do we need this anymore with DDF in place?
        if (!node->nodeDescriptor().isNull() && node->nodeDescriptor().manufacturerCode_t() == 0x1037_mfcode && node->modelId().startsWith(QLatin1String("lumi.ctrl_neutral")))
        {
            NodeDescriptor nd = node->nodeDescriptor();
            nd.setMacCapabilities(nd.macCapabilities() | deCONZ::MacReceiverOnWhenIdle);
            node->setMacCapabilities(nd.macCapabilities());
            NodeEvent event(NodeEvent::UpdatedNodeDescriptor, node);
            emit nodeEvent(event);
        }

        m_fetchCurNode++;
        return;
    }

    // check if we are too busy for any new requests
    int busyCount = 0;
    int zdpCount = 0;

    for (const deCONZ::ApsDataRequest &req : m_apsRequestQueue)
    {
        if ((req.state() == deCONZ::BusyState)/* || (m_apsRequestQueue[i].state() == deCONZ::ConfirmedState)*/)
        {
            busyCount++;
            if (busyCount > MaxApsBusyRequests)
            {
                return;
            }
        }

        if (!req.confirmed() && req.dstAddress().ext() == node->address().ext())
        {
            if (DBG_IsEnabled(DBG_INFO_L2))
            {
                DBG_Printf(DBG_ZDP, "ZDP skip fetch, node " FMT_MAC " has unconfirmed requests [1]\n", FMT_MAC_CAST(node->address().ext()));
            }
            m_fetchCurNode++;
            return;
        }

        if (req.profileId() == ZDP_PROFILE_ID && !req.confirmed())
        {
            zdpCount++;
        }

        if (zdpCount >= MaxApsRequestsZdp)
        {
            // DBG_Printf(DBG_ZDP, "ZDP skip fetch, node 0x%0llX total unconfirmed zdp request count %d [2]\n", node->address().ext(), zdpCount);
            return;
        }
    }

    node->checkWaitState();

    if (node->isZombie())
    {
        m_fetchCurNode++;
        return;
    }

    if (node->state() != deCONZ::IdleState)
    {
        m_fetchCurNode++;
        return;
    }


    deCONZ::RequestId curItem = fastFetchItem;

    if (curItem == deCONZ::ReqUnknown)
    {
        curItem = node->curFetchItem();

        if (!node->needFetch(curItem))
        {
            curItem = node->nextCurFetchItem();


            // end
            if (curItem == deCONZ::ReqUnknown)
            {
                m_fetchCurNode++;
                return;
            }

            if (!node->needFetch(curItem))
            {
                return;
            }
        }
    }

    m_fetchCurNode++;

    if (!fastDiscoverNode && !isValid(node->lastSeen()))
    {
        return;
    }

    {
        const deCONZ::TimeMs dt = m_steadyTimeRef - node->lastSeen();
        if (!fastDiscoverNode && deCONZ::TimeSeconds{600} < dt)
        {
            DBG_Printf(DBG_ZDP, "ZDP skip fetch " FMT_MAC ", diff last seen: %d ms [4]\n", FMT_MAC_CAST(node->address().ext()), int(dt.val));
            return;
        }
    }

    // workaround to update lights nwkUpdateId for supported lights
    if (node->needRejoin() && (
        node->nodeDescriptor().manufacturerCode() == VENDOR_PHILIPS))
    {
        const bool removeChildren = false;
        const bool rejoin = true;
        if (sendMgmtLeaveRequest(node, removeChildren, rejoin))
        {
            node->setNeedRejoin(false);
            return;
        }
    }

    bool sendDone = false;

    ApsDataRequest apsReq;
    QDataStream stream(&apsReq.asdu(), QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    // ZDP Header
    apsReq.dstAddress() = node->address();
    if (node->address().hasNwk())
    {
        apsReq.setDstAddressMode(deCONZ::ApsNwkAddress);
    }
    else if (node->address().hasExt())
    {
        apsReq.setDstAddressMode(deCONZ::ApsExtAddress);
    }

    apsReq.setDstEndpoint(ZDO_ENDPOINT);
    apsReq.setSrcEndpoint(ZDO_ENDPOINT);
    apsReq.setProfileId(ZDP_PROFILE_ID);
    apsReq.setRadius(0);
    apsReq.setState(deCONZ::BusyState); // means ok, do process and send
    stream << genSequenceNumber();

    switch (curItem)
    {
    case deCONZ::ReqIeeeAddr:
    {
        uint8_t requestType = 0x00; // single request

        apsReq.setClusterId(ZDP_IEEE_ADDR_CLID);
        stream << node->address().nwk();

//        if (!node->isEndDevice())
//        {
//            requestType = 0x01; // extended request
//        }

        stream << requestType;
        stream << (uint8_t)0x00; // ignore start index
    }
        break;

    case deCONZ::ReqNwkAddr:
    {
        if (node->address().hasExt())
        {
            uint8_t requestType = 0x00; // single request

            apsReq.setDstAddressMode(deCONZ::ApsNwkAddress);
            apsReq.dstAddress().setNwk(BroadcastRxOnWhenIdle);
            apsReq.setClusterId(ZDP_NWK_ADDR_CLID);
            stream << (quint64)node->address().ext();

            // broadcasts don't allow ACKs
            apsReq.setTxOptions(apsReq.txOptions() & ~deCONZ::ApsTxAcknowledgedTransmission);

//                if (!node->isEndDevice())
//                {
//                    requestType = 0x01; // extended request
//                }

            stream << requestType;
            stream << (uint8_t)0x00; // ignore start index

            // prevent endless broadcasts if node is not available
            // TODO check for timeout on response and generate retry/errors
            node->setFetched(deCONZ::ReqNwkAddr, true);
        }
    }
        break;

    case deCONZ::ReqNodeDescriptor:
    {
        if (node->nodeDescriptor().isNull())
        {
            DBG_Printf(DBG_ZDP, "ZDP node descriptor request to " FMT_MAC "\n", FMT_MAC_CAST(node->address().ext()));
            apsReq.setClusterId(ZDP_NODE_DESCRIPTOR_CLID);
            stream << node->address().nwk();
        }
        else
        {
            node->setFetched(deCONZ::ReqNodeDescriptor, true);
            apsReq.setState(deCONZ::IdleState);
        }
    }
        break;

    case deCONZ::ReqPowerDescriptor:
    {
        apsReq.setClusterId(ZDP_POWER_DESCRIPTOR_CLID);
        stream << node->address().nwk();
    }
        break;

    case deCONZ::ReqMgmtLqi:
    {
        curItem = node->nextCurFetchItem();
    }
        return;

    case deCONZ::ReqMgmtBind:
    {
        apsReq.setClusterId(ZDP_MGMT_BIND_REQ_CLID);
        stream << (uint8_t)0;
    }
        break;

    case deCONZ::ReqActiveEndpoints:
    {
        if (node->nodeDescriptor().manufacturerCode() == VENDOR_115F && node->endpoints().size() > 3) // assume already fetched
        {
            node->setFetched(deCONZ::ReqActiveEndpoints, true);
            apsReq.setState(deCONZ::IdleState); // disable send
        }
        else if (getParameter(deCONZ::ParamPermitJoin) > 0 && node->endpoints().size() > 0)
        {
            apsReq.setState(deCONZ::IdleState); // disable send
        }
        else
        {
            DBG_Printf(DBG_ZDP, "ZDP active ep request to %s\n", node->extAddressString().c_str());
            apsReq.setClusterId(ZDP_ACTIVE_ENDPOINTS_CLID);
            stream << node->address().nwk();
            node->retryIncr(deCONZ::ReqActiveEndpoints);
        }
    }
        break;

    case deCONZ::ReqUserDescriptor:
        if (!node->nodeDescriptor().hasUserDescriptor())
        {
            node->setFetched(deCONZ::ReqUserDescriptor, true);
            apsReq.setState(deCONZ::IdleState); // disable send
            break;
        }
        else
        {
            apsReq.setClusterId(ZDP_USER_DESCRIPTOR_CLID);
            stream << node->address().nwk();
        }
        break;

    case deCONZ::ReqSimpleDescriptor:
        {
            int ep = node->getNextUnfetchedEndpoint();

            if (ep != -1)
            {
                auto sd = std::find_if(node->simpleDescriptors().cbegin(), node->simpleDescriptors().cend(), [ep](const auto &sd) { return sd.endpoint() == ep;});

                if (sd == node->simpleDescriptors().cend())
                {
                    apsReq.setClusterId(ZDP_SIMPLE_DESCRIPTOR_CLID);
                    stream << node->address().nwk();
                    stream << static_cast<uint8_t>(ep);
                }
                else if (getParameter(deCONZ::ParamPermitJoin) > 0 && sd->deviceId() != 0xffff && sd->endpoint() == ep)
                {
                    // valid enough, don't query while search is active
                    apsReq.setState(deCONZ::IdleState); // disable send
                }
                else if (sd->deviceId() != 0xffff && sd->endpoint() == ep && (node->address().ext() & macPrefixMask) != deMacPrefix)
                {
                    // valid enough
                    apsReq.setState(deCONZ::IdleState); // disable send
                }
                else
                {
                    apsReq.setClusterId(ZDP_SIMPLE_DESCRIPTOR_CLID);
                    stream << node->address().nwk();
                    stream << static_cast<uint8_t>(ep);
                }
            }
            else
            {
                apsReq.setState(deCONZ::IdleState);
            }
        }
        break;

    default:
        apsReq.setState(deCONZ::IdleState);
        break;
    }

    if (apsReq.state() == deCONZ::BusyState) // ok
    {
#if 1
        bool found = false;

        // allow only one request per node

        for (auto i = m_apsRequestQueue.cbegin(); i != m_apsRequestQueue.cend(); ++i)
        {
            if (i->state() == deCONZ::BusyState ||
                i->state() == deCONZ::IdleState ||
                i->state() == deCONZ::ConfirmedState)
            {
                if (i->profileId() == ZDP_PROFILE_ID)
                {
                    if (i->dstAddress().hasExt() && apsReq.dstAddress().hasExt())
                    {
                        if (i->dstAddress().ext() == apsReq.dstAddress().ext())
                        {
                            found = true;
                            break;
                        }
                    }
                    else if (i->dstAddress().hasNwk() && apsReq.dstAddress().hasNwk())
                    {
                        if (i->dstAddress().nwk() == apsReq.dstAddress().nwk())
                        {
                            found = true;
                            break;
                        }
                    }
                }
            }
        }

        // use APS ACKs only for unicast addressing and if enabled in preferences
        if (deCONZ::netEdit()->apsAcksEnabled() && !apsReq.dstAddress().isNwkBroadcast())
        {
            if (m_nodes[0].data->address().ext() != apsReq.dstAddress().ext())
            {
                apsReq.setTxOptions(apsReq.txOptions() | deCONZ::ApsTxAcknowledgedTransmission);
            }
        }
        else if (!node->nodeDescriptor().receiverOnWhenIdle() && !apsReq.dstAddress().isNwkBroadcast())
        {
            apsReq.setTxOptions(apsReq.txOptions() | deCONZ::ApsTxAcknowledgedTransmission);
        }

        // don't send twice
        if (!found)
#endif
        {
            if (apsReq.dstAddress().isNwkBroadcast())
            {
                apsReq.setState(deCONZ::IdleState);
            }
            else if (m_nodes[0].data->address().ext() == apsReq.dstAddress().ext())
            {
                // commands to own node don't need much attention
                apsReq.setSendDelay(20);
                apsReq.setState(deCONZ::IdleState);
            }
            else
            {
                apsReq.setState(deCONZ::IdleState);
            }

            if (apsdeDataRequest(apsReq) == deCONZ::Success)
            {
                sendDone = true;
            }
        }
    }

    if (sendDone)
    {
        if (node)
        {
            if (!apsReq.dstAddress().isNwkBroadcast())
            {
                node->setWaitState(1);
            }
        }
    }
    else
    {
        if (node)
        {
            curItem = node->nextCurFetchItem();
        }
    }
}

/*!
    Checks one node per call for zombie timeout.
 */
void zmController::zombieTick()
{
    if (!autoFetchFFD())
    {
        return;
    }

    if (m_nodes.size() <= 1) // dont kill coord
        return;

    if (m_zombieDelay > 0)
    {
        return;
    }

    // check for zombies
    if (m_nodeZombieIter >= (int)m_nodes.size())
    {
        m_nodeZombieIter = 1;       
    }

    NodeInfo &info = m_nodes[m_nodeZombieIter];
    m_nodeZombieIter++;

    if (!info.data || !info.g)
    {
        return;
    }

    deCONZ::zmNode *node = info.data;

    deCONZ::SteadyTimeRef minSeenTime = node->lastSeen();
    deCONZ::TimeSeconds delta = ZombieDelta;
    int knownByNeighbors = 0;

    if      (m_nodes.size() < 10)  { delta = deCONZ::TimeSeconds{600}; }
    else if (m_nodes.size() < 50)  { delta = deCONZ::TimeSeconds{1800}; }
    else if (m_nodes.size() < 100) { delta = deCONZ::TimeSeconds{3000}; }
    else                           { delta = deCONZ::TimeSeconds{3600}; }

    int zombieCount = 0;
    {
        auto ni = m_nodes.begin();
        auto nend = m_nodes.end();

        for ( ;ni != nend; ++ni)
        {
            if (!ni->data)
            {
                continue;
            }

            if (ni->data->isZombie())
            {
                zombieCount++;
                continue;
            }

            const zmNeighbor *neib = ni->data->getNeighbor(node->address());
            if (!neib)
            {
                continue;
            }

            if (neib->lqi() < 10)
            {
                continue;
            }

            knownByNeighbors++;

            if (!node->nodeDescriptor().receiverOnWhenIdle())
            {
                if (!isValid(minSeenTime) || minSeenTime < neib->lastSeen())
                {
                    if (isValid(neib->lastSeen()))
                    {
                        minSeenTime = neib->lastSeen();
                    }
                }
            }
        }
    }

    deCONZ::TimeMs dt;
    if (isValid(minSeenTime))
    {
        dt = m_steadyTimeRef - minSeenTime;
    }

    if (!node->nodeDescriptor().receiverOnWhenIdle())
    {
        delta = ZombieDeltaEndDevice;

        if (knownByNeighbors == 0)
        {
            minSeenTime = node->lastSeen();
        }
    }

    if (!isValid(minSeenTime) || (delta < dt) || (node->recvErrors() >= MaxRecvErrorsZombie))
    {
        uint16_t ownNwk = m_nodes[0].data->address().nwk();
        if (!node->isZombie() && (node->address().nwk() != ownNwk) && node->nodeDescriptor().receiverOnWhenIdle() && node->recvErrors() > MaxRecvErrors)
        {
            DBG_Printf(DBG_INFO, "%s seems to be a zombie recv errors %d\n", node->extAddressString().c_str(), node->recvErrors());
            deleteNode(&info, NodeRemoveZombie);
            NodeEvent event(NodeEvent::NodeZombieChanged, node);
            emit nodeEvent(event);
            zombieCount++;
            if (info.g)
            {
                info.g->requestUpdate(); // redraw
            }
        }
    }
    else if (isValid(minSeenTime))
    {
        if (node->isZombie())
        {
            DBG_Printf(DBG_INFO, "%s is alive again\n", node->extAddressString().c_str());
            wakeNode(&info);
            NodeEvent event(NodeEvent::NodeZombieChanged, node);
            emit nodeEvent(event);
            zombieCount--;
        }
    }

    m_zombieCount = zombieCount;
}

/*!
    Creates links between neighbors if they dont exist;
 */
void zmController::linkCreateTick()
{
    if (m_createLinkQueue.isEmpty())
    {
        return;
    }

    AddressPair addrPair = m_createLinkQueue.takeFirst();

    if (!addrPair.aAddr.hasNwk() || !addrPair.bAddr.hasNwk())
    {
        return;
    }

    if (gHeadlessVersion) // no links here
    {
        return;
    }

    NodeInfo *a = getNode(addrPair.aAddr, deCONZ::NwkAddress);
    if (a && a->data && !a->data->isZombie())
    {
        NodeInfo *b = getNode(addrPair.bAddr, deCONZ::NwkAddress);
        if (b && b->data && !b->data->isZombie())
        {
            if (!a->data->isEndDevice() && !b->data->isEndDevice())
            {
                const zmNeighbor *neibA = a->data->getNeighbor(b->data->address());
                const zmNeighbor *neibB = b->data->getNeighbor(a->data->address());

                if ((!neibA && !neibB) || ((neibA && neibA->lqi() == 0) || (neibB && neibB->lqi() == 0)))
                {
                    if (DBG_IsEnabled(DBG_INFO_L2))
                    {
                        DBG_Printf(DBG_INFO_L2, "skip create link for 0x%04X (lqi: %u) - 0x%04X (lqi: %u)\n",
                               a->data->address().nwk(), (neibA ? neibA->lqi() : 0),
                               b->data->address().nwk(), (neibB ? neibB->lqi() : 0));
                    }
                    return;
                }
            }

            LinkInfo *li = linkInfo(a->g, b->g, deCONZ::UnknownRelation);
            Q_UNUSED(li);
        }
    }
}

/*!
    Calculates age of links and remove deadlinks.
 */
void zmController::linkTick()
{
    if (!autoFetchFFD())
    {
        return;
    }

    if (m_neighbors.isEmpty())
    {
        return;
    }

#if ARCH_ARM
    if (m_steadyTimeRef - m_linkUpdateTime < deCONZ::TimeMs{1000})
#else
    if (m_steadyTimeRef - m_linkUpdateTime < deCONZ::TimeMs{250})
#endif
    {
        return;
    }

    m_linkUpdateTime = m_steadyTimeRef;

    if (m_linkIter >= m_neighbors.size())
    {
        m_linkIter = 0;
    }

    LinkInfo &li = m_neighbors[m_linkIter];

    if (li.link && !m_showNeighborLinks)
    {
        if (li.link->isVisible() != m_showNeighborLinks)
        {
            li.link->setVisible(m_showNeighborLinks);
        }
    }

    // t0 holds the node which wassn't seen at least
    // t0 now holds the difference
    deCONZ::TimeMs t0 = m_steadyTimeRef - li.linkAgeUnix;

    if (!m_showNeighborLinks)
    {

    }
    else if (!li.a || !li.a->data() || !li.b || !li.b->data())
    {
        // invalid handles
    }
    else if (t0 < deCONZ::TimeSeconds{MaxLinkAge} && li.a && li.b && li.link
             && !li.a->data()->isZombie() && !li.b->data()->isZombie())
    {
        // if neighbor tables contain entries to each other calculate the lqi
        uint lqi = 0;
        uint divider = 0;

        if (!li.a->hasLink(li.link) || !li.b->hasLink(li.link))
        {
            // remove later
            li.link->hide();
            li.a = 0;
            li.b = 0;
            return;
        }

        li.linkAge = (qreal)t0.val / (qreal)MaxLinkAge;
        uint8_t lqiA = 0;
        uint8_t lqiB = 0;
        int routers = 0;

        if (!li.a->data()->isEndDevice()) // TODO check if isEndDevice() works correctly
        {
            const zmNeighbor *neib = li.a->data()->getNeighbor(li.b->data()->address());
            if (neib)
            {
                lqi = neib->lqi();
                lqiA = lqi;
                divider++;
            }
            routers++;
        }

        if (!li.b->data()->isEndDevice()) // TODO check if isEndDevice() works correctly
        {
            const zmNeighbor * neib = li.b->data()->getNeighbor(li.a->data()->address());
            if (neib)
            {
                lqiB = neib->lqi();
                lqi += lqiB;
                divider++;
            }
            routers++;
        }

        if (divider == 0) // no lqi known
        {
            li.linkLqi = li.linkAge;
            li.link->setMiddleText(QString());
        }
        else if (lqiA == 0 && lqiB == 0)
         //|| (m_minLqiDisplay >= 0 && divider > 0 && routers == 2 && (lqiA == 0 || lqiB == 0)))
        {
            DBG_Printf(DBG_INFO, "remove link for (%X, %X)\n",
                       li.a->data()->address().nwk(),
                       li.b->data()->address().nwk());

            li.a->remLink(li.link);
            li.b->remLink(li.link);
            li.link->hide();
            m_neighborsDead.append(li);
            li.link = 0;
            return;
        }
        else if (lqiA || lqiB) // at least one entry known
        {
            if (routers == 2 && (lqiA && lqiB))
            {
                lqi = (lqiA + lqiB) / 2;
            }
            else
            {
                lqi = qMax(lqiA, lqiB);
            }

            // spread the lower values with a approximated function (curve)
            if (lqi > 0 && lqi <= 255.0)
            {
                li.linkLqi = 1.0 - (double(lqi) / 255.0);
            }
            else
            {
                li.linkLqi = 0;
            }

            if (m_showLqi)
            {
                li.link->setMiddleText(QString(QLatin1String("%1/%2")).arg(lqiA).arg(lqiB));
            }
            else if (!li.link->middleText().isEmpty())
            {
                li.link->setMiddleText(QLatin1String(""));
            }
        }

        if (m_linkViewMode == LinkShowAge)
        {
            li.link->setValue(li.linkAge);
        }
        else if (m_linkViewMode == LinkShowLqi)
        {
            li.link->setValue(li.linkLqi);
        }
        else
        {
            li.link->setValue(0.3);
        }

        if (routers == 2 && m_minLqiDisplay > int(lqi))
        {
            if (li.link->isVisible())
            {
                li.link->setVisible(false);
            }
        }
        else if (!li.link->isVisible())
        {
            li.link->setVisible(true);
            li.link->updatePosition();
        }
    }
    else if (li.a && li.b && li.link)
    {
        DBG_Printf(DBG_INFO, "remove dead link for (%X, %X)\n",
                li.a->data()->address().nwk(),
                li.b->data()->address().nwk());

        li.a->remLink(li.link);
        li.b->remLink(li.link);
        li.link->hide();
        m_neighborsDead.append(li);
        li.link = 0;
    }
    else if (li.link)
    {
        DBG_Printf(DBG_INFO, "remove orphan link\n");
        li.link->hide();
        m_neighborsDead.append(li);
        li.link = 0;
    }
    else
    {
        m_neighbors.removeAt(m_linkIter);
    }

    m_linkIter++;
}

void zmController::neighborTick()
{
    if (!autoFetchFFD())
    {
        return;
    }

    if (m_nodes.empty())
    {
        return;
    }

    if (m_neibIter >= (int)m_nodes.size())
    {
        m_neibIter = 0;
    }

    NodeInfo &node = m_nodes[m_neibIter];
    node.data->removeOutdatedNeighbors(ZombieDelta.val * 4);
    m_neibIter++;
}

void zmController::bindLinkTick()
{
    if (m_bindLinkQueue.empty())
        return;

    Address addr = m_bindLinkQueue.back();
    m_bindLinkQueue.pop_back();
    NodeInfo *node = getNode(addr, deCONZ::ExtAddress);

    if (!node)
        return;

    for (const auto &bnd : node->data->bindingTable())
    {
        checkBindingLink(bnd);
    }
}

void zmController::bindTick()
{
    if (m_bindQueue.isEmpty())
        return;

    NodeInfo *node;
    Address addr;
    deCONZ::BindReq req = m_bindQueue.takeFirst();
    addr.setExt(req.srcAddr);
    node = getNode(addr, deCONZ::ExtAddress);

    if (node && node->data->address().hasNwk())
    {
        ApsDataRequest apsReq;
        QDataStream stream(&apsReq.asdu(), QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);

        apsReq.dstAddress() = node->data->address();
        apsReq.setDstAddressMode(deCONZ::ApsNwkAddress);

        apsReq.setDstEndpoint(ZDO_ENDPOINT);
        apsReq.setSrcEndpoint(ZDO_ENDPOINT);
        apsReq.setProfileId(ZDP_PROFILE_ID);
        apsReq.setTxOptions(deCONZ::ApsTxAcknowledgedTransmission
                            // | deCONZ::ApsTxSecurityEnabledTransmission
                            // | deCONZ::ApsTxUseNwk
                            );
        apsReq.setRadius(10);
        apsReq.setClusterId(req.unbind ? ZDP_UNBIND_REQ_CLID : ZDP_BIND_REQ_CLID);
        stream << genSequenceNumber();
        stream << (quint64)req.srcAddr;
        stream << req.srcEndpoint;
        stream << req.clusterId;
        stream << req.dstAddrMode;
        if (req.dstAddrMode == deCONZ::ApsExtAddress)
        {
            stream << (quint64)req.dstExtAddr;
            stream << req.dstEndpoint;
        }
        else if (req.dstAddrMode == deCONZ::ApsGroupAddress)
        {
            stream << req.dstGroupAddr;
        }
        apsdeDataRequest(apsReq);
    }
    else
    {
        deCONZ::notifyUser(QString("Can't create Binding, unknown NWK addr for node %1")
                       .arg(req.srcAddr, 16, 16, QChar(' ')));
    }
}

/*! Device discovery handler checks for address changes and will
    send unicast ZDP IEEE_addr_req to nodes which are marked as zombie but
    have been seen by other nodes via ZDP Mgmt_Lqi_req neighbor table.
 */
void zmController::deviceDiscoverTick()
{
    if (m_nodes.empty())
    {
        return;
    }

    if (!autoFetchFFD())
    {
        return;
    }

    int fetchAfter = 20000;

    if (m_steadyTimeRef - m_lastEndDeviceAnnounce < deCONZ::TimeSeconds{2 * 60} && getParameter(deCONZ::ParamPermitJoin) > 0)
    {
        // skip while end-device search is active
        DBG_Printf(DBG_ZDP, "skip device discovery while end devices is added\n");
        return;
    }
    else if (m_fastDiscovery)
    {
        if (!m_fetchLqiTickMsCounter.isValid() || (m_fetchLqiTickMsCounter.elapsed() > 250))
        {
            fetchAfter = 1000;
        }
    }
    else if (m_otauActivity > 0)       { fetchAfter = 5000; }
    else if (isValid(m_lastNodeAdded) && deCONZ::TimeSeconds{72} < m_steadyTimeRef - m_lastNodeAdded) { fetchAfter = 15000; }
    else if (m_nodes.size() < 10) { fetchAfter = 2000; }
    else if (m_nodes.size() < 20) { fetchAfter = 2500; }
    else if (m_nodes.size() < 50) { fetchAfter = 3000; }
    else                          { fetchAfter = 3500; }

    if (m_nodes.front().data->neighbors().empty())
    {
        m_lqiIter = 0; // always fetch coordinator first
    }

    int busyCount = 0;
    int ieeeReqCount = 0;
    int lqiReqCount = 0;
    for (const deCONZ::ApsDataRequest &req : m_apsRequestQueue)
    {
        if (!req.confirmed())
        {
            busyCount++;
        }

        if (req.profileId() == ZDP_PROFILE_ID)
        {
            if      (req.clusterId() == ZDP_IEEE_ADDR_CLID) { ieeeReqCount++;  }
            else if (req.clusterId() == ZDP_MGMT_LQI_REQ_CLID) { lqiReqCount++;  }
        }
    }

    if (!m_fetchLqiTickMsCounter.isValid() ||
        (m_fetchLqiTickMsCounter.elapsed() > 60000) ||
        (busyCount <  5 && m_fetchLqiTickMsCounter.elapsed() > fetchAfter) /*||
        (busyCount == 0 && m_fetchLqiTickMsCounter.elapsed() > 2000 && m_otauActivity == 0)*/) // nothing else in queue
    {
        if (m_lqiIter >= m_nodes.size())
        {
            m_lqiIter = 0;
        }

        NodeInfo &node = m_nodes[m_lqiIter];

        if (!node.data || node.data->isZombie() || node.data->isEndDevice() ||
             node.data->isInWaitState() || !node.data->address().hasNwk())
        {
            m_lqiIter++;
        }
        else if (isValid(node.data->lastSeen()) || node.data->lastSeenByNeighbor() < 9000 ||
                 (!node.data->sourceRoutes().empty() && node.data->sourceRoutes().front().errors() < 1))
        {
            for (const deCONZ::ApsDataRequest &req : m_apsRequestQueue)
            {
                if (req.state() == deCONZ::FinishState)
                {
                    continue;
                }

                if (deCONZ::TimeSeconds{600} < node.data->lastDiscoveryTryMs(m_steadyTimeRef)) // > 10 min.
                {
                    // ok, don't wait too long
                }
                else if (req.dstAddress().hasNwk() &&
                         req.dstAddress().nwk() == node.data->address().nwk() && req.dstAddress().nwk() != 0x0000)
                {
                    m_lqiIter++;
                    return;
                }
            }

            if (lqiReqCount == 0)
            {
                m_lqiIter++;
                if (sendMgtmLqiRequest(&node))
                {
                    node.data->discoveryTimerReset(m_steadyTimeRef);
                    return;
                }
            }
        }
        else
        {
            m_lqiIter++;
        }
    }

    if (m_deviceDiscoverQueue.isEmpty())
    {
        if (m_discoverIter >= m_nodes.size())
        {
            m_discoverIter = 0;
        }

#if 0 // TODO(mpi): reactivate?!
        NodeInfo *node = &m_nodes[m_discoverIter];
        if (getParameter(deCONZ::ParamPermitJoin) > 0)
        {
            // wait
        }
        else if (node->data && !node->data->isZombie() && m_apsRequestQueue.size() < 2 && node->data->nodeDescriptor().receiverOnWhenIdle())
        {
            if (isValid(node->data->lastSeen()) && node->data->lastSeenElapsed() < (1000 * 60 * 8) &&
                !node->data->simpleDescriptors().empty()) // already queried?
            {
            }
            else if (m_steadyTimeRef - m_lastNwkAddrRequest < deCONZ::TimeSeconds{45})
            {
            }
            else if (node->data->nodeDescriptor().manufacturerCode() == VENDOR_DDEL && node->data->address().nwk() == 0x0000)
            { // coordinator ghost nodes
            }
#if 0
            else if (node->data->lastDiscoveryTryMs(m_steadyTimeRef).val == 0 ||
                     ZombieDiscoveryEmptyInterval < node->data->lastDiscoveryTryMs(m_steadyTimeRef))
            {
                if (m_nodes.size() > 80 && !node->data->sourceRoutes().empty())
                {
                    if (sendIeeeAddrRequest(node))
                    {
                        m_lastNwkAddrRequest = m_steadyTimeRef;
                    }
                }
                else if (sendNwkAddrRequest(node))
                {
                    m_lastNwkAddrRequest = m_steadyTimeRef;
                    m_discoverIter++;
                    node->data->discoveryTimerReset(m_steadyTimeRef);
                    node->data->retryIncr(deCONZ::ReqNwkAddr);
                    return;
                }
            }
#endif
        }
#endif // if 0

        m_discoverIter++;
    }
    else
    {
        NodeInfo nodeInfo; // todo cleanup
        NodeInfo *node = nullptr;
        AddressPair addrPair;

        while (!m_deviceDiscoverQueue.isEmpty())
        {
            bool noDuplicate = true;
            addrPair = m_deviceDiscoverQueue.takeFirst();

            if (!addrPair.bAddr.hasExt())
            {
                addrPair = AddressPair();
                DBG_Printf(DBG_ZDP, "remove discovery request - has no ext address (TODO)\n");
                continue;
            }

            if (!addrPair.bAddr.hasNwk())
            {
                addrPair = AddressPair();
                DBG_Printf(DBG_ZDP, "remove discovery request - has no nwk address (TODO)\n");
                continue;
            }

            auto i = m_deviceDiscoverQueue.cbegin();
            const auto end = m_deviceDiscoverQueue.cend();
            for ( ; i != end; ++i)
            {
                if (i->bAddr.hasExt() && addrPair.bAddr.ext() == i->bAddr.ext())
                {
                    if (i->bAddr.hasNwk() && addrPair.bAddr.hasNwk() &&
                            i->bAddr.nwk() != addrPair.bAddr.nwk())
                    {
                        // check address change
                    }
                    else
                    {
                        noDuplicate = false;
                        break;
                    }
                }
            }

            if (noDuplicate)
            {
                break;
            }
        }

        if (addrPair.bAddr.hasExt() && (addrPair.bAddr.ext() != 0) && addrPair.bAddr.hasNwk())
        {
            node = getNode(addrPair.bAddr, deCONZ::ExtAddress);

            if (node && node->data && !node->data->isEndDevice() && isValid(node->data->lastSeen()))
            {
                const auto dt = m_steadyTimeRef - node->data->lastSeen();
                if (dt < deCONZ::TimeSeconds{MaxZombieDiscoveryInterval.val / 4})
                {
                    // seems to be working node
                    return;
                }
            }

            if (!node)
            {
                node = getNode(addrPair.bAddr, deCONZ::NwkAddress);

                if (node)
                {
                    DBG_Printf(DBG_ZDP, "node with nwk address 0x%04X but different mac address already exist\n", addrPair.bAddr.nwk());
                    // do nothing
                    // return;
                }

                // create a new node
                if (!node)
                {
                    nodeInfo = createNode(addrPair.bAddr, addrPair.bMacCapabilities);
                    if (nodeInfo.isValid())
                    {
                        node = &nodeInfo;
                    }
                }

                //node = node ? node : &createNode(addrPair.bAddr, addrPair.bMacCapabilities);
                if (!node || !node->data)
                {
                    return;
                }
                if (node->data && (addrPair.bMacCapabilities & deCONZ::MacReceiverOnWhenIdle))
                {
                    m_lastNodeAdded = m_steadyTimeRef;
                    m_zombieDelay = qMax(m_zombieDelay, (NODE_ADDED_ZOMBIE_DELAY / TickMs)); // prevent going zombie too early
                    if (!node->data->simpleDescriptors().empty())
                    {
                        node->data->setWaitState(2);
                    }
                }
            }
        }

        if (node && node->data)
        {
            if (node->data->nodeDescriptor().isNull() || !node->data->nodeDescriptor().receiverOnWhenIdle())
            {
                return;
            }

            static_assert(MaxApsBusyRequests >= 4, "MaxApsBusyRequests value needs to be higher");
            int busyApsRequests = APS_RequestsBusyCount(m_apsRequestQueue);
            if (busyApsRequests > MaxApsBusyRequests / 2)
            {
                if ((deCONZ::master()->netState() == deCONZ::InNetwork))
                {

                    //DBG_Printf(DBG_ZDP, "device discover rotate, too busy (%d requests in que, %d)\n", busyApsRequests, m_deviceDiscoverQueue.size());
                    m_deviceDiscoverQueue.push_back(addrPair);
                    return;
                }
            }

            node->data->checkWaitState();
            if (node->data->isInWaitState())
            {
                m_deviceDiscoverQueue.push_back(addrPair);
                return;
            }

            if (node->data->isZombie())
            {
                int retryCount = node->data->recvErrors();

                if (retryCount < 1)
                {
                    retryCount = 1;
                }

                // larger delay with more retries
                deCONZ::TimeSeconds retryOffset = ZombieDiscoveryInterval * retryCount;

                // don't exceed upper bound
                if (MaxZombieDiscoveryInterval < retryOffset)
                {
                    retryOffset = MaxZombieDiscoveryInterval;
                }

                const deCONZ::TimeMs lastTry = node->data->lastDiscoveryTryMs(m_steadyTimeRef);
                if (deCONZ::TimeMs{0} < lastTry && lastTry < retryOffset)
                {
                    DBG_Printf(DBG_INFO, "discovery for zombie %s dropped, last try was %d seconds ago\n", node->data->extAddressString().c_str(), int(lastTry.val / 1000));
                    return;
                }
            }

            { // check if requests to this node are already running
                auto i = m_apsRequestQueue.cbegin();
                const auto end = m_apsRequestQueue.cend();

                for (; i != end; ++i)
                {
                    if ((i->dstAddress().hasExt() && i->dstAddress().ext() == node->data->address().ext()) ||
                        (i->dstAddress().hasNwk() && i->dstAddress().nwk() == node->data->address().nwk()))
                    {
                        // discovery for node dropped, there are already requests running
                        return;
                    }
                }
            }

            if (getParameter(deCONZ::ParamPermitJoin) > 0)
            {

            }
            else if (m_steadyTimeRef - m_lastNwkAddrRequest < deCONZ::TimeSeconds{15})
            {
              // wait
                m_deviceDiscoverQueue.push_back(addrPair);
            }
            //else if (node->data->isZombie() && sendNwkAddrRequest(node))
            else if (ZDP_SendIeeeAddrRequest(this, addrPair.bAddr))
            {
                m_lastNwkAddrRequest = m_steadyTimeRef;
                node->data->retryIncr(deCONZ::ReqNwkAddr);
                node->data->discoveryTimerReset(m_steadyTimeRef);
                return;
            }
        }
    }
}

/*!
    Reads parameters from device.
*/
void zmController::readParamTimerFired()
{
    if (networkState() != deCONZ::InNetwork)
    {
        return;
    }

    if (!deCONZ::master())
    {
        return;
    }

    deCONZ::master()->readParameter(ZM_DID_APS_CHANNEL_MASK);
    deCONZ::master()->readParameter(ZM_DID_APS_TRUST_CENTER_ADDRESS);
    deCONZ::master()->readParameter(ZM_DID_APS_USE_EXTENDED_PANID);
    deCONZ::master()->readParameter(ZM_DID_STK_CURRENT_CHANNEL);
    deCONZ::master()->readParameter(ZM_DID_STK_NWK_UPDATE_ID);
    if (deCONZ::master()->deviceFirmwareVersion() > 0x261f0500)
    {
        deCONZ::master()->readParameter(ZM_DID_DEV_WATCHDOG_TTL);
    }
    if (deCONZ::master()->deviceProtocolVersion() >= DECONZ_PROTOCOL_VERSION_1_12)
    {
        deCONZ::master()->readParameter(ZM_DID_STK_FRAME_COUNTER);
//        deCONZ::master()->readParameter(ZM_DID_STK_DEBUG);
    }

    m_readParamTimer->stop();
    m_readParamTimer->start(180 * 1000);
}

/*!
    Send a ZCL command.

    \return  > 0 a aps request id, same as in confirm
              -1 if not connected
              -2 malformed command
              -3 could not send
 */
int zmController::zclCommandRequest(const Address &address,
                                    deCONZ::ApsAddressMode addressMode,
                                     const deCONZ::SimpleDescriptor &simpleDescriptor,
                                     const deCONZ::ZclCluster     &cluster,
                                     const deCONZ::ZclCommand     &command)
{
    ApsDataRequest apsReq;
    deCONZ::ZclFrame zclFrame;
    const SimpleDescriptor *srcEndpoint;

    DBG_Printf(DBG_ZCL, "ZCL cmd-req nwk: 0x%04X, profile: 0x%04X, cluster: 0x%04X cmd: 0x%02X\n", address.nwk(), simpleDescriptor.profileId(), cluster.id(), command.id());

    if (!deCONZ::master()->connected())
    {
        return -1;
    }

    if ((addressMode == deCONZ::ApsNwkAddress) && !address.hasNwk())
    {
        DBG_Printf(DBG_ZCL, "ZCL can't send command to unknown NWK address\n");
        return -2;
    }

    if ((addressMode == deCONZ::ApsGroupAddress) && !address.hasGroup())
    {
        DBG_Printf(DBG_ZCL, "ZCL can't send command to unknown group address\n");
        return -2;
    }

    apsReq.setDstAddressMode(addressMode);
    apsReq.dstAddress() = address;
    apsReq.setProfileId(simpleDescriptor.profileId());
    apsReq.setClusterId(cluster.id());
    apsReq.setDstEndpoint(simpleDescriptor.endpoint());

    // quirks mode for ZLL nodes:
    // set correct HA profile adressing instead of ZLL profile
    if (simpleDescriptor.profileId() == ZLL_PROFILE_ID)
    {
        apsReq.setProfileId(HA_PROFILE_ID);
    }

    deCONZ::ApsTxOptions txOptions;

    // use APS ACKs only for unicast addressing and if enabled in preferences
    if (deCONZ::netEdit()->apsAcksEnabled())
    {
        if (addressMode == deCONZ::ApsNwkAddress)
        {
            if (apsReq.dstAddress().isNwkUnicast())
            {
                txOptions |= deCONZ::ApsTxAcknowledgedTransmission;
            }
        }
    }

    apsReq.setTxOptions(txOptions);
    apsReq.setRadius(0);

    srcEndpoint = getCompatibleEndpoint(simpleDescriptor);

    if (!srcEndpoint)
    {
        deCONZ::notifyUser(tr("Can't send ZCL command we don't have a compatible endpoint"));
        apsReq.setSrcEndpoint(0x00);
    }
    else
    {
        apsReq.setSrcEndpoint(srcEndpoint->endpoint());
    }

    uint8_t frameControl = 0x00;
    frameControl |= command.disableDefaultResponse() ? deCONZ::ZclFCDisableDefaultResponse : deCONZ::ZclFCEnableDefaultResponse ;
    frameControl |= command.isProfileWide() ? deCONZ::ZclFCProfileCommand : deCONZ::ZclFCClusterCommand;
    frameControl |= cluster.isServer() ? deCONZ::ZclFCDirectionClientToServer : deCONZ::ZclFCDirectionServerToClient;

    if (command.manufacturerId() != 0)
    {
        frameControl |= deCONZ::ZclFCManufacturerSpecific;
        zclFrame.setManufacturerCode(command.manufacturerId());
    }

    zclFrame.setFrameControl(frameControl);
    zclFrame.setCommandId(command.id());
    zclFrame.setSequenceNumber(genSequenceNumber());

    if (cluster.isZcl())
    {
        {
            // write ZclCommand command into ZclFrame
            QDataStream stream(&zclFrame.payload(), QIODevice::WriteOnly);
            stream.setByteOrder(QDataStream::LittleEndian);
            command.writeToStream(stream);
        }

        { // write ZclFrame into ASDU
            QDataStream stream(&apsReq.asdu(), QIODevice::WriteOnly);
            stream.setByteOrder(QDataStream::LittleEndian);
            zclFrame.writeToStream(stream);
        }
    }
    else
    {
        apsReq.setResponseClusterId(cluster.oppositeId());
        // write ZclCommand command into ASDU
        QDataStream stream(&apsReq.asdu(), QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        command.writeToStream(stream);
    }

    if (apsdeDataRequest(apsReq) == deCONZ::Success)
    {
        return apsReq.id();
    }

    return -3;
}

void zmController::zclReportAttributesIndication(NodeInfo *node, const deCONZ::ApsDataIndication &ind, const deCONZ::ZclFrame &zclFrame, NodeEvent &event)
{
    if (!node || !node->data)
    {
        return;
    }

    for (auto &bnd : node->data->bindingTable())
    {
        if (bnd.clusterId() != ind.clusterId())
        {
            continue;
        }

        if (bnd.srcEndpoint() != ind.srcEndpoint())
        {
            continue;
        }

        if (bnd.dstAddressMode() == deCONZ::ApsExtAddress && bnd.dstEndpoint() == ind.dstEndpoint())
        {
            bnd.setConfirmedTimeRef(m_steadyTimeRef);
            break;
        }
        else if (bnd.dstAddressMode() == deCONZ::ApsGroupAddress && ind.dstAddress().hasGroup() && ind.dstAddress().group() == bnd.dstAddress().group())
        {
            bnd.setConfirmedTimeRef(m_steadyTimeRef);
            break;
        }
    }

    deCONZ::ZclClusterSide side =
            // check  if this is always teh reverse case ?
            (zclFrame.frameControl() & deCONZ::ZclFCDirectionServerToClient) ? deCONZ::ServerCluster : deCONZ::ClientCluster;
    deCONZ::ZclCluster *cluster = node->data->getCluster(ind.srcEndpoint(), ind.clusterId(), side);

    deCONZ::SimpleDescriptor *sd = node->data->getSimpleDescriptor(ind.srcEndpoint());

    m_deviceWatchdogOk |= DEVICE_RX_NETWORK_OK;

    // Xiaomi FFD don't always respond to ZDP so mark them as available here
    if (zclFrame.manufacturerCode() == VENDOR_115F)
    {
        node->data->resetRecErrors();
        node->data->discoveryTimerReset(m_steadyTimeRef); // prevent discovery
    }

    // try to append unknown cluster
    if (!cluster && node && node->data && side == deCONZ::ServerCluster)
    {
        ZclDataBase *db = deCONZ::zclDataBase();

        if (!sd)
        {
            deCONZ::SimpleDescriptor s;
            s.setEndpoint(ind.srcEndpoint());
            s.setProfileId(ind.profileId());
            s.setDeviceId(0xffff); // unknown
            node->data->simpleDescriptors().push_back(s);
            std::vector<uint8_t> eps = node->data->endpoints();
            if (std::find(eps.begin(), eps.end(), ind.srcEndpoint()) == eps.end())
            {
                eps.push_back(ind.srcEndpoint());
                node->data->setActiveEndpoints(eps);
            }
            sd = node->data->getSimpleDescriptor(ind.srcEndpoint());
        }

        if (sd && db)
        {
            const deCONZ::ZclCluster cl = db->inCluster(ind.profileId(), ind.clusterId(), node->data->nodeDescriptor().manufacturerCode());
            if (cl.isValid())
            {
                sd->inClusters().push_back(cl);
                cluster = node->data->getCluster(ind.srcEndpoint(), ind.clusterId(), side);

                if (node->g)
                {
                    node->g->updated(deCONZ::ReqSimpleDescriptor);
                }
                NodeEvent event(NodeEvent::UpdatedSimpleDescriptor, node->data, sd->endpoint());
                emit nodeEvent(event);
                queueSaveNodesState();
            }
        }
    }

    if ((node->data->address().ext() & macPrefixMask) == jennicMacPrefix &&
        (zclFrame.manufacturerCode() == 0 || ind.clusterId() != 0x0000))
    {
        // skip, query xiaomi devices only after special reports for basic cluster are received
    }
    else if (getParameter(deCONZ::ParamPermitJoin) > 0)
    {

    }
    else if (!sd || sd->deviceId() == 0xffff)
    {
        if (node->data->retryCount(deCONZ::ReqSimpleDescriptor) < 2)
        {
            if (sendSimpleDescriptorRequest(node, ind.srcEndpoint()))
            {
                node->data->retryIncr(deCONZ::ReqSimpleDescriptor);
            }
        }
    }
    else if (node->data->nodeDescriptor().isNull() && node->data->retryCount(deCONZ::ReqNodeDescriptor) < 2)
    {
        if (sendNodeDescriptorRequest(node))
        {
            node->data->retryIncr(deCONZ::ReqNodeDescriptor);
        }
    }

    if (cluster)
    {
        QDataStream stream(zclFrame.payload());
        stream.setByteOrder(QDataStream::LittleEndian);

        uint16_t attrId;
        uint8_t dataType;

        while (!stream.atEnd())
        {
            stream >> attrId;

            if (stream.atEnd())
            {
                return;
            }

            stream >> dataType;

            if (stream.atEnd())
            {
                return;
            }

            bool wasRead = false;

            auto i = cluster->attributes().begin();
            auto end = cluster->attributes().end();

            for (; i != end; ++i)
            {
                if (i->id() == attrId && i->dataType() != dataType && !i->isManufacturerSpecific() && zclFrame.manufacturerCode_t() == 0x0000_mfcode)
                {
                    if (deCONZ::zclDataBase()->knownDataType(dataType))
                    {
                        // convert
                        DBG_Printf(DBG_ZCL, "ZCL cluster 0x%04X attribute 0x%04X, update to new data type 0x%02X -> 0x%02X\n", cluster->id(), i->id(), i->dataType(), dataType);
                        i->setDataType(dataType);
                    }
                }

                if (i->id() == attrId && i->dataType() == dataType)
                {
                    if (i->isManufacturerSpecific() && i->manufacturerCode() != zclFrame.manufacturerCode())
                    {
                        continue;
                    }

                    if (!i->readFromStream(stream))
                    {
                        return;
                    }

                    wasRead = true;

                    i->setLastRead(m_steadyTimeRef.ref);
                    event.addAttributeId(attrId);
                    break;
                }
            }

            if (!wasRead)
            { // strip from stream for known datatypes
                deCONZ::ZclAttribute a(attrId, dataType, QString(), deCONZ::ZclReadWrite, true);

                if (!a.readFromStream(stream))
                {
                    return;
                }
            }
        }

        DBG_Assert(node && node->data && cluster);
        if (node && node->data && cluster)
        {
            deCONZ::clusterInfo()->refreshNodeAttributes(node->data, ind.srcEndpoint(), cluster);
        }
    }
}

bool zmController::sendMgmtLeaveRequest(deCONZ::zmNode *node, bool removeChildren, bool rejoin)
{
    if (!node->address().hasExt())
    {
        DBG_Printf(DBG_ZDP, "CTRL can't send mgmt leave request with unknown EXT address");
        return false;
    }

    ApsDataRequest req;
    quint64 extAddr = 0;
    uint8_t options = 0x00; // includes first 6 reserved bits

    if (removeChildren)
    {
        options |= 0x40;
    }

    if (rejoin)
    {
        options |= 0x80;
    }

    req.setDstAddressMode(deCONZ::ApsExtAddress);

    if (node->isEndDevice())
    {
        if (!node->parentAddress().hasNwk())
        {
            return false;
        }
        else
        {
            req.dstAddress() = node->parentAddress();
        }
        extAddr = node->address().ext();
    }
    else // if (!node->isEndDevice())
    {
        // coordinator or router or a enddevice with unknown parent
        req.dstAddress() = node->address();
        extAddr = 0; // remove itself
    }

    req.setProfileId(ZDP_PROFILE_ID);
    req.setClusterId(ZDP_MGMT_LEAVE_REQ_CLID);
    req.setDstEndpoint(ZDO_ENDPOINT);
    req.setSrcEndpoint(ZDO_ENDPOINT);
    req.setTxOptions(deCONZ::ApsTxAcknowledgedTransmission);
    req.setRadius(0);

    QDataStream stream(&req.asdu(), QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    const uint8_t seqNo = genSequenceNumber();
    stream << seqNo;
    stream << extAddr;
    stream << options;

    DBG_Printf(DBG_ZDP, "Mgmt_Leave_req zdpSeq: %u to %s\n", seqNo, node->extAddressString().c_str());

    if (apsdeDataRequest(req) == deCONZ::Success)
    {
        return true;
    }

    return false;
}

bool zmController::sendNwkLeaveRequest(deCONZ::zmNode *node, bool removeChildren, bool rejoin)
{
    if (!node || !m_master)
    {
        return false;
    }

    deCONZ::NwkLeaveRequest req;
    req.flags = 0;
    req.dstAddress = node->address().nwk();

    if (rejoin)
    {
        req.flags |= 0x1;
    }

    if (removeChildren)
    {
        req.flags |= 0x2;
    }

    return m_master->nwkLeaveRequest(req) == 0;
}

bool zmController::sendForceChildRejoin(deCONZ::zmNode *node)
{
    if (!node || !m_master)
    {
        return false;
    }

    DBG_Printf(DBG_INFO, "force rejoin of node %s / 0x%04X\n", node->extAddressString().c_str(), node->address().nwk());
    return m_master->forceRejoinChildNode(node->address()) == 0;
}

const deCONZ::SimpleDescriptor *zmController::getCompatibleEndpoint(const deCONZ::SimpleDescriptor &other)
{
    // search compatible endpoint (with proper profile id)
    NodeInfo &srcNode = m_nodes.front(); // self
    // TODO first node isn't always own node
    for (auto &sd : srcNode.data->simpleDescriptors())
    {
        if (sd.profileId() == other.profileId())
        {
            return &sd;
        }
        // the ZLL profile shall be compatible with HA profile
        else if (other.profileId() == ZLL_PROFILE_ID && sd.profileId() == HA_PROFILE_ID)
        {
            return &sd;
        }
    }

    return nullptr;
}

void zmController::checkAddressChange(const Address &address, NodeInfo *node)
{
    if (address.hasExt() && address.hasNwk())
    {
        if (!node)
        {
            node = getNode(address, deCONZ::ExtAddress);
        }

        if (node && node->data && node->g)
        {
            if (node->data->address().nwk() != address.nwk())
            {
                DBG_Printf(DBG_INFO, "%s 0x%04X nwk changed to 0x%04X\n",
                       node->data->extAddressString().c_str(), node->data->address().nwk(), address.nwk());
                node->data->setAddress(address);
                node->g->updateParameters(node->data);
                node->g->requestUpdate();
                NodeEvent e(NodeEvent::UpdatedNodeAddress, node->data);
                emit nodeEvent(e);
                visualizeNodeChanged(node, deCONZ::IndicateDataUpdate);
                queueSaveNodesState();
            }
        }
        else
        {
            node = getNode(address, deCONZ::NwkAddress);

            if (node && node->data && !node->data->address().hasExt())
            {
                node->data->setAddress(address);
                node->data->setFetched(deCONZ::ReqIeeeAddr, true);
                visualizeNodeChanged(node, deCONZ::IndicateDataUpdate);
                queueSaveNodesState();
                NodeEvent e(NodeEvent::UpdatedNodeAddress, node->data);
                emit nodeEvent(e);
            }
        }

        if (node && node->data && !node->data->isZombie() && node->data->address().hasExt() && !node->g->isVisible())
        {
            wakeNode(node);
        }
    }
}

void zmController::setDeviceState(deCONZ::State state)
{
    if (m_devState != state)
    {
        m_devState = state;

        if (m_master->connected())
        {
            getNetworkConfig();
        }        
    }
}

void zmController::visualizeNodeIndication(NodeInfo *node, deCONZ::Indication indication)
{
    if (node && node->g && indication != deCONZ::IndicateNone)
    {
        node->g->indicate(indication);
    }
}

void zmController::visualizeNodeChanged(NodeInfo *node, deCONZ::Indication indication)
{
    if (node && node->data && node->g)
    {
        zmNetEvent event;
        event.setType(deCONZ::NodeDataChanged);
        event.setNode(node->data);
        emit notify(event);

        deCONZ::nodeModel()->updateNode(*node);
    }
}

constexpr char lookup[] = { '0', '1', '2','3','4','5','6','7','8','9','a','b','c','d','e','f' };

void generateUniqueId2(uint64_t extAddress, char *buf, unsigned buflen)
{
    if (buflen < 23 + 1)
    {
        if (buflen > 0) { *buf = '\0'; }
        return;
    }

    // 00:21:2e:ff:ff:00:12:34
    for (int i = 0; i < 8; i++)
    {
        char hex = (extAddress >> 56) & 0xFF;
        *buf++ = lookup[(hex & 0xf0) >> 4];
        *buf++ = lookup[hex & 0xf];
        extAddress <<= 8;
        if (i < 7) { *buf++ = ':'; }
    }

    *buf = '\0';
}

void zmController::restoreNodesState()
{
    m_saveNodesChanges = 0;
}

void zmController::unregisterGNode(zmgNode *gnode)
{
    if (!gnode)
    {
        return;
    }

    for (uint i = 0; i < m_nodes.size(); i++)
    {
        NodeInfo &node = m_nodes[i];
        if (node.g && (node.g == gnode))
        {
            node.pos = gnode->pos();
            node.g = 0;
        }
    }
}

void zmController::toggleLqiView(bool show)
{
    m_showLqi = show;
}

void zmController::toggleNeighborLinks(bool show)
{
    m_showNeighborLinks = show;
}

void zmController::deviceState(int state)
{
    switch (state)
    {
    case deCONZ::BusyState:
        m_waitForQueueEmpty = true;
        break;

    default:
        break;
    }
}

/*! Try to fast probe all infos of a node
 */
void zmController::fastPrope(uint64_t ext, uint16_t nwk, uint8_t macCapabilities)
{
    if (macCapabilities & deCONZ::MacReceiverOnWhenIdle)
    {
        if (deCONZ::appArgumentNumeric(QLatin1String("--dev-test-managed"), 0) > 0)
        {
            return;
        }
    }
    else
    {
        return; // end device not supported here
    }

    for (FastDiscover &fd : m_fastDiscover)
    {
        if (fd.addr.ext() == ext)
        {
            fd.addr.setNwk(nwk);
            fd.errors = 0;
            return;
        }
    }

    FastDiscover fd;
    fd.errors = 0;
    fd.busy = 0;
    fd.done = 0;
    fd.tAnnounce = m_steadyTimeRef;
    fd.addr.setExt(ext);
    fd.addr.setNwk(nwk);
    fd.clusters[0] = ZDP_NODE_DESCRIPTOR_CLID;
    fd.clusters[1] = ZDP_ACTIVE_ENDPOINTS_CLID;
    fd.clusters[2] = ZDP_SIMPLE_DESCRIPTOR_CLID;
    fd.clusterCount = 3;
    m_fastDiscover.push_back(fd);
    DBG_Printf(DBG_ZDP, "ZDP add fast discover for " FMT_MAC "\n", FMT_MAC_CAST(ext));
}

void zmController::wakeNode(NodeInfo *node)
{
    if (node && node->data && node->g)
    {
        node->data->setState(deCONZ::IdleState);
        node->data->setZombieInternal(false);
        node->data->touch(m_steadyTimeRef);
        node->data->setFetched(deCONZ::ReqMgmtLqi, false);
        node->g->show();
        node->g->requestUpdate();
        NodeEvent event(NodeEvent::NodeAdded, node->data);
        emit nodeEvent(event);
    }
}

void zmController::setAutoFetchingFFD(bool enabled)
{
    m_autoFetchFFD = enabled;
    setAutoFetching();
}

void zmController::setAutoFetchingRFD(bool enabled)
{
    m_autoFetchRFD = enabled;
    setAutoFetching();
}

void zmController::setAutoFetching()
{
    bool enabled = m_autoFetchFFD || m_autoFetchRFD;

    if (enabled != m_autoFetch)
    {
        m_autoFetch = enabled;
        QList<deCONZ::RequestId> items;
        items.append(deCONZ::ReqNodeDescriptor);
        items.append(deCONZ::ReqActiveEndpoints);
        items.append(deCONZ::ReqSimpleDescriptor);
        items.append(deCONZ::ReqMgmtLqi);

        foreach (NodeInfo node, m_nodes)
        {
            foreach (deCONZ::RequestId item, items)
            {
                node.data->setFetchItemEnabled(item, m_autoFetch);
            }
        }
    }

    // call mainwindow
    QMetaObject::invokeMethod(parent(), "setAutoFetching");
}

void zmController::sendNext()
{
    if (sendNextApsdeDataRequest())
    {
        return;
    }

    sendNextLater();
}

void zmController::sendNextLater()
{
    if (!m_sendNextTimer->isActive() && !m_apsRequestQueue.empty())
    {
        m_sendNextTimer->start();
    }
}

/*! Handle cleanup stuff before the app quits.
 *  The deconstructor might be called after dependent objects where destroyed
 *  so this is a better place to handle shutdown.
 */
void zmController::appAboutToQuit()
{
    m_saveNodesTimer->stop();
    killTimer(m_timer);
    killTimer(m_timeoutTimer);

    for (auto &node : m_nodes)
    {
        // force save on quit
        if (node.data && node.g)
            node.g->setNeedSaveToDatabase(true);
    }

    queueSaveNodesState();
    m_otauActivity = 0;
    saveNodesState();

    std::vector<NodeInfo>::iterator i = m_nodes.begin();
    std::vector<NodeInfo>::iterator end = m_nodes.end();

    for (;i != end; ++i)
    {
        unregisterGNode(i->g);
    }
}

void zmController::setSourceRouteMinLqi(int sourceRouteMinLqi)
{
    if (m_sourceRouteMinLqi == sourceRouteMinLqi)
        return;

    DBG_Printf(DBG_INFO, "Set source route min LQI: %d -> %d\n", m_sourceRouteMinLqi, sourceRouteMinLqi);
    m_sourceRouteMinLqi = sourceRouteMinLqi;
    emit sourceRouteMinLqiChanged(m_sourceRouteMinLqi);
}

void zmController::setSourceRouteMaxHops(int sourceRouteMmaxHops)
{
    if (m_sourceRouteMaxHops == sourceRouteMmaxHops)
        return;

    DBG_Printf(DBG_INFO, "Set source route max Hops: %d -> %d\n", m_sourceRouteMaxHops, sourceRouteMmaxHops);
    m_sourceRouteMaxHops = sourceRouteMmaxHops;
    emit sourceRouteMaxHopsChanged(m_sourceRouteMaxHops);
}

void zmController::setSourceRoutingEnabled(bool sourceRoutingEnabled)
{
    if (m_sourceRoutingEnabled == sourceRoutingEnabled)
        return;

    DBG_Printf(DBG_INFO, "Set source routing enabled: %d -> %d\n", m_sourceRoutingEnabled, sourceRoutingEnabled);
    m_sourceRoutingEnabled = sourceRoutingEnabled;
    emit sourceRoutingEnabledChanged(m_sourceRoutingEnabled);
}

void zmController::setFastNeighborDiscovery(bool fastDiscovery)
{
    if (m_fastDiscovery == fastDiscovery)
        return;

    DBG_Printf(DBG_INFO, "Set fast discovery enabled: %d -> %d\n", m_fastDiscovery, fastDiscovery);
    m_fastDiscovery = fastDiscovery;
}

void zmController::setMinLqiDisplay(int minLqi)
{
    if (m_minLqiDisplay == minLqi)
        return;

    DBG_Printf(DBG_INFO, "Set min LQI display: %d -> %d\n", m_minLqiDisplay, minLqi);
    m_minLqiDisplay = minLqi;
}

void zmController::addDeviceDiscover(const AddressPair &a)
{
    if (!a.bAddr.hasExt() || !a.bAddr.hasNwk())
    {
        DBG_Printf(DBG_ZDP, "don't put incomplete discover address in queue\n");
        return;
    }

    // check for duplicates (ext(1) + nwk address(N))
    auto i = std::find_if(m_deviceDiscoverQueue.begin(), m_deviceDiscoverQueue.end(), [&](const AddressPair &x)
    {
        return x.bAddr.ext() == a.bAddr.ext() &&
               x.bAddr.nwk() == a.bAddr.nwk();
    });

    if (i == m_deviceDiscoverQueue.end())
    {
        DBG_Printf(DBG_ZDP, "ZDP add " FMT_MAC ", nwk: 0x%04X to discover queue\n", FMT_MAC_CAST(a.bAddr.ext()), a.bAddr.nwk());
        m_deviceDiscoverQueue.push_back(a);
    }
}

void zmController::addNodePlugin(NodeInterface *plugin)
{
    if (plugin)
    {
        if (0 == m_restPlugin && strstr(plugin->name(), "REST"))
        {
            m_restPlugin = dynamic_cast<QObject*>(plugin);
            connect(m_restPlugin, SIGNAL(nodeUpdated(quint64,QString,QString)),
                    this, SLOT(onRestNodeUpdated(quint64,QString,QString)));
        }
    }
}
