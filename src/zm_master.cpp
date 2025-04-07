/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMetaType>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimerEvent>
#include <QTimer>
#include <QSettings>
#include <QtEndian>
#include <stdio.h>
#include <sys/time.h>
#if defined(Q_OS_LINUX) && defined(USE_LIBCAP)
  #include <sys/capability.h>
#endif
#include "deconz/dbg_trace.h"
#include "deconz/device_enumerator.h"
#include "deconz/buffer_helper.h"
#include "deconz/zdp_descriptors.h"
#include "deconz/util.h"
#include "deconz/green_power_controller.h"
#include "deconz/timeref.h"
#include "zm_controller.h"
#include "zm_global.h"
#include "zm_master.h"
#include "zm_master_com.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x)            (sizeof(x) / sizeof((x)[0]))
#endif

#define ZM_MAX_COMMAND_LENGTH 255

// Firmware version related (32-bit field)
#define FW_PLATFORM_MASK          0x0000FF00UL
#define FW_PLATFORM_DERFUSB23E0X  0x00000300UL
#define FW_PLATFORM_AVR           0x00000500UL
#define FW_PLATFORM_R21           0x00000700UL

enum MasterConfig
{
    MAX_QUEUE_ITEMS = 32,
    MAX_APS_QUEUE_ITEMS = 16
};

enum QueItemState
{
    QUEUE_FREE,
    QUEUE_PENDING,
    QUEUE_RESERVED
};

static const int MaxUnconfirmed = 2;
static const int MaxSendRetry = 1;
static const int TimeoutDelay = 500;
static const int StatusQueryDelay = 500;
static const int SendDelay = 20;
static const int MaxCommandFails = 10;
static int needStatus = 1;
static int64_t tSend;
static int64_t tStatus;
#ifdef Q_OS_LINUX
// a file in which the firmware version will be written
// the info will be used by the update script
static const char *firmwareVersionFile = "/var/tmp/deconz-firmware-version";
#endif // Q_OS_LINUX

enum QueueItemState
{
    QITEM_STATE_INIT = 0,
    QITEM_STATE_WAIT_SEND = 1,
    QITEM_STATE_WAIT_CONFIRM = 2
};

typedef struct QueueItem
{
    struct zm_command cmd;
    int64_t tref_tx;
    QueueItemState state;
    int retries;
} QueueItem_t;

struct zm_master
{
    unsigned char proto_id;
    unsigned char RxBuffer[ZM_MAX_COMMAND_LENGTH];
    unsigned char TxBuffer[ZM_MAX_COMMAND_LENGTH];
    struct zm_command RxCmd;
    struct zm_command TxCmd;
    unsigned char _seq;
    int cmd_fails;

    uint16_t q_aps[MAX_APS_QUEUE_ITEMS];
    unsigned q_aps_rp;
    unsigned q_aps_wp;

    unsigned q_item_sp; /* send pointer */
    unsigned q_item_wp; /* write pointer */
    unsigned q_items_wait_send;
    unsigned q_items_wait_confirm;
    QueueItem_t q_items[MAX_QUEUE_ITEMS];
    zmMaster *instance;
    uint8_t status0;
    uint8_t status1;
    ZLL_NetState_t zllState;
    IPAN_State_t ipanState;
};

static SerialCom *m_serialCom = nullptr;
static zmMaster::MasterState m_state;
static struct zm_master Master;

static const char *stackStatus[] =
{
    "APP_SUCCESS",     // 0x00
    "APP_FAILURE",     // 0x01
    "APP_BUSY",        // 0x02
    "APP_TIMEOUT",     // 0x03
    "APP_UNSUPPORTED", // 0x04
    "APP_ERROR",       // 0x05
    "APP_ENONET",      // 0x06
    "APP_EINVAL",      // 0x07
    "APP_ELEN",        // 0x08
    "APP_EOFFSET",     // 0x09
    "APP_ELEAK",       // 0x0A
    "APP_OVFLW",       // 0x0B
    NULL
};

static const char *cmdString[] =
{
    "CMD_ACK",
    "CMD_INVALID",
    "CMD_GENERAL",
    "CMD_APS_DATA_REQ",
    "CMD_APS_DATA_CONFIRM",
    "CMD_APS_DATA_INDICATION",
    "CMD_NPDU_INDICATION",
    "CMD_STATUS",
    "CMD_CHANGE_NET_STATE",
    "CMD_ZDO_NET_CONFIRM",
    "CMD_READ_PARAM",
    "CMD_WRITE_PARAM",
    "CMD_RESEND_LAST_CMD",
    "CMD_VERSION",
    "CMD_STATUS_CHANGE",
    "CMD_RESERVED8",
    "CMD_RESERVED9",
    "CMD_FEATURE",
    "CMD_APS_DATA_REQ_2",
    "CMD_START_INTERPAN_MODE",
    "CMD_SEND_INTERPAN_REQ",
    "CMD_INTERPAN_INDICATION",
    "CMD_INTERPAN_CONFIRM",
    "CMD_APS_DATA_INDICATION2",
    "CMD_READ_REGISTER",
    "CMD_GP_DATA_INDICATION",
    "CMD_LINK_ADDRESS",
    "CMD_PHY_FRAME",
    "CMD_MAC_POLL",
    "CMD_UPDATE_NEIGHBOR",
    "CMD_REBOOT",
    "CMD_BEACON",
    "CMD_FACTORY_RESET",
    "CMD_NWK_LEAVE_REQ",
    "CMD_DEBUG_LOG",
};

const char *cmdToString(uint cmd)
{
    if (cmd <= ZM_CMD_DEBUG_LOG)
    {
        return cmdString[cmd];
    }
    else
    {
        DBG_Printf(DBG_PROT, "[Master] unknown command 0x%02X\n", cmd);
        return "CMD_UNKNOWN";
    }
}

static QueueItem *EnqueueStatus();

static const char *appStatusToString(uint8_t status)
{
    if (status < ARRAY_SIZE(stackStatus))
    {
        return stackStatus[status];
    }

    return "UNKNOWN";
}

static void QItem_Init(QueueItem_t *item)
{
    item->state = QITEM_STATE_INIT;
    item->cmd.cmd = ZM_CMD_INVALID;
    item->cmd.buffer.len = 0;
    item->tref_tx = 0;
    item->retries = 0;
}

static uint8_t QItem_NextSeq()
{
    unsigned i;
    Master._seq++;

    if ((Master.q_items_wait_send + Master.q_items_wait_confirm) == 0)
        return Master._seq;

again:
    for (i = 0; i < MAX_QUEUE_ITEMS; i++)
    {
        if (Master.q_items[i].state == QITEM_STATE_INIT)
            continue;

        if (Master.q_items[i].cmd.seq == Master._seq)
        {
            Master._seq++;
            goto again;
        }
    }

    return Master._seq;
}

static QueueItem_t *QItem_Alloc()
{
    unsigned i;
    QueueItem_t *item = nullptr;

    for (i = 0; i < MAX_QUEUE_ITEMS; i++)
    {
        if (Master.q_items[Master.q_item_wp % MAX_QUEUE_ITEMS].state == QITEM_STATE_INIT)
        {
            item = &Master.q_items[Master.q_item_wp % MAX_QUEUE_ITEMS];
            QItem_Init(item);
            item->cmd.seq = QItem_NextSeq();
            break;
        }

        Master.q_item_wp++;
    }

    return item;
}

static void QItem_Free(QueueItem_t *item)
{
    DBG_Assert(item->state != QITEM_STATE_INIT);

    Q_ASSERT(item >= &Master.q_items[0]);
    Q_ASSERT(item <= &Master.q_items[MAX_QUEUE_ITEMS - 1]);

    if (item->state == QITEM_STATE_WAIT_SEND)
    {
        Q_ASSERT(Master.q_items_wait_send != 0);
        Master.q_items_wait_send--;
    }
    else if (item->state == QITEM_STATE_WAIT_CONFIRM)
    {
        Q_ASSERT(Master.q_items_wait_confirm != 0);
        Master.q_items_wait_confirm--;
    }
    else
    {
        DBG_Assert(0 && "unexpected queue item state");
    }
    item->cmd.cmd = ZM_CMD_INVALID;
    item->state = QITEM_STATE_INIT;
}

static int QItem_Enqueue(QueueItem_t *item)
{
    QueueItem_t *pos;

    pos = &Master.q_items[Master.q_item_wp % MAX_QUEUE_ITEMS];

    if (pos == item && pos->state == QITEM_STATE_INIT)
    {
        item->state = QITEM_STATE_WAIT_SEND;
        Q_ASSERT(Master.q_items_wait_send < MAX_QUEUE_ITEMS);
        Master.q_items_wait_send++;
        Master.q_item_wp++;

        if (m_state == zmMaster::MASTER_IDLE)
        {
            if (Master.q_items_wait_confirm < MaxUnconfirmed)
            {
                Master.instance->startTaskTimer(zmMaster::ACTION_PROCESS, 0, __LINE__);
            }
        }

        return 1;
    }
    else
    {
        Q_ASSERT(0 && "unexpected enqueue item");
    }

    DBG_Assert(pos == item);
    DBG_Assert(pos->state == QITEM_STATE_INIT);

    return 0;
}

static int QItems_Empty()
{
    return (Master.q_items_wait_send || Master.q_items_wait_confirm) ? 0 : 1;
}

static int QItem_Send(QueueItem_t *item)
{
    int ret = -100;
    Q_ASSERT(item->state == QITEM_STATE_WAIT_SEND);

    if (m_serialCom && item->state == QITEM_STATE_WAIT_SEND)
    {
        DBG_Printf(DBG_PROT, "[Master] send cmd seq: %u, %s\n", item->cmd.seq, cmdToString(item->cmd.cmd));
        ret = m_serialCom->send(&item->cmd);
        if (ret == 0)
        {
            if (&Master.q_items[Master.q_item_sp % MAX_QUEUE_ITEMS] == item)
                Master.q_item_sp++;

            Q_ASSERT(Master.q_items_wait_send != 0);
            Master.q_items_wait_send--;
            Q_ASSERT(Master.q_items_wait_confirm < MAX_QUEUE_ITEMS);
            Master.q_items_wait_confirm++;
            item->state = QITEM_STATE_WAIT_CONFIRM;
            item->tref_tx = deCONZ::steadyTimeRef().ref;
            tSend = item->tref_tx;
        }
        else
        {
            DBG_Printf(DBG_PROT, "[Master] send cmd seq: %u, %s failed, ret: %d\n", item->cmd.seq, cmdToString(item->cmd.cmd), ret);
        }
    }

    return 0;
}

static QueueItem_t *QItem_NextToSend()
{
    unsigned i;
    unsigned j;
    QueueItem_t *item;

    item = nullptr;
    i = Master.q_item_sp % MAX_QUEUE_ITEMS;

    for (j = 0; j < MAX_QUEUE_ITEMS; j++)
    {
        if (Master.q_items[i].state == QITEM_STATE_WAIT_SEND)
        {
            item = &Master.q_items[i];
            break;
        }

        i = (i + 1) % MAX_QUEUE_ITEMS;
    }

    return item;
}

static int QItem_Confirm(const struct zm_command *cmd)
{
    unsigned i;
    QueueItem_t *item;

    for (i = 0; i < MAX_QUEUE_ITEMS; i++)
    {
        if (Master.q_items[i].state != QITEM_STATE_WAIT_CONFIRM)
            continue;

        item = &Master.q_items[i];

        if (item->cmd.cmd == cmd->cmd && item->cmd.seq == cmd->seq)
        {
            if (DBG_IsEnabled(DBG_PROT))
            {
                int64_t dt = (deCONZ::steadyTimeRef().ref - item->tref_tx);
                DBG_Printf(DBG_PROT, "[Master] response cmd seq: %u, %s, dt %d ms\n", cmd->seq, cmdToString(cmd->cmd), int(dt));
            }
            QItem_Free(item);

            Master.cmd_fails = 0;

            /*if (Master.q_items_wait_confirm < MaxUnconfirmed)
            {
                Master.instance->startTaskTimer(zmMaster::ACTION_PROCESS, 0, __LINE__);
            }*/

            return 1;
        }
    }

    return 0;
}

static int QAPS_Empty()
{
    return Master.q_aps_wp == Master.q_aps_rp;
}

static int QAPS_Full()
{
    return (Master.q_aps_wp + 1) % MAX_APS_QUEUE_ITEMS == Master.q_aps_rp;
}

static int QAPS_Push(unsigned id)
{
    if (QAPS_Full())
        return 0; // queue full

    Q_ASSERT(Master.q_aps_wp < MAX_APS_QUEUE_ITEMS);

    Master.q_aps[Master.q_aps_wp] = (uint16_t)id;
    Master.q_aps_wp = (Master.q_aps_wp + 1) % MAX_APS_QUEUE_ITEMS;

    return 1;
}

static unsigned QAPS_Pop()
{
    unsigned result;

    if (QAPS_Empty())
        return UINT16_MAX;

    Q_ASSERT(QAPS_Empty() == 0);
    Q_ASSERT(Master.q_aps_rp < MAX_APS_QUEUE_ITEMS);
    result = Master.q_aps[Master.q_aps_rp];
    Master.q_aps_rp = (Master.q_aps_rp + 1) % MAX_APS_QUEUE_ITEMS;
    return result;
}

static void QAPS_Test()
{
    unsigned i;
    Master.q_aps_rp = 0;
    Master.q_aps_wp = 0;

    Q_ASSERT(QAPS_Empty() == 1);
    Q_ASSERT(QAPS_Full() == 0);

    Q_ASSERT(QAPS_Push(10) == 1);

    Q_ASSERT(QAPS_Empty() == 0);
    Q_ASSERT(QAPS_Full() == 0);

    Q_ASSERT(QAPS_Pop() == 10);

    Q_ASSERT(QAPS_Empty() == 1);
    Q_ASSERT(QAPS_Full() == 0);

    for (i = 0; i < MAX_APS_QUEUE_ITEMS - 1; i++)
    {
        Q_ASSERT(QAPS_Push(20 + i) == 1);
        Q_ASSERT(QAPS_Empty() == 0);
    }

    Q_ASSERT(QAPS_Full() == 1);

    Q_ASSERT(QAPS_Push(30) == 0); // NOP

    for (i = 0; i < MAX_APS_QUEUE_ITEMS - 1; i++)
    {
        Q_ASSERT(QAPS_Pop() >= 20);
    }

    Q_ASSERT(QAPS_Full() == 0);
    Q_ASSERT(QAPS_Empty() == 1);

    Q_ASSERT(QAPS_Pop() == UINT16_MAX); // NOP
}

namespace deCONZ {
zmMaster *master()
{
    return Master.instance;
}

}

zmMaster::zmMaster(QObject *parent) :
    deCONZ::TouchlinkController(parent)
{
    qRegisterMetaType<zmNetEvent>("zmNetEvent");
    qRegisterMetaType<deCONZ::ApsDataRequest>("deCONZ::ApsDataRequest");
    qRegisterMetaType<deCONZ::ApsDataConfirm>("deCONZ::ApsDataConfirm");
    qRegisterMetaType<deCONZ::ApsDataIndication>("deCONZ::ApsDataIndication");
    qRegisterMetaType<deCONZ::GpDataIndication>("deCONZ::GpDataIndication");

    new deCONZ::GreenPowerController(this);

    QAPS_Test();

    m_state = MASTER_OFF;
    m_devProtocolVersion = DECONZ_PROTOCOL_VERSION_MIN; // default
    m_devFirmwareVersion = 0; // unknown
    m_maxNodes = APP_MIN_NODES;
    m_readParamCount = 0;
    m_serialCom = nullptr;

    initSerialCom();

    Master.status0 = 0x00;
    Master.status1 = 0x00;
    Master.zllState = ZLL_NET_NOT_CONNECTED;
    Master.instance = this;



    m_taskTimer = new QTimer(this);
    m_taskTimer->setInterval(TimeoutDelay);
    m_taskTimer->setSingleShot(true);
    connect(m_taskTimer, SIGNAL(timeout()),
            this, SLOT(taskTimerFired()));

    // read registers for debug
//    if (deCONZ::appArgumentNumeric("--read-reg-interval", 0) > 0)
//    {
//        QTimer::singleShot(1000, this, SLOT(readRegisterTimerFired()));
//    }

    // cleanup handler
    connect(QCoreApplication::instance(), SIGNAL(aboutToQuit()),
            this, SLOT(appAboutToQuit()));

    m_timeoutTimer = startTimer(100);
}

zmMaster::~zmMaster()
{
    Q_ASSERT(m_serialCom);
    if (m_serialCom)
    {
        comExit();
        m_serialCom->deleteLater();
        m_serialCom = nullptr;
        m_serialPort.clear();
    }
    Master.instance = nullptr;
}

int zmMaster::openSerial(const QString &port, int baudrate)
{
    if (m_state != MASTER_OFF)
    {
        return -4;
    }

    if (!m_serialCom)
    {
        return -1;
    }

    if (m_serialCom->isOpen())
    {
        return -2;
    }

    m_packetCounter = 0;
    m_bootloaderStarted = 0;
    tSend = 0;
    tStatus = 0;
    Master.cmd_fails = 0;

    Q_ASSERT(Master.q_aps_rp == 0);
    Q_ASSERT(Master.q_aps_wp == 0);

    Q_ASSERT(Master.q_items_wait_confirm == 0);
    Q_ASSERT(Master.q_items_wait_send == 0);

    if (m_serialCom->open(port, baudrate) == 0)
    {
        setState(MASTER_CONNECTING);
        m_serialPort = port;
        return 0;
    }

    return -3;
}

void zmMaster::queNextApsDataRequest()
{
    QueueItem_t *item;
    struct zm_command *cmd;
    unsigned id;
    const deCONZ::ApsDataRequest *aps;

    if (QAPS_Empty() == 0)
    {
        item = QItem_Alloc();
        if (!item)
            return;

        id = QAPS_Pop();
        DBG_Assert(id != UINT16_MAX);
        aps = deCONZ::controller()->getApsRequest(id);

        emit commandQueueEmpty();

        if (!aps)
        {
            return;
        }

        cmd = &item->cmd;
        if (aps->version() == 1)
        {
            cmd->cmd = ZM_CMD_APS_DATA_REQ;
        }
        else if (aps->version() == 2)
        {
            cmd->cmd = ZM_CMD_APS_DATA_REQ_2;
        }
        else
        {
            DBG_Printf(DBG_ERROR, "Unknown aps request version %u, ignored\n", unsigned(aps->version()));
            emit apsdeDataRequestDone(id, ZM_STATE_ERROR);
            return;
        }

//        if (m_devProtocolVersion >= DECONZ_PROTOCOL_VERSION_1_3 &&
//            aps->dstAddress().hasExt() &&
//            aps->dstAddress().hasNwk() &&
//            (aps->dstAddressMode() == deCONZ::ApsNwkAddress ||
//             aps->dstAddressMode() == deCONZ::ApsExtAddress))
//        {
//            // TODO optimize
//            // link address so device address map has proper nwk and extended address
//            QueueItem_t item2;
//            struct zm_command *cmd2 = &item2.cmd;
//            cmd2->cmd = ZM_CMD_LINK_ADDRESS;

//            uint8_t *p = cmd2->buffer.data;

//            uint16_t shortAddr = aps->dstAddress().nwk();
//            uint64_t extAddr = aps->dstAddress().ext();
//            p = put_u16_le(p, &shortAddr);
//            p = put_u64_le(p, &extAddr);
//            cmd2->buffer.len = p - cmd2->buffer.data;
//            Master.queue.push(item2);
//        }

        QByteArray arr;
        QDataStream stream(&arr, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);

        if (aps->writeToStream(stream) != 1)
        {
            DBG_Printf(DBG_ERROR, "APS request id: %u failed to serialize\n", (unsigned)aps->id());
            emit apsdeDataRequestDone(aps->id(), ZM_STATE_ERROR);
            return;
        }

        cmd->buffer.len = (quint16)arr.size();

        if (cmd->buffer.len > (sizeof(cmd->buffer.data)))
        {
            DBG_Printf(DBG_ERROR, "APS request id: %u too large\n", (unsigned)aps->id());
            emit apsdeDataRequestDone(aps->id(), ZM_STATE_ERROR);
            return;
        }

        Q_ASSERT (cmd->buffer.len < (sizeof(cmd->buffer.data))); // got a segv!!!

        DBG_Printf(DBG_PROT, "[Master] enqueue APS request id: %u, cmd.seq %u\n", aps->id(), cmd->seq);

        memcpy(&cmd->buffer.data[0], arr.constData(), cmd->buffer.len);
        Master.status0 &= ~ZM_STATUS_FREE_APS_SLOTS;
        QItem_Enqueue(item);
    }
}

int zmMaster::queApsDataConfirm()
{
    int i;
    QueueItem_t *item;
    struct zm_command *cmd;

    if (!QItems_Empty())
    {
        for (i = 0; i < MAX_QUEUE_ITEMS; i++)
        {
            if (Master.q_items[i].cmd.cmd != ZM_CMD_APS_DATA_CONFIRM)
                continue;

            if (Master.q_items[i].state == QITEM_STATE_WAIT_CONFIRM)
            {
                Master.status0 &= ~ZM_STATUS_APS_DATA_CONF;
                return 1;
            }

            if (Master.q_items[i].state == QITEM_STATE_WAIT_SEND)
            {
                Master.status0 &= ~ZM_STATUS_APS_DATA_CONF;
                return 1;
            }
        }
    }

    item = QItem_Alloc();
    if (!item)
        return 0;

    cmd = &item->cmd;
    cmd->cmd = ZM_CMD_APS_DATA_CONFIRM;
    cmd->buffer.len = 0;
    Master.status0 &= ~ZM_STATUS_APS_DATA_CONF;
    QItem_Enqueue(item);
    return 1;
}

int zmMaster::queApsDataIndication()
{
    int i;
    QueueItem_t *item;
    struct zm_command *cmd;

    if (!QItems_Empty())
    {
        for (i = 0; i < MAX_QUEUE_ITEMS; i++)
        {
            if (Master.q_items[i].cmd.cmd == ZM_CMD_APS_DATA_INDICATION || Master.q_items[i].cmd.cmd == ZM_CMD_APS_DATA_INDICATION_2)
            {
                if (Master.q_items[i].state == QITEM_STATE_WAIT_CONFIRM)
                {
                    Master.status0 &= ~ZM_STATUS_APS_DATA_IND;
                    return 1;
                }

                if (Master.q_items[i].state == QITEM_STATE_WAIT_SEND)
                {
                    Master.status0 &= ~ZM_STATUS_APS_DATA_IND;
                    return 1;
                }
            }
        }
    }

    item = QItem_Alloc();
    if (!item)
        return 0;

    cmd = &item->cmd;

    cmd->cmd = ZM_CMD_APS_DATA_INDICATION;
    cmd->buffer.len = 0;

    if (deviceProtocolVersion() >= DECONZ_PROTOCOL_VERSION_1_2)
    {
        cmd->cmd = ZM_CMD_APS_DATA_INDICATION_2;

        if (m_devProtocolVersion >= DECONZ_PROTOCOL_VERSION_1_11)
        {
            cmd->buffer.len = 1; // flags
            cmd->buffer.data[0] = 0x02 | 0x04; // 0x02 FLAG_INCLUDE_LAST_HOP | FLAG_INCLUDE_SRC_NWK_IEEE_ADDRESS
        }
        else if (m_devProtocolVersion >= DECONZ_PROTOCOL_VERSION_1_8)
        {
            cmd->buffer.len = 1; // flags
            cmd->buffer.data[0] = 0x02; // 0x02 FLAG_INCLUDE_LAST_HOP
        }
    }

    Master.status0 &= ~ZM_STATUS_APS_DATA_IND;

    //cmd->buffer.len = 1; // flags
    //cmd->buffer.data[0] = 0x01; // 0x01 FLAG_PREFER_NWK_SRC_ADDR
    QItem_Enqueue(item);
    return 1;
}

void zmMaster::queInterpanDataIndication()
{
    QueueItem_t *item;
    struct zm_command *cmd;

    item = QItem_Alloc();
    if (!item)
        return;

    cmd = &item->cmd;
    cmd->cmd = ZM_CMD_INTERPAN_INDICATION;
    cmd->buffer.len = 0;
    Master.status1 &= ~ZM_STATUS_INTERPAN_IND;
    QItem_Enqueue(item);
}

void zmMaster::queInterpanDataConfirm()
{
    QueueItem_t *item;
    struct zm_command *cmd;

    item = QItem_Alloc();
    if (!item)
        return;

    cmd = &item->cmd;
    cmd->cmd = ZM_CMD_INTERPAN_CONFIRM;
    cmd->buffer.len = 0;
    Master.status1 &= ~ZM_STATUS_INTERPAN_CONF;
    QItem_Enqueue(item);
}

void zmMaster::queGetStartNetworkConfirmStatus()
{
    QueueItem_t *item;
    struct zm_command *cmd;

    item = QItem_Alloc();
    if (!item)
        return;

    cmd = &item->cmd;
    cmd->cmd = ZM_CMD_ZDO_NET_CONFIRM;
    cmd->buffer.len = 0;
    QItem_Enqueue(item);
}

void zmMaster::sendNextCommand()
{
    if (!connected())
        return;

    if (Master.q_items_wait_send && Master.q_items_wait_confirm < MaxUnconfirmed)
    {
        QueueItem_t *item = QItem_NextToSend();
        if (!item)
            return;

        item->tref_tx = 0;


        int ret = QItem_Send(item);

        DBG_Printf(DBG_PROT, "[Master] send packet seq: %u, %s\n", item->cmd.seq, cmdToString(item->cmd.cmd));
        if (ret == 0)
        {
            if (QAPS_Empty())
            {
                emit commandQueueEmpty();
            }
        }
        else if (ret == -1)
        {
            DBG_Printf(DBG_ERROR, "[COM] tx queue full\n");
        }
        else
        {
            DBG_Printf(DBG_ERROR, "[COM] failed to send command, ret: %d\n", ret);
            killCommand(&item->cmd, ZM_STATE_ERROR);
            QItem_Free(item);
        }
    }
}

void zmMaster::taskTimerFired()
{
    taskHandler(m_taskTimerEvent);
}

#if 0
/*! Only for testing purpose read registers.
 */
void zmMaster::readRegisterTimerFired()
{
    if (connected())
    {
        QueueItem_t item;
        struct zm_command *cmd = &item.cmd;

        cmd->cmd = ZM_CMD_READ_REGISTER;
        cmd->seq = Master.seq++;
        cmd->buffer.len = 1;
        cmd->buffer.data[0] = 0x01; // 16-bit addresses

        uint8_t *p = cmd->buffer.data + 1;

        QString regs = deCONZ::appArgumentString("--read-reg", "");
        if (!regs.isEmpty())
        {
            QStringList ls = regs.split(',');

            QStringList::const_iterator i = ls.constBegin();
            QStringList::const_iterator end = ls.constEnd();

            for (; i != end; ++i)
            {
                bool ok;
                uint16_t addr = i->toUShort(&ok, 16);

                if (ok)
                {
                    if ((cmd->buffer.len + 2) <= ZM_MAX_BUFFER_LEN)
                    {
                        DBG_Printf(DBG_INFO, "Read REG 0x%04X\n", addr);
                        p = put_u16_le(p, &addr);
                        cmd->buffer.len += 2;
                    }
                }
            }
        }

        Master.queue.push(item);
    }

    int msec = deCONZ::appArgumentNumeric("--read-reg-interval", 0);

    if (msec)
    {
        QTimer::singleShot(msec, this, SLOT(readRegisterTimerFired()));
    }
}
#endif

void zmMaster::initSerialCom()
{
    Q_ASSERT(m_serialCom == nullptr);
    m_serialCom = new SerialCom;

    connect(m_serialCom, SIGNAL(connected()),
            this, SLOT(onDeviceConnected()));

    connect(m_serialCom, SIGNAL(disconnected(int)),
            this, SLOT(onDeviceDisconnected(int)));

    connect(m_serialCom, &SerialCom::bootloaderStarted,
            this, &zmMaster::bootloaderStarted);
}

void zmMaster::processQueue()
{
    if (m_state == MASTER_IDLE)
    {
        if (needStatus)
        {
            if (EnqueueStatus() != nullptr)
            {
                Master.status0 &= ~(ZM_STATUS_APS_DATA_CONF | ZM_STATUS_APS_DATA_IND | ZM_STATUS_FREE_APS_SLOTS);
                needStatus = 0;
            }
            return;
        }
        else if (Master.q_items_wait_send >= MAX_QUEUE_ITEMS)
        {
            return;
        }
        else if (Master.q_items_wait_confirm >= MaxUnconfirmed)
        {
            return;
        }

        if (Master.status0 & ZM_STATUS_APS_DATA_CONF)
        {
            if (queApsDataConfirm())
                return;
        }
        else if (Master.status0 & ZM_STATUS_APS_DATA_IND)
        {
            if (queApsDataIndication())
                return;
        }

        if (netState() == deCONZ::InNetwork)
        {
            if ((Master.status0 & ZM_STATUS_FREE_APS_SLOTS) && QAPS_Empty() == 0)
            {
                queNextApsDataRequest();
            }
        }

        if (Master.status1 & ZM_STATUS_INTERPAN_IND)
        {
            queInterpanDataIndication();
        }
        else if (Master.status1 & ZM_STATUS_INTERPAN_CONF)
        {
            queInterpanDataConfirm();
        }
    }
}

void zmMaster::appAboutToQuit()
{
    disconnect(m_serialCom, nullptr, this, nullptr);
    killCommandQueue();
}

void zmMaster::timerEvent(QTimerEvent *event)
{
    if (m_state != MASTER_IDLE)
        return;

    if (event->timerId() == m_timeoutTimer)
    {
        if (Master.q_items_wait_confirm || Master.q_items_wait_send || QAPS_Empty() == 0)
        {
            DBG_Printf(DBG_PROT, "[Master] timer: q.wait_send: %u, q.wait_confirm: %u, q.aps_empty %u\n", Master.q_items_wait_send, Master.q_items_wait_confirm, QAPS_Empty());
        }

        uint64_t now = deCONZ::steadyTimeRef().ref;

        if (now - tSend > 60)
        {
            if (Master.q_items_wait_confirm)
            {
                if (needStatus == 0)
                {
                    needStatus = 1;
                    DBG_Printf(DBG_PROT, "[Master] send fill command\n");
                }
            }
            else if ((now - tStatus) > 1000)
            {
                //DBG_Printf(DBG_PROT, "[Master] query status\n");
                needStatus = 1;

                if (QAPS_Empty())
                {
                    emit commandQueueEmpty();
                }
            }

            // proceed sending
            if (needStatus || QAPS_Empty() == 0 || Master.q_items_wait_send || (Master.status0 & (ZM_STATUS_APS_DATA_CONF | ZM_STATUS_APS_DATA_IND)))
            {
                if (!m_taskTimer->isActive())
                    startTaskTimer(ACTION_PROCESS, 0, __LINE__);
            }
        }

        handleTimeouts();
    }
}

void zmMaster::processPacked(const zm_command *cmd)
{
    if (m_state == MASTER_OFF || m_state == MASTER_CONNECTING)
    {
        return;
    }

    Q_ASSERT(Master.q_items_wait_confirm <= MaxUnconfirmed);

    DBG_Printf(DBG_PROT, "[Master] process packet seq: %u, %s\n", cmd->seq, cmdToString(cmd->cmd));

    QItem_Confirm(cmd);

    if (Master.q_items_wait_confirm == 0)
        tSend = 0; // no longer wait for response

    if (m_packetCounter < INT_MAX)
    {
        if (m_packetCounter == 0)
        {
            firmwareVersionRequest();
            unlockMaxNodes();
            emit deviceConnected();
        }
        m_packetCounter++;
    }

    switch (cmd->cmd)
    {
    case ZM_CMD_STATUS:
    case ZM_CMD_STATUS_CHANGE:
    {
        tStatus = deCONZ::steadyTimeRef().ref;
        needStatus = 0;
        checkStatus0(cmd->data);
        checkStatus1(cmd->data);

        emit deviceState();
    }
        break;

    case ZM_CMD_VERSION:
    {
        needStatus = 1;
        get_u32_le(cmd->data, &m_devFirmwareVersion);

        if (m_devName.isEmpty() && deCONZ::DeviceEnumerator::instance()->listSerialPorts())
        {
            const auto &devs = deCONZ::DeviceEnumerator::instance()->getList();

            auto dev = std::find_if(devs.cbegin(), devs.cend(), [this](const auto &x)
            {
                return x.path == m_serialPort;
            });

            if (dev != devs.cend() && !dev->friendlyName.isEmpty())
            {
                m_devName = dev->friendlyName;
            }
        }

        if (m_devName.isEmpty()) // TODO remove after testing, this is the old code with coarse detection
        {
            if ((deviceFirmwareVersion() & FW_PLATFORM_MASK) == FW_PLATFORM_R21)
            {
                if (m_serialPort.contains(QLatin1String("ttyACM")))
                {
                    m_devName = QLatin1String("ConBee II");
                }
                else if (m_serialPort.contains(QLatin1String("ttyAMA")) || m_serialPort.contains(QLatin1String("ttyS")))
                {
                    m_devName = QLatin1String("RaspBee II");
                }
            }
            else if ((deviceFirmwareVersion() & FW_PLATFORM_MASK) == FW_PLATFORM_AVR)
            {
                if (m_serialPort.contains(QLatin1String("ttyAMA")) || m_serialPort.contains(QLatin1String("ttyS")))
                {
                    m_devName = QLatin1String("RaspBee");
                }
                else
                {
                    m_devName = QLatin1String("ConBee");
                }
            }
        }

        DBG_Printf(DBG_INFO, "Device firmware version 0x%08X %s\n", m_devFirmwareVersion, qPrintable(m_devName));

#ifdef Q_OS_LINUX
        if (!QFile::exists(firmwareVersionFile))
        {
            QFile f(firmwareVersionFile);

            if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            {
                QTextStream stream(&f);
                QString version = QString("0x%1\n").arg(m_devFirmwareVersion, 8, 16, QLatin1Char('0'));
                stream << version;
                f.close();
            }
            else
            {
                DBG_Printf(DBG_ERROR, "could not open %s : %s\n", firmwareVersionFile, qPrintable(f.errorString()));
            }
        }
#endif // Q_OS_LINUX
    }
        break;

    case ZM_CMD_FEATURE:
    {
        if (FEATURE_MAX_NODES == cmd->buffer.data[0])
        {
            if (cmd->buffer.data[1] == ZM_STATE_SUCCESS)
            {
                if (cmd->buffer.len == 4)
                {
                    uint16_t maxNodes;

                    get_u16_le(cmd->buffer.data + 2, &maxNodes);

                    if (maxNodes >= APP_MIN_NODES && (maxNodes <= APP_MAX_NODES))
                    {
                        DBG_Printf(DBG_INFO, "unlocked max nodes: %u\n", maxNodes);
                        m_maxNodes = maxNodes;
                    }
                }
            }
        }       
    }
        break;

    case ZM_CMD_UPDATE_NEIGHBOR:
    {
        DBG_Printf(DBG_PROT, "[Master] verify neighbor status: %s (0x%02X) \n",  appStatusToString(cmd->status), cmd->status);
    }
        break;

    case ZM_CMD_APS_DATA_REQ:
    case ZM_CMD_APS_DATA_REQ_2:
    {
        checkStatus0(cmd->buffer.data);
        needStatus = 0;

        if (cmd->status == ZM_STATE_SUCCESS)
        {
            emit apsdeDataRequestDone(cmd->buffer.data[1], cmd->status);
        }
        else
        {
            DBG_Printf(DBG_ERROR, "[Master] APS-DATA.request seq: %u, id: %u, failed-status: %s (0x%02X) \n", cmd->seq, cmd->buffer.data[1], appStatusToString(cmd->status), cmd->status);
        }
    }
        break;

    case ZM_CMD_APS_DATA_CONFIRM:
    {
        checkStatus0(cmd->buffer.data);
        needStatus = 0;

        if (cmd->status == ZM_STATE_SUCCESS)
        {
            deCONZ::ApsDataConfirm confirm;
            const auto arr = QByteArray::fromRawData((const char*)&cmd->buffer.data[1], cmd->buffer.len - 1);
            QDataStream stream(arr);
            stream.setByteOrder(QDataStream::LittleEndian);

            confirm.readFromStream(stream);
            if (!confirm.dstAddress().hasExt() && confirm.dstAddress().hasNwk())
            {
                deCONZ::controller()->resolveAddress(confirm.dstAddress());
            }

            deCONZ::controller()->onApsdeDataConfirm(confirm);
        }
        else
        {
            DBG_Printf(DBG_ERROR, "[Master] APS-DATA.confirm seq: %u, id: %u, failed-status: %s (0x%02X) \n", cmd->seq, cmd->buffer.data[1], appStatusToString(cmd->status), cmd->status);
        }
    }
        break;

    case ZM_CMD_APS_DATA_INDICATION:
    case ZM_CMD_APS_DATA_INDICATION_2:
    {
        //DBG_Assert(m_state == MASTER_BUSY);
        checkStatus0(cmd->buffer.data);
        needStatus = 0;

        if (cmd->status == ZM_STATE_SUCCESS)
        {
            DBG_MEASURE_START(CORE_APS_IND);

            deCONZ::ApsDataIndication &ind = m_ind;
            ind.reset();

            if (cmd->cmd == ZM_CMD_APS_DATA_INDICATION_2)
            {
                if (m_devProtocolVersion < DECONZ_PROTOCOL_VERSION_1_8)
                {
                    ind.setVersion(2);
                }
                else
                {
                    ind.setVersion(3);
                }
            }

            {
                const QByteArray arr = QByteArray::fromRawData((const char*)&cmd->buffer.data[1], cmd->buffer.len - 1);
                QDataStream stream(arr);
                stream.setByteOrder(QDataStream::LittleEndian);
                ind.readFromStream(stream);
            }

            // DBG_Printf(DBG_PROT, "[Master] got APS indication rxtime: %u ms\n", ind.rxTime());

            if (DBG_IsEnabled(DBG_APS))
            {
                int n;
                char srcAddr[24];

                if (ind.srcAddressMode() == deCONZ::ApsExtAddress)
                {
                    n = snprintf(srcAddr, sizeof(srcAddr) - 1, FMT_MAC, FMT_MAC_CAST(ind.srcAddress().ext()));
                }
                else
                {
                    n = snprintf(srcAddr, sizeof(srcAddr) - 1, "0x%04X", (unsigned)ind.srcAddress().nwk());
                }
                srcAddr[n] = '\0';

                DBG_Printf(DBG_APS, "APS-DATA.indication srcAddr: %s, srcEp: 0x%02X dstAddrMode: %u, profile: 0x%04X, cluster: 0x%04X, lqi: %u, rssi: %d\n",
                            srcAddr, ind.srcEndpoint(), (quint8)ind.dstAddressMode(), ind.profileId(), ind.clusterId(), ind.linkQuality(), ind.rssi());

                if (DBG_IsEnabled(DBG_APS_L2))
                {
                    DBG_Printf(DBG_APS_L2, "\tasdu: %s\n", qPrintable(ind.asdu().toHex()));
                }
            }

            if (!(ind.srcAddress().hasExt() && ind.srcAddress().hasNwk()))
            {
                deCONZ::controller()->resolveAddress(ind.srcAddress());
            }

            deCONZ::controller()->onApsdeDataIndication(ind);

            DBG_MEASURE_END(CORE_APS_IND);
        }
        else
        {
            DBG_Printf(DBG_ERROR, "[Master] APS-DATA.indication seq: %u, failed-status: %s (0x%02X) \n", cmd->seq, appStatusToString(cmd->status), cmd->status);
        }
    }
        break;

    case ZM_CMD_CHANGE_NET_STATE:
    {
        emit changeNetStateDone(cmd->status);
    }
        break;

    case ZM_CMD_MAC_POLL:
    {
        //needStatus = 1;
        deCONZ::Address addr;

        const unsigned char *p = &cmd->buffer.data[0];
        quint32 lifeTime = ~0;
        quint32 devTimeout = ~0;

        if (0x02 == *p)
        {
            quint16 nwk;
            p = get_u16_le(p + 1, &nwk);
            addr.setNwk(nwk);

            p++; // lqi
            p++; // rssi

            if (cmd->buffer.len >= (1 + 2 + 2 + 4 + 4))
            {
                p = get_u32_le(p, &lifeTime);
                get_u32_le(p, &devTimeout);
            }

            DBG_Printf(DBG_ZDP, "MAC Poll 0x%02X 0x%02X%02X, life time: %u sec, dev timeout: %u sec\n", cmd->buffer.data[0], cmd->buffer.data[2], cmd->buffer.data[1], lifeTime, devTimeout);
            emit macPoll(addr, lifeTime);
        }
        else if (0x03 == *p)
        {
            uint64_t ext;
            get_u64_le(p + 1, &ext);
            addr.setExt(ext);
        }
    }
        break;

    case ZM_CMD_BEACON:
    {
        const uint BEACON_LEN = 7;

        deCONZ::Beacon b;

        const uint8_t *p = cmd->buffer.data;
        quint16 len = cmd->buffer.len;

        while (len >= BEACON_LEN)
        {
            p = get_u16_le(p, &b.source);
            p = get_u16_le(p, &b.panId);
            p = get_u8_le(p, &b.channel);
            p = get_u8_le(p, &b.flags);
            p = get_u8_le(p, &b.updateId);

            emit beacon(b);

            len -= BEACON_LEN;
        }
    }
        break;

    case ZM_CMD_ZDO_NET_CONFIRM:
    {
        DBG_Printf(DBG_INFO, "NET ZDO start network status 0x%02X\n", cmd->data[0]);
        emit netStartDone(cmd->data[0]);
    }
        break;

    case ZM_CMD_READ_PARAM:
    {
        ZM_State_t status = (ZM_State_t)cmd->status;
        ZM_DataId_t id = (ZM_DataId_t)cmd->buffer.data[0];

        if (id == ZM_DID_STK_PROTOCOL_VERSION)
        {
            if ((status == ZM_STATE_SUCCESS) && (cmd->buffer.len == 3))
            {
                uint16_t version;
                get_u16_le(cmd->buffer.data + 1, &version);

                if ((version >= DECONZ_PROTOCOL_VERSION_MIN)/* && (version <= DECONZ_PROTOCOL_VERSION)*/)
                {
                    m_devProtocolVersion = version;
                    DBG_Printf(DBG_INFO, "Device protocol version: 0x%04X\n", version);
                }
                else
                {
                    DBG_Printf(DBG_INFO, "Unsupported device protocol version: 0x%04X\n", version);
                }
            }
            else
            {
                // downgrade if device was changed
                m_devProtocolVersion = DECONZ_PROTOCOL_VERSION_MIN;
            }
        }
        else if (id == ZM_DID_APS_TRUST_CENTER_ADDRESS)
        {
            // read link key for trust center
            uint64_t tcAddr;
            get_u64_le(cmd->buffer.data + 1, &tcAddr);
            if (tcAddr != 0)
            {
                readParameterWithArg(ZM_DID_STK_LINK_KEY, &cmd->buffer.data[1], sizeof(uint64_t));
            }
        }

        deCONZ::controller()->readParameterResponse(status, id, &cmd->buffer.data[1], cmd->buffer.len - 1);
        emit parameterUpdated(id);
    }
        break;

    case ZM_CMD_WRITE_PARAM:
    {
        DBG_Printf(DBG_PROT, "%s write param rsp seq: %u, param: 0x%02X, status: 0x%02X\n", Q_FUNC_INFO, cmd->seq, cmd->buffer.data[0], cmd->status);

        emit writeParameterDone(cmd->buffer.data[0], cmd->status);
    }
        break;

    case ZM_CMD_START_INTERPAN_MODE:
    {
        if (cmd->status == ZM_STATE_SUCCESS)
        {
            Master.ipanState = IPAN_CONNECTING;
//            m_startInterpanTimer->start(100);
        }
        else
        {
            DBG_Printf(DBG_INFO, "Start interpan mode status=0x%02X\n", cmd->status);
            emit startInterpanModeConfirm(deCONZ::TouchlinkFailed);
        }
    }
        break;

    case ZM_CMD_SEND_INTERPAN_REQ:
    {
        if (cmd->status != ZM_STATE_SUCCESS)
        {
            DBG_Printf(DBG_TLINK, "send interpan req status=0x%02X\n", cmd->status);
            emit sendInterpanConfirm(deCONZ::TouchlinkFailed);
        }
    }
        break;

    case ZM_CMD_INTERPAN_INDICATION:
    {
        checkStatus0(cmd->buffer.data);
        checkStatus1(cmd->buffer.data);
        needStatus = 0;

        if (cmd->status == ZM_STATE_SUCCESS)
        {
            // forward data frame without status bytes
            QByteArray ind((char*)(cmd->buffer.data + 2), (int)(cmd->buffer.len - 2));
            emit interpanIndication(ind);
        }
        else
        {
            DBG_Printf(DBG_TLINK, "interpan indication status=0x%02X\n", cmd->status);
        }
    }
        break;

    case ZM_CMD_INTERPAN_CONFIRM:
    {
        checkStatus0(cmd->buffer.data);
        checkStatus1(cmd->buffer.data);
        needStatus = 0;

        if (cmd->status == ZM_STATE_SUCCESS)
        {
            if (cmd->buffer.data[2] == 0x00)
            {
                emit sendInterpanConfirm(deCONZ::TouchlinkSuccess);
            }
            else
            {
                DBG_Printf(DBG_TLINK, "interpan confirm status=0x%02X\n", cmd->buffer.data[2]);
                emit sendInterpanConfirm(deCONZ::TouchlinkFailed);
            }
        }
        else
        {
            DBG_Printf(DBG_TLINK, "interpan confirm status=0x%02X\n", cmd->status);
            emit sendInterpanConfirm(deCONZ::TouchlinkFailed);
        }
    }
        break;

    case ZM_CMD_READ_REGISTER:
    {
        if (cmd->status == ZM_STATE_SUCCESS)
        {
            if (cmd->buffer.len > 1)
            {
                if (cmd->buffer.data[0] == 0x01) // 16-bit address, 8-bit value
                {
                    QByteArray arr((const char*)&cmd->buffer.data[1], cmd->buffer.len - 1);
                    QDataStream stream(arr);
                    stream.setByteOrder(QDataStream::LittleEndian);

                    uint16_t len = cmd->buffer.len - 1;
                    while (len >= 3)
                    {
                        uint16_t addr;
                        uint8_t val;

                        stream >> addr;
                        stream >> val;

                        DBG_Printf(DBG_INFO, "REG 0x%04X = 0x%02X\n", addr, val);
                        len -= 3;
                    }
                }
            }
        }
    }
        break;

    case ZM_CMD_GP_DATA_INDICATION:
    {
        if (cmd->buffer.len > 0)
        {
            deCONZ::GreenPowerController *gpCtrl = deCONZ::GreenPowerController::instance();

            if (gpCtrl)
            {
                QByteArray arr = QByteArray::fromRawData((const char*)cmd->buffer.data, cmd->buffer.len);
                gpCtrl->processIncomingData(arr);
            }
        }
    }
        break;

    case ZM_CMD_PHY_FRAME:
    {
        if (cmd->buffer.len > 0)
        {
            /*------------------------------------------------------------
            *
            *      ZEP Packets must be received in the following format:
            *      |UDP Header|  ZEP Header |IEEE 802.15.4 Packet|
            *      | 8 bytes  | 16/32 bytes |    <= 127 bytes    |
            *------------------------------------------------------------
            *
            *      ZEP v1 Header will have the following format:
            *      |Preamble|Version|Channel ID|Device ID|CRC/LQI Mode|LQI Val|Reserved|Length|
            *      |2 bytes |1 byte |  1 byte  | 2 bytes |   1 byte   |1 byte |7 bytes |1 byte|
            *
            *      ZEP v2 Header will have the following format (if type=1/Data):
            *      |Preamble|Version| Type |Channel ID|Device ID|CRC/LQI Mode|LQI Val|NTP Timestamp|Sequence#|Reserved|Length|
            *      |2 bytes |1 byte |1 byte|  1 byte  | 2 bytes |   1 byte   |1 byte |   8 bytes   | 4 bytes |10 bytes|1 byte|
            *
            *      ZEP v2 Header will have the following format (if type=2/Ack):
            *      |Preamble|Version| Type |Sequence#|
            *      |2 bytes |1 byte |1 byte| 4 bytes |
            *------------------------------------------------------------
            */
            QByteArray pkt;
            QDataStream stream(&pkt, QIODevice::WriteOnly);
            stream.setByteOrder(QDataStream::BigEndian);

            static quint32 seqNum = 1;
            quint8 type = cmd->buffer.len > 5 ? 1 : 0;


            struct timeval tref;
            gettimeofday(&tref, NULL);

            const quint32 secs = tref.tv_sec + 0x83AA7E80;
            const quint32 fraction = (uint32_t)( (double)(tref.tv_usec+1) * (double)(1LL<<32) * 1.0e-6 );

            // preamble
            stream << (uint8_t)'E';
            stream << (uint8_t)'X';
            // version
            stream << (uint8_t)2;
            // type
            stream << type;

            if (type == 1) // data
            {
                // channel
                stream << (uint8_t)11;
                // deviceId
                stream << (uint16_t)0;
                // crc/lqi mode
                stream << (uint8_t)0;
                // lqi val
                stream << (uint8_t)0;
                // NTP timestamp
                stream << secs;
                stream << fraction;
            }

            // http://waitingkuo.blogspot.de/2012/06/conversion-between-ntp-time-and-unix.html

//            void convert_ntp_time_into_unix_time(struct ntp_time_t *ntp, struct timeval *unix)
//            {
//                unix->tv_sec = ntp->second - 0x83AA7E80; // the seconds from Jan 1, 1900 to Jan 1, 1970
//                unix->tv_usec = (uint32_t)( (double)ntp->fraction * 1.0e6 / (double)(1LL<<32) );
//            }

//            void convert_unix_time_into_ntp_time(struct timeval *unix, struct ntp_time_t *ntp)
//            {
//                ntp->second = unix->tv_sec + 0x83AA7E80;
//                ntp->fraction = (uint32_t)( (double)(unix->tv_usec+1) * (double)(1LL<<32) * 1.0e-6 );
//            }

            // seq num
            stream << (quint32)seqNum++;

            quint8 len = cmd->buffer.len;
            if (type == 1) // data
            {
                // reserved 10 bytes
                stream << (quint8)0;
                stream << (quint8)0;
                stream << (quint8)0;
                stream << (quint8)0;
                stream << (quint8)0;
                stream << (quint8)0;
                stream << (quint8)0;
                stream << (quint8)0;
                stream << (quint8)0;
                stream << (quint8)0;
                stream << (quint8)len;
            }

            for (uint i = 0; i < len; i++)
                stream << cmd->buffer.data[i];
        }
    }
        break;

    case ZM_CMD_LINK_ADDRESS:
        break;

    case ZM_CMD_DEBUG_LOG:
    {
        if (DBG_IsEnabled(DBG_PROT))
        {
            if (cmd->buffer.len < sizeof(cmd->buffer.data))
            {
                char *beg = (char*)&cmd->buffer.data[0];

                unsigned i;
                for (i = 0; i < cmd->buffer.len; i++)
                {
                    if (beg[i] == '\0' || beg[i] == '\r' || beg[i] == '\n')
                        break;
                }

                beg[i] = '\0';
                DBG_Printf(DBG_INFO, "%s\n", beg);
            }
            else
            {
                DBG_Printf(DBG_INFO, "FW debug string too large\n");
            }
        }
    }
        break;


    default:
        DBG_Printf(DBG_PROT, "%s unknown cmd: %u, seq %u\n", Q_FUNC_INFO, cmd->cmd, cmd->seq);
        break;
    }

    if (!m_taskTimer->isActive() && m_state == MASTER_IDLE)
        startTaskTimer(ACTION_PROCESS, 0, __LINE__);
    //processQueue();
    //sendNextCommand();

    emit deviceActivity();
}

/*!
    APSDE-DATA.request.

    \param aps a APSDE-DATA.request primitive.

    \return  0 on success
            -1 not connected to device
            -2 not joined to a network
            -3 queue is full
 */
int zmMaster::apsdeDataRequest(const deCONZ::ApsDataRequest &aps)
{
    if (!connected())
    {
        return -1;
    }

    if (netState() != deCONZ::InNetwork)
    {
        return -2;
    }

    if (QAPS_Full())
        return -3;

    if (QAPS_Push(aps.id()) != 1)
    {
        Q_ASSERT(0 && "unexpected aps queue");
    }
    else
    {
        DBG_Printf(DBG_PROT, "[Master] add APS request id: %u\n", aps.id());
    }

    taskHandler(EVENT_ITEM_ADDED);

    return 0;
}

int zmMaster::startInterpanMode(uint8_t channel)
{
    QueueItem_t *item;
    struct zm_command *cmd;

    if (netState() != deCONZ::NotInNetwork)
    {
        return -2;
    }

    DBG_Assert((channel >= 11) && (channel <= 26));
    if ((channel < 11) || (channel > 26))
    {
        return -3;
    }

    item = QItem_Alloc();
    if (!item)
        return -4;

    cmd = &item->cmd;
    cmd->cmd = ZM_CMD_START_INTERPAN_MODE;

    uint8_t *p = cmd->buffer.data;
    p = put_u8_le(p, &channel);
    cmd->buffer.len = (quint16)(p - cmd->buffer.data);

    DBG_Assert(cmd->buffer.len == 1);
    DBG_Assert(cmd->buffer.len < (sizeof(cmd->buffer.data)));

    QItem_Enqueue(item);

    taskHandler(EVENT_ITEM_ADDED);

    return 0;
}

int zmMaster::sendInterpanRequest(const deCONZ::TouchlinkRequest &req)
{
    QueueItem_t *item;
    struct zm_command *cmd;

    if (netState() != deCONZ::InNetwork && Master.ipanState != IPAN_CONNECTED)
    {
        return -1;
    }

    item = QItem_Alloc();
    if (!item)
        return -2;

    cmd = &item->cmd;
    cmd->cmd = ZM_CMD_SEND_INTERPAN_REQ;

    QByteArray arr;
    QDataStream stream(&arr, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    req.writeToStream(stream);

    cmd->buffer.len = (quint16)arr.size();

    DBG_Assert(cmd->buffer.len < (sizeof(cmd->buffer.data)));

    if (cmd->buffer.len >= (sizeof(cmd->buffer.data)))
    {
        return -1;
    }

    for (uint i = 0; i < cmd->buffer.len; i++)
    {
        cmd->buffer.data[i] = arr[i];
    }

    QItem_Enqueue(item);

    taskHandler(EVENT_ITEM_ADDED);

    return 0;
}

int zmMaster::firmwareVersionRequest()
{
    QueueItem_t *item;
    struct zm_command *cmd;

    if (connected())
    {
        item = QItem_Alloc();
        if (!item)
            return -1;

#ifdef Q_OS_LINUX
        // delete firmware version file (will be rewritten once connected)
        if (QFile::exists(firmwareVersionFile))
        {
            if (!QFile::remove(firmwareVersionFile))
            {
                DBG_Printf(DBG_ERROR, "could not delete %s\n", firmwareVersionFile);
            }
        }
#endif // Q_OS_LINUX

        cmd = &item->cmd;
        cmd->cmd = ZM_CMD_VERSION;
        cmd->buffer.len = 0;

        QItem_Enqueue(item);
        taskHandler(EVENT_ITEM_ADDED);
        return 0;
    }

    return -1;
}

int zmMaster::nwkLeaveRequest(const deCONZ::NwkLeaveRequest &req)
{
    QueueItem_t *item;
    struct zm_command *cmd;

    if (connected())
    {
        item = QItem_Alloc();
        if (!item)
            return -2;

        cmd = &item->cmd;
        cmd->cmd = ZM_CMD_NWK_LEAVE_REQ;
        cmd->buffer.len = 4;

        auto *p = put_u16_le(cmd->buffer.data, &req.flags);
        put_u16_le(p, &req.dstAddress);

        QItem_Enqueue(item);
        taskHandler(EVENT_ITEM_ADDED);
        return 0;
    }

    return -2;
}

/*!
    Try to unlock more nodes.
    \return  0 if request will be processed
    \return -1 on error
 */
int zmMaster::unlockMaxNodes()
{
    QueueItem_t *item;
    struct zm_command *cmd;

    if (connected())
    {
        item = QItem_Alloc();
        if (!item)
            return -1;

        const uint16_t maxNodes = APP_MAX_NODES;

        cmd = &item->cmd;
        cmd->cmd = ZM_CMD_FEATURE;
        cmd->buffer.len = 4;

        cmd->buffer.data[0] = FEATURE_MAX_NODES;
        cmd->buffer.data[1] = COMMERCIAL_KEY;
        put_u16_le(cmd->buffer.data + 2, &maxNodes);

        QItem_Enqueue(item);
        taskHandler(EVENT_ITEM_ADDED);
        return 0;
    }

    return -1;
}

void zmMaster::factoryReset()
{
    QueueItem_t *item;
    struct zm_command *cmd;

    if (connected())
    {
        item = QItem_Alloc();
        if (!item)
            return;

        cmd = &item->cmd;
        cmd->cmd = ZM_CMD_FACTORY_RESET;
        cmd->buffer.len = 0;

        QItem_Enqueue(item);
        taskHandler(EVENT_ITEM_ADDED);
    }
}

/*! Returns true if connected to device and operational.
 */
bool zmMaster::connected()
{
    if (m_state == MASTER_OFF || m_state == MASTER_CONNECTING)
        return false;
    return m_serialCom && m_serialCom->isApplicationConnected();
}

/*! Returns true if connected to device (but might not be operational).
 */
bool zmMaster::isOpen()
{
    return m_serialCom && m_serialCom->isOpen();
}

void zmMaster::comExit()
{
    if (m_state == MASTER_OFF)
        return;

    killCommandQueue();

    if (m_serialCom)
    {
        m_serialCom->close();
        m_devFirmwareVersion = 0;
    }
}

void COM_OnPacket(const zm_command *cmd)
{
    Master.instance->processPacked(cmd);
}

void zmMaster::onDeviceConnected()
{
    needStatus = 1;
    setState(MASTER_IDLE);
    startTaskTimer(ACTION_PROCESS, SendDelay, __LINE__);

    uint32_t loglevel = DBG_APS | DBG_APS_L2;

    writeParameter(ZM_DID_STK_DEBUG_LOG_LEVEL, (unsigned char*)&loglevel, 4);
}

void zmMaster::onDeviceDisconnected(int reason)
{
    m_taskTimer->stop();
    setState(MASTER_OFF);
    m_serialPort.clear();
    m_devFirmwareVersion = 0;
    killCommandQueue();
    emit deviceDisconnected(reason);
}

void zmMaster::taskHandler(zmMaster::MasterEvent event)
{
    switch (m_state)
    {
    case MASTER_OFF:
        break;

    case MASTER_IDLE:
        handleStateIdle(event);
        break;

    default:
        setState(MASTER_IDLE);
        break;
    }
}

void zmMaster::handleStateIdle(zmMaster::MasterEvent event)
{
    switch (event)
    {
    case ACTION_PROCESS:
    {
        unsigned wait_confirm0 = Master.q_items_wait_confirm;
        processQueue();
        sendNextCommand();

        if (Master.q_items_wait_confirm < MaxUnconfirmed && !m_taskTimer->isActive())
        {
            if (Master.status0 & (ZM_STATUS_APS_DATA_CONF | ZM_STATUS_APS_DATA_IND))
            {
//                DBG_Printf(DBG_INFO, "IDLE ACTION_PROCESS WS: %u, WC: %u, C: %u, I: %u\n", Master.q_items_wait_send, Master.q_items_wait_confirm, (Master.status0 & ZM_STATUS_APS_DATA_CONF), (Master.status0 & ZM_STATUS_APS_DATA_IND));
                int interval = Master.q_items_wait_confirm * 10;
                startTaskTimer(ACTION_PROCESS, interval, __LINE__);
            }
            else if ((QAPS_Empty() == 0 || Master.q_items_wait_send > 0))
            {
                startTaskTimer(ACTION_PROCESS, SendDelay, __LINE__);
            }
        }
    }
        break;

    case EVENT_ITEM_ADDED:
    {
        if (!m_taskTimer->isActive())
            startTaskTimer(ACTION_PROCESS, 0, __LINE__);
    }
        break;

    default:
        break;
    }
}

QueueItem *EnqueueStatus()
{
    unsigned i;
    QueueItem *item;
    struct zm_command *cmd;

    if (!QItems_Empty())
    {
        for (i = 0; i < MAX_QUEUE_ITEMS; i++)
        {
            if (Master.q_items[i].state == QITEM_STATE_INIT)
                continue;

            if (Master.q_items[i].cmd.cmd != ZM_CMD_STATUS)
                continue;

            if (Master.q_items[i].state == QITEM_STATE_WAIT_CONFIRM)
                return &Master.q_items[i];

            if (Master.q_items[i].state == QITEM_STATE_WAIT_SEND)
                return &Master.q_items[i];

            break;
        }
    }

    item = QItem_Alloc();
    if (!item)
        return nullptr;

    cmd = &item->cmd;
    cmd->cmd = ZM_CMD_STATUS;
    cmd->data[0] = 0; // dummy value
#ifdef COMMERCIAL_KEY1
    cmd->data[2] = COMMERCIAL_KEY1;
#endif // COMMERCIAL_KEY1
    QItem_Enqueue(item);
    return item;
}

void zmMaster::handleTimeouts()
{
    int64_t dt;
    QueueItem_t *item;
    unsigned i;
    unsigned k;
    unsigned count = 0;

    if (Master.q_items_wait_confirm == 0)
        return;

    i = Master.q_item_wp - 1;

    const auto now = deCONZ::steadyTimeRef();

    for (k = 0; k < MAX_QUEUE_ITEMS && count < Master.q_items_wait_confirm; k++)
    {
        item = &Master.q_items[i % MAX_QUEUE_ITEMS];
        i--;

        if (item->state != QITEM_STATE_WAIT_CONFIRM)
            continue;

        count++;

        dt = (now.ref - item->tref_tx);

        if (dt > TimeoutDelay)
        {
            if (item->retries >= MaxSendRetry)
            {
                DBG_Printf(DBG_PROT, "command queue give up on cmd: %s, seq: %u\n", cmdToString(item->cmd.cmd), item->cmd.seq);
                killCommand(&item->cmd, ZM_STATE_TIMEOUT);
                QItem_Free(item);
                Master.cmd_fails++;

                if (Master.cmd_fails >= MaxCommandFails)
                {
                    DBG_Printf(DBG_PROT, "[Master] force reconnect\n");
                    comExit();
                    return;
                }
            }
            else
            {
                DBG_Printf(DBG_PROT, "[Master] timeout on cmd: %s, seq: %u (retry: %d)\n", cmdToString(item->cmd.cmd), item->cmd.seq, item->retries);
                item->retries++;
                item->state = QITEM_STATE_WAIT_SEND;
                Q_ASSERT(Master.q_items_wait_send < MAX_QUEUE_ITEMS);
                Master.q_items_wait_send++;

                Q_ASSERT(Master.q_items_wait_confirm > 0);
                Master.q_items_wait_confirm--;
                QItem_Send(item);
            }
        }
    }
}

deCONZ::State zmMaster::netState()
{
    if (!connected())
    {
        return deCONZ::UnknownState;
    }

    if (Master.zllState == ZLL_NET_TOUCHLINK)
    {
        return deCONZ::Touchlink;
    }

    switch (Master.status0 & ZM_STATUS_NET_STATE_MASK)
    {
    case ZM_NET_OFFLINE:
        return deCONZ::NotInNetwork;

    case ZM_NET_JOINING:
        return deCONZ::Connecting;

    case ZM_NET_ONLINE:
        return deCONZ::InNetwork;

    case ZM_NET_LEAVING:
        return deCONZ::Leaving;

    default:
        break;
    }

    return deCONZ::UnknownState;
}

bool zmMaster::hasFreeApsRequest()
{
    if (netState() == deCONZ::InNetwork && Master.q_items_wait_confirm < MaxUnconfirmed)
    {
        return QAPS_Full() ? false : true;

        //if (Master.status0 & ZM_STATUS_FREE_APS_SLOTS)
        //{
        //    return true;
        //}
    }

    return false;
}

const QString &zmMaster::devicePath() const
{
    if (m_serialCom && m_serialCom->isApplicationConnected())
    {
        return m_serialPort;
    }

    return m_emptyString;
}

const QString &zmMaster::deviceName() const
{
    return m_devName;
}

int zmMaster::rebootDevice()
{
    quint32 ttl = 2; // seconds
    return resetDeviceWatchdog(ttl);
}

int zmMaster::resetDeviceWatchdog(quint32 ttl)
{
    return writeParameter(ZM_DID_DEV_WATCHDOG_TTL, reinterpret_cast<quint8*>(&ttl), sizeof(ttl));
}

void zmMaster::joinNetwork()
{
    QueueItem_t *item;
    struct zm_command *cmd;

    if (connected())
    {
        item = QItem_Alloc();
        if (!item)
            return;

        cmd = &item->cmd;
        cmd->cmd = ZM_CMD_CHANGE_NET_STATE;
        cmd->data[0] = ZM_NET_ONLINE;
        QItem_Enqueue(item);
    }
}

void zmMaster::leaveNetwork()
{
    QueueItem_t *item;
    struct zm_command *cmd;

    if (connected())
    {
        item = QItem_Alloc();
        if (!item)
            return;

        cmd = &item->cmd;

        cmd->cmd = ZM_CMD_CHANGE_NET_STATE;
        cmd->data[0] = ZM_NET_OFFLINE;
        QItem_Enqueue(item);
    }
}

void zmMaster::startTaskTimer(zmMaster::MasterEvent event, int interval, int line)
{
    if (event == ACTION_PROCESS)
    {
        //DBG_Printf(DBG_INFO, "start task timer: line %d\n", line);
    }

    m_taskTimer->stop();
    m_taskTimerEvent = event;
    m_taskTimerLine = line;
    m_taskTimer->start(interval);
}

void zmMaster::checkStatus0(const uint8_t *status)
{
    ZM_NetState_t n0 = (ZM_NetState_t)(Master.status0 & ZM_STATUS_NET_STATE_MASK);
    ZM_NetState_t n1 = (ZM_NetState_t)(status[0] & ZM_STATUS_NET_STATE_MASK);

    Master.status0 = status[0];

    if ((Master.status0 & (ZM_STATUS_APS_DATA_CONF | ZM_STATUS_APS_DATA_IND)) != 0)
    {
        DBG_Printf(DBG_PROT, "[Master] dev-status0: conf: %u, free-slots: %u, ind: %u\n",
               (Master.status0 & ZM_STATUS_APS_DATA_CONF) ? 1 : 0,
               (Master.status0 & ZM_STATUS_FREE_APS_SLOTS) ? 1 : 0,
               (Master.status0 & ZM_STATUS_APS_DATA_IND) ? 1 : 0);
    }

    if (Master.status0 & ZM_STATUS_CONFIG_CHANGED)
    {
        DBG_Printf(DBG_INFO, "[Master] config changed, read parameters\n");
        readParameters();
    }

    if (n0 != n1)
    {
        switch (n1)
        {
        case ZM_NET_JOINING:
            deCONZ::setDeviceState(deCONZ::Connecting);
            break;

        case ZM_NET_ONLINE:
            deCONZ::setDeviceState(deCONZ::InNetwork);
            if (n0 == ZM_NET_JOINING)
            {
                readParameter(ZM_DID_NWK_PANID);
                readParameter(ZM_DID_NWK_NETWORK_ADDRESS);
            }
            break;

        case ZM_NET_LEAVING:
            deCONZ::setDeviceState(deCONZ::Leaving);
            break;

        case ZM_NET_OFFLINE:
            deCONZ::setDeviceState(deCONZ::NotInNetwork);
            if (n0 == ZM_NET_JOINING)
            {
                // check why connecting failed
                queGetStartNetworkConfirmStatus();
            }
            break;

        default:
            deCONZ::setDeviceState(deCONZ::UnknownState);
            break;
        }

        if (n0 != n1)
        {
            emit netStateChanged();
        }
    }
}

void zmMaster::checkStatus1(const uint8_t *status)
{
    if (status[0] & 0x80) // has byte 1
    {
        Master.status1 = status[1];
        uint8_t zllState = status[1] & 0x03;

        DBG_Assert(zllState <= ZLL_NET_CONNECTED);

        if ((zllState <= ZLL_NET_CONNECTED) &&
            (zllState != Master.zllState))
        {
            const char *st[] = {
                "NOT_CONNECTED", "TOUCHLINK", "CONNECTED"
            };
            Master.zllState = (ZLL_NetState_t)zllState;
            DBG_Printf(DBG_INFO, "ZLL State changed to %s\n", st[zllState]);
            emit netStateChanged();
        }

        uint8_t ipanState = ((status[1] & ZM_STATUS_INTERPAN_MASK) >> 3);

        DBG_Assert(ipanState <= IPAN_CONNECTED);

        if ((ipanState <= IPAN_CONNECTED) &&
            (ipanState != Master.ipanState))
        {
            const char *st[] = {
                "NOT_CONNECTED", "CONNECTING", "CONNECTED"
            };

            uint8_t ipanStateBefore = Master.ipanState;
            Master.ipanState = (IPAN_State_t)ipanState;

            if (ipanStateBefore == IPAN_CONNECTING)
            {
                if (ipanState == IPAN_CONNECTED)
                {
                    emit startInterpanModeConfirm(deCONZ::TouchlinkSuccess);
                }
                else if (ipanState == IPAN_NOT_CONNECTED)
                {
                    emit startInterpanModeConfirm(deCONZ::TouchlinkFailed);
                }
            }
            DBG_Printf(DBG_TLINK, "IPAN State changed to %s\n", st[ipanState]);
        }

        if (status[1] & 0x80) // debug assertion
        {
            DBG_Printf(DBG_INFO, "stack has debug assertion\n");
        }
    }
}

/*!
    Cleanup queue and notify higher layers.
 */
void zmMaster::killCommandQueue()
{
    unsigned i;

    QueueItem_t *item;

    for (i = 0; i < MAX_QUEUE_ITEMS; i++)
    {
        item = &Master.q_items[i];
        if (item->state != QITEM_STATE_INIT)
        {
            killCommand(&item->cmd, ZM_STATE_ERROR);
            QItem_Free(item);
        }
    }

    Master.q_item_sp = 0;
    Master.q_item_wp = 0;

    Q_ASSERT(Master.q_items_wait_confirm == 0);
    Q_ASSERT(Master.q_items_wait_send == 0);

    while (QAPS_Empty() == 0)
    {
        i = QAPS_Pop();
        DBG_Assert(i != UINT16_MAX);
        emit apsdeDataRequestDone(i, ZM_STATE_ERROR);
    }

    Master.q_aps_rp = 0;
    Master.q_aps_wp = 0;
}

void zmMaster::bootloaderStarted()
{
    m_bootloaderStarted++;

    if (m_devFirmwareVersion != 0)
    {
        return;
    }

    const deCONZ::DeviceEnumerator *e = deCONZ::DeviceEnumerator::instance();
    if (!e /*|| !e->listSerialPorts()*/)
    {
        return; // something went wrong
    }

    for (const auto &dev : e->getList())
    {
        if (dev.path != m_serialPort)
        {
            continue;
        }

        if (dev.friendlyName == QLatin1String("ConBee II"))
        {
            m_devFirmwareVersion = FW_ONLY_R21_BOOTLOADER;
            return;
        }

        if (dev.friendlyName == QLatin1String("ConBee") ||
            dev.friendlyName == QLatin1String("RaspBee"))
        {
            m_devFirmwareVersion = FW_ONLY_AVR_BOOTLOADER;
            return;
        }
    }
}

/*!
    Notify higher layers that a command is not processed.
 */
void zmMaster::killCommand(const struct zm_command *cmd, ZM_State_t state)
{
    DBG_Printf(DBG_PROT, "[Master] kill cmd %s (%s)\n", cmdToString(cmd->cmd), protocol_strstate(state));

    switch (cmd->cmd)
    {
    case ZM_CMD_READ_PARAM:
        deCONZ::controller()->readParameterResponse(state, (ZM_DataId_t)cmd->data[0], 0, 0);
        break;

    case ZM_CMD_WRITE_PARAM:
    {
        emit writeParameterDone(cmd->buffer.data[0], state);
    }
        break;

    case ZM_CMD_APS_DATA_REQ:
    case ZM_CMD_APS_DATA_REQ_2:
    {
        emit apsdeDataRequestDone(cmd->buffer.data[0], state);
    }
        break;

    case ZM_CMD_START_INTERPAN_MODE:
    {
        emit startInterpanModeConfirm(deCONZ::TouchlinkFailed);
    }
        break;

    case ZM_CMD_SEND_INTERPAN_REQ:
    {
        emit sendInterpanConfirm(deCONZ::TouchlinkFailed);
    }
        break;

    case ZM_CMD_INTERPAN_CONFIRM:
    {
        emit sendInterpanConfirm(deCONZ::TouchlinkFailed);
    }
        break;

    default:
        break;
    }
}

void zmMaster::setState(zmMaster::MasterState state)
{
    if (m_state != state)
    {
        DBG_Printf(DBG_PROT_L2, "[Master] setState state: %c -> %c\n", m_state, state);
        m_state = state;
    }
}

/*!
    Write a parameter to the device.

    \returns 0 if the request is send to device.
            -1 if the request can't be processed.
            -2 length is too long.
 */
int zmMaster::writeParameter(ZM_DataId_t id, const uint8_t *data, uint8_t length)
{
    QueueItem_t *item;
    struct zm_command *cmd;

    if (connected())
    {
        if (!data ||  (length == 0))
            return -1;

        item = QItem_Alloc();
        if (!item)
            return -1;

        if (length > (ZM_MAX_BUFFER_LEN - 1))
            return -2;

        cmd = &item->cmd;
        cmd->cmd = ZM_CMD_WRITE_PARAM;
        cmd->buffer.len = 1 + length;
        cmd->buffer.data[0] = (uint8_t)id;
        memcpy(cmd->buffer.data + 1, data, length);
        QItem_Enqueue(item);

        DBG_Printf(DBG_PROT, "[Master] write param req param: 0x%02X\n", cmd->buffer.data[0]);
        return 0;
    }

    return -1;
}

int zmMaster::verifyChildNode(const deCONZ::Address &address, quint8 macCapabilities)
{
    QueueItem_t *item;
    struct zm_command *cmd;

    if (connected())
    {
        if (!address.hasExt() || !address.hasNwk())
        {
            return -1;
        }

        if ((macCapabilities & deCONZ::MacDeviceIsFFD) != 0)
        {
            return -1; // FFD not supported
        }

        if (m_devProtocolVersion < DECONZ_PROTOCOL_VERSION_1_7 && (macCapabilities & deCONZ::MacReceiverOnWhenIdle) != 0)
        {
            return -1;
        }

        item = QItem_Alloc();
        if (!item)
            return -1;

        cmd = &item->cmd;
        cmd->cmd = ZM_CMD_UPDATE_NEIGHBOR;
        cmd->buffer.len = 1 + 2 + 8 + 1;
        cmd->buffer.data[0] = 1; // action:add

        uint8_t *p = cmd->buffer.data + 1;
        uint16_t nwk = address.nwk();
        uint64_t ext = address.ext();

        p = put_u16_le(p, &nwk);
        p = put_u64_le(p, &ext);
        *p++ = macCapabilities;

        QItem_Enqueue(item);

        DBG_Printf(DBG_PROT, "[Master] verify child node: " FMT_MAC "\n", FMT_MAC_CAST(address.ext()));
        return 0;
    }

    return -1;
}

int zmMaster::forceRejoinChildNode(const deCONZ::Address &address)
{
    QueueItem_t *item;
    struct zm_command *cmd;

    if (connected())
    {
        if (!address.hasExt() || !address.hasNwk())
            return -1;

        item = QItem_Alloc();
        if (!item)
            return -1;

        cmd = &item->cmd;
        cmd->cmd = ZM_CMD_UPDATE_NEIGHBOR;
        cmd->buffer.len = 1 + 2 + 8;
        cmd->buffer.data[0] = 3; // action: force rejoin

        uint8_t *p = cmd->buffer.data + 1;
        uint16_t nwk = address.nwk();
        uint64_t ext = address.ext();

        p = put_u16_le(p, &nwk);
        p = put_u64_le(p, &ext);

        Q_UNUSED(p);

        QItem_Enqueue(item);

        DBG_Printf(DBG_PROT, "[Master] force rejoin child node: " FMT_MAC "\n", FMT_MAC_CAST(address.ext()));
        return 0;
    }

    return -1;
}

/*!
    Read all parameters from device.

    \returns 0 if the request is send to device.
            -1 if the request can't be processed.
 */
int zmMaster::readParameters()
{
    if (connected())
    {
        readParameter(ZM_DID_STK_PROTOCOL_VERSION);
        readParameter(ZM_DID_NWK_NETWORK_ADDRESS);
        readParameter(ZM_DID_MAC_ADDRESS);
        readParameter(ZM_DID_NWK_PANID);
        readParameter(ZM_DID_NWK_EXTENDED_PANID);
        readParameter(ZM_DID_APS_CHANNEL_MASK);
        readParameter(ZM_DID_APS_DESIGNED_COORDINATOR);
        readParameter(ZM_DID_APS_TRUST_CENTER_ADDRESS);
        readParameter(ZM_DID_APS_USE_INSECURE_JOIN);
        readParameter(ZM_DID_STK_SECURITY_MODE);
        readParameter(ZM_DID_APS_USE_EXTENDED_PANID);
        readParameter(ZM_DID_STK_PREDEFINED_PANID);
        readParameter(ZM_DID_STK_CURRENT_CHANNEL);
        readParameter(ZM_DID_STK_CONNECT_MODE);
        readParameter(ZM_DID_STK_PERMIT_JOIN);
        readParameter(ZM_DID_STK_NWK_UPDATE_ID);
        readParameter(ZM_DID_STK_ANT_CTRL);
        readParameter(ZM_DID_STK_NO_ZDP_RESPONSE);
        //readParameter(ZM_DID_ZLL_KEY);
        //readParameter(ZM_DID_ZLL_FACTORY_NEW);
        uint8_t keyNum = 0;
        readParameterWithArg(ZM_DID_STK_NETWORK_KEY, &keyNum, 1);


#if 0
        // test reading link key for index 0-3
        keyNum = 0;
        readParameterWithArg(ZM_DID_STK_KEY_FOR_INDEX, &keyNum, 1);
        keyNum = 1;
        readParameterWithArg(ZM_DID_STK_KEY_FOR_INDEX, &keyNum, 1);
        keyNum = 2;
        readParameterWithArg(ZM_DID_STK_KEY_FOR_INDEX, &keyNum, 1);
        keyNum = 3;
        readParameterWithArg(ZM_DID_STK_KEY_FOR_INDEX, &keyNum, 1);
#endif
        uint8_t idx = 0;
        readParameterWithArg(ZM_DID_STK_ENDPOINT, &idx, 1);
        idx = 1;
        readParameterWithArg(ZM_DID_STK_ENDPOINT, &idx, 1);
        idx = 2;
        readParameterWithArg(ZM_DID_STK_ENDPOINT, &idx, 1);

        readParameter(ZM_DID_STK_STATIC_NETWORK_ADDRESS);
        readParameter(ZM_DID_STK_SECURITY_MATERIAL0);

        if (m_devProtocolVersion >= DECONZ_PROTOCOL_VERSION_1_12)
        {
            readParameter(ZM_DID_STK_DEBUG);
        }

        return 0;
    }

    return -1;
}

int zmMaster::readParameter(ZM_DataId_t id)
{
    QueueItem_t *item;
    struct zm_command *cmd;

    if (connected())
    {
        item = QItem_Alloc();
        if (!item)
            return -1;

        cmd = &item->cmd;
        cmd->cmd = ZM_CMD_READ_PARAM;
        cmd->buffer.len = 1;
        cmd->buffer.data[0] = (uint8_t)id;
        QItem_Enqueue(item);

        DBG_Printf(DBG_PROT, "[Master] read parameter 0x%02X\n", id);
        return 0;
    }

    return -1;
}

int zmMaster::readParameterWithArg(ZM_DataId_t id, const uint8_t *data, uint8_t length)
{
    QueueItem_t *item;
    struct zm_command *cmd;

    if (connected())
    {
        item = QItem_Alloc();
        if (!item)
            return -1;

        cmd = &item->cmd;
        cmd->cmd = ZM_CMD_READ_PARAM;
        cmd->buffer.len = 1 + length;
        cmd->buffer.data[0] = (uint8_t)id;
        for (uint i = 0; i < length; i++)
        {
            cmd->buffer.data[i + 1] = data[i];
        }

        DBG_Printf(DBG_INFO_L2, "[Master] read param with arg 0x%02X\n", id);

        QItem_Enqueue(item);
        return 0;
    }

    return -1;
}
