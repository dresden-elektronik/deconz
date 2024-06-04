/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <cassert>
#include <cmath>
#include <QGraphicsScene>
#include <QTimer>
#include <sqlite3.h>
#include <deconz/dbg_trace.h>
#include <deconz/types.h>
#include <deconz/zdp_descriptors.h>
#include <deconz/zdp_profile.h>
#include <deconz/util.h>
#include <deconz/node_event.h>

#include "zm_node_model.h"
#include "zm_controller.h"
#include "db_nodes.h"
#include "db_json_nodes.h"

static sqlite3 *db = nullptr;

bool openDb()
{
    if (db) // already open
        return true;

    int rc = 0;
    QString dataPath = deCONZ::getStorageLocation(deCONZ::ApplicationsDataLocation);
    QString sqliteDatabaseName = dataPath + QLatin1String("/zll.db");

    rc = sqlite3_open(qPrintable(sqliteDatabaseName), &db);

    DBG_Assert(rc == SQLITE_OK);
    if (rc != SQLITE_OK)
        return false;

    const char *sql = "PRAGMA foreign_keys = ON"; // must be enabled at runtime for each connection
    rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    DBG_Assert(rc == SQLITE_OK);
    if (rc == SQLITE_OK)
        return true;

    DBG_Printf(DBG_ERROR, "CTRL can't open database: %s\n", sqlite3_errmsg(db));
    return false;
}

bool closeDb()
{
    if (!db)
        return false;

    int rc = sqlite3_close(db);
    DBG_Assert(rc == SQLITE_OK || rc == SQLITE_BUSY);
    if (rc == SQLITE_BUSY)
        return false; // close later

    db = nullptr;
    return true;
}

bool DB_ExistsRestDevice(quint64 extAddr)
{
    bool dbWasOpen = db != nullptr;
    if (!dbWasOpen && !openDb())
    {
        return true;
    }

    int count = 0;

    char uniqueId[23 + 1];
    generateUniqueId2(extAddr, uniqueId, sizeof(uniqueId));
    assert(uniqueId[23] == '\0');

    {
        char sql[160];
        int rc = snprintf(sql, sizeof(sql), "select id from nodes where mac LIKE '%s%%' AND state = 'normal'", uniqueId);
        assert(size_t(rc) < sizeof(sql));
        if (size_t(rc) >= sizeof(sql))
        {
            return false;
        }

        sqlite3_stmt *stmt = nullptr;

        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        DBG_Assert(stmt);
        DBG_Assert(rc == SQLITE_OK);

        if (rc == SQLITE_OK)
        {
            rc = sqlite3_step(stmt);
            while (rc == SQLITE_ROW)
            {
                count++;
                break;
            }
        }

        if (stmt)
        {
            rc = sqlite3_finalize(stmt);
            DBG_Assert(rc == SQLITE_OK);
        }
    }

    if (count == 0) // also check sensors
    {
        char sql[160];
        int rc = snprintf(sql, sizeof(sql), "select sid from sensors where uniqueid LIKE '%s%%' AND deletedState = 'normal'", uniqueId);
        assert(size_t(rc) < sizeof(sql));
        if (size_t(rc) >= sizeof(sql))
        {
            return false;
        }

        sqlite3_stmt *stmt = nullptr;

        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        DBG_Assert(stmt);
        DBG_Assert(rc == SQLITE_OK);

        if (rc == SQLITE_OK)
        {
            rc = sqlite3_step(stmt);
            while (rc == SQLITE_ROW)
            {
                count++;
                break;
            }
        }

        if (stmt)
        {
            rc = sqlite3_finalize(stmt);
            DBG_Assert(rc == SQLITE_OK);
        }
    }

    if (!dbWasOpen)
    {
        closeDb();
    }

    return count > 0;

}

static quint64 DB_ParseMacAddress(const unsigned char *mac, int len)
{
    Q_ASSERT(mac);
    DBG_Assert(len == 23);
    if (len != 23)
        return 0;

    char buf[23 + 1], *p = buf;
    for (int i = 0; i < len; i++)
    {
        if (mac[i] == ':')
            continue;
        *p++ = mac[i];
    }
    *p = '\0';

    errno = 0;
    auto extAddr = strtoull(buf, nullptr, 16);
    DBG_Assert(errno == 0);
    if (errno)
    {
        return 0;
    }

    return extAddr;
}

bool DB_ParseDescriptors(DB_Node *node)
{
    // Node Descriptor first
    for (const auto &descriptor : node->rawDescriptors)
    {
        QDataStream stream(descriptor.data);
        stream.setByteOrder(QDataStream::LittleEndian);

        if (descriptor.type == ZDP_NODE_DESCRIPTOR_CLID)
        {
            node->nodeDescriptor.readFromStream(stream);
            DBG_Assert(!node->nodeDescriptor.isNull());
        }
    }

    for (const auto &descriptor : node->rawDescriptors)
    {
        QDataStream stream(descriptor.data);
        stream.setByteOrder(QDataStream::LittleEndian);

        if (descriptor.type == ZDP_SIMPLE_DESCRIPTOR_CLID)
        {
            deCONZ::SimpleDescriptor sd;
            sd.readFromStream(stream, node->nodeDescriptor.manufacturerCode());
            DBG_Assert(sd.isValid());
            if (sd.isValid())
            {
                node->simpleDescriptors.push_back(std::move(sd));
            }
        }

    }

    return !node->nodeDescriptor.isNull();
}

static NodeInfo DB_CreateNodeInfo(const DB_Node &dbNode, int nodeId)
{
    NodeInfo node;
    Q_ASSERT(dbNode.extAddr != 0);
    DBG_Assert(!dbNode.nodeDescriptor.isNull());

    node.id = static_cast<quint32>(nodeId);
    node.data = new deCONZ::zmNode(dbNode.nodeDescriptor.macCapabilities());
    node.g = new zmgNode(node.data, nullptr);

    node.data->setNodeDescriptor(dbNode.nodeDescriptor);
    node.data->setFetched(deCONZ::ReqNodeDescriptor, true);

    deCONZ::Address addr;

    addr.setExt(dbNode.extAddr);
    if (dbNode.nwkAddr >= 0)
        addr.setNwk(static_cast<quint16>(dbNode.nwkAddr));

    node.data->setAddress(addr);

    QPointF p;
    p.setX(dbNode.sceneX);
    p.setY(dbNode.sceneY);
    node.g->setPos(p);
    node.g->updateParameters(node.data);
    node.g->show();
    node.g->requestUpdate();

    for (const auto &sd : dbNode.simpleDescriptors)
    {
        DBG_Assert(sd.isValid());

        node.data->setSimpleDescriptor(sd);
        node.g->updated(deCONZ::ReqSimpleDescriptor);
        node.g->requestUpdate();
    }

    return node;
}

std::vector<DB_Node> DB_LoadNodes()
{
    std::vector<DB_Node> result;

    auto jsonNodes = DB_LoadNodesJson();

    if (!openDb())
    {
        return result;
    }

    // device || descriptors || gui position

    const char *sql = "SELECT"
                      " devices.id AS device_id,"
                      " devices.mac,"
                      " devices.nwk,"
                      " device_descriptors.endpoint,"
                      " device_descriptors.type,"
                      " device_descriptors.data,"
                      " device_gui.scene_x,"
                      " device_gui.scene_y"
                      " FROM devices"
                      " INNER JOIN device_gui ON devices.id = device_gui.device_id"
                      " INNER JOIN device_descriptors ON devices.id = device_descriptors.device_id"
                      " ORDER BY"
                      " devices.nwk,"
                      " device_id,"
                      " device_descriptors.endpoint";

    int rc = -1;
    sqlite3_stmt *stmt = nullptr;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    DBG_Assert(stmt);
    DBG_Assert(rc == SQLITE_OK);

    if (rc == SQLITE_OK)
    {
        DB_Node node2;
        int curNodeId = -1;

        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            int nodeId = sqlite3_column_int(stmt, 0);

            if (curNodeId != nodeId) // next device
            {
                if (curNodeId >= 0 && DB_ParseDescriptors(&node2))
                {
                    result.push_back(node2);
                }

                node2 = { };
                curNodeId = nodeId;
                node2.sceneX = sqlite3_column_double(stmt, 6);
                node2.sceneY = sqlite3_column_double(stmt, 7);

                const auto *mac = sqlite3_column_text(stmt, 1);
                node2.extAddr = DB_ParseMacAddress(mac, sqlite3_column_bytes(stmt, 1));
                DBG_Printf(DBG_INFO_L2, "Node: id: %d, %s (0x%016llX) scene: %f, %f\n", curNodeId, mac, node2.extAddr, node2.sceneX, node2.sceneY);
            }

            if (sqlite3_column_type(stmt, 2) == SQLITE_INTEGER) // might be NULL
            {
                node2.nwkAddr = sqlite3_column_int(stmt, 2);
            }
            else
            {
                DBG_Assert(sqlite3_column_type(stmt, 2) == SQLITE_NULL);
            }

            DB_Descriptor descriptor;
            //int endpoint = sqlite3_column_int(stmt, 3);
            descriptor.type = sqlite3_column_int(stmt, 4);

            const auto descriptorData = sqlite3_column_blob(stmt, 5);
            const auto descriptorSize = sqlite3_column_bytes(stmt, 5);

            if (!descriptorData || descriptorSize <= 0)
                continue;

            descriptor.data = QByteArray(static_cast<const char*>(descriptorData), descriptorSize);
            node2.rawDescriptors.push_back(descriptor);
        }

        if (curNodeId >= 0 && DB_ParseDescriptors(&node2)) // last node, or just one node
        {
            result.push_back(node2);
        }

        DBG_Assert(rc == SQLITE_DONE);
    }

    if (stmt)
    {
        rc = sqlite3_finalize(stmt);
        DBG_Assert(rc == SQLITE_OK);
    }

    closeDb();

    // (1) cleanup json nodes which already exist in database
    for (auto &node : result)
    {
        const auto i = std::find_if(jsonNodes.cbegin(), jsonNodes.cend(), [&node](const DB_Node &jsonNode)
        {
            return node.extAddr == jsonNode.extAddr;
        });

        if (i != jsonNodes.end())
        {
            if (node.nodeDescriptor.isNull() && !i->nodeDescriptor.isNull())
            {
                node.nodeDescriptor = i->nodeDescriptor; // borrow from json node
            }
            jsonNodes.erase(i);
        }
    }

    // (2) cleanup remaining json nodes with no REST node reference
    while (1)
    {
        const auto i = std::find_if(jsonNodes.cbegin(), jsonNodes.cend(), [](const DB_Node &jsonNode)
        {
            return !DB_ExistsRestDevice(jsonNode.extAddr);
        });

        if (i == jsonNodes.cend())
        {
            break;
        }

        jsonNodes.erase(i);
    }

    // (3) add valid json nodes which aren't in the database yet
    if (!jsonNodes.empty())
    {
        result.insert(result.end(), jsonNodes.begin(), jsonNodes.end());
    }

    return result;
}

void zmController::loadNodesFromDb()
{
    std::vector<DB_Node> nodes = DB_LoadNodes();

    for (const auto &dbNode : nodes)
    {
        const auto i = std::find_if(m_nodes.cbegin(), m_nodes.cend(), [&dbNode](const NodeInfo &n)
        {
            return dbNode.extAddr == n.data->address().ext();
        });

        if (i != m_nodes.cend())
        {
            continue; // already exist
        }

        auto node = DB_CreateNodeInfo(dbNode, m_nodes.size() + 1);
        Q_ASSERT(node.data);
        connect(node.g, &zmgNode::contextMenuRequest, this, &zmController::onNodeContextMenuRequest);
        connect(node.g, SIGNAL(moved()), this, SLOT(queueSaveNodesState()));
        if (!node.g->scene())
        {
            m_scene->addItem(node.g);
        }

        if (node.data->nodeDescriptor().macCapabilities() & deCONZ::MacDeviceIsFFD)
        {
            if (node.data->endpoints().empty())
            {
                node.data->setFetchItemEnabled(deCONZ::ReqActiveEndpoints, true);
                node.data->setFetched(deCONZ::ReqActiveEndpoints, false);
                node.data->setFetched(deCONZ::ReqSimpleDescriptor, false);
            }
            else
            {
                node.data->setFetchItemEnabled(deCONZ::ReqActiveEndpoints, false);
                node.data->setFetched(deCONZ::ReqActiveEndpoints, true);
                node.data->setFetched(deCONZ::ReqSimpleDescriptor, true);
            }
        }

        deCONZ::nodeModel()->addNode(dbNode.extAddr, dbNode.nwkAddr);
        m_nodes.push_back(node);
    }

    for (auto &node : m_nodes)
    {
        if (!node.data || !node.g)
            continue;

        emit nodeEvent({deCONZ::NodeEvent::NodeAdded, node.data});

        if (!node.data->nodeDescriptor().isNull())
        {

            emit nodeEvent({deCONZ::NodeEvent::UpdatedNodeDescriptor, node.data, ZDO_ENDPOINT, ZDP_PROFILE_ID, ZDP_NODE_DESCRIPTOR_CLID});
        }

        if (node.data->powerDescriptor().isValid())
        {
            emit nodeEvent({deCONZ::NodeEvent::UpdatedPowerDescriptor, node.data, ZDO_ENDPOINT, ZDP_PROFILE_ID, ZDP_POWER_DESCRIPTOR_CLID});
        }

        for (const auto &sd : node.data->simpleDescriptors())
        {
            emit nodeEvent({deCONZ::NodeEvent::UpdatedSimpleDescriptor, node.data, sd.endpoint()});
        }
    }

    emit nodesRestored();
}

void zmController::saveNodesState()
{
    if (m_saveNodesChanges == 0)
    {
        return;
    }

    if (m_restPlugin)
    {
        bool allowSave = false;
        QMetaObject::invokeMethod(m_restPlugin, "dbSaveAllowed", Qt::DirectConnection, Q_RETURN_ARG(bool, allowSave));
        if (!allowSave)
        {
            return;
        }
    }

    if (m_otauActivity > 0)
    {
        DBG_Printf(DBG_INFO_L2, "don't save node state while OTA busy\n");
        return;
    }

    int rc = 0;

    if (!openDb())
    {
        DBG_Printf(DBG_ERROR, "CTRL failed save nodes state, can't open db\n");
        return;
    }

    rc = sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
    DBG_Assert(rc == SQLITE_OK);

    sqlite3_stmt *stmt = nullptr;

    if (rc == SQLITE_OK)
    {
        const char *sql = "INSERT OR REPLACE INTO device_gui"
                          " (device_id, scene_x, scene_y)"
                          " SELECT id, ?1, ?2"
                          " FROM devices WHERE mac = ?3";
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        DBG_Assert(stmt);
        DBG_Assert(rc == SQLITE_OK);
    }

    for (NodeInfo &node : m_nodes)
    {
        if (rc != SQLITE_OK) // previous command must succeed
            break;

        if (!node.g || !node.data)
            continue;

        if (node.g->needSaveToDatabase())
        {
            char mac[23 + 1];
            generateUniqueId2(node.data->address().ext(), mac, sizeof(mac));
            assert(mac[23] == '\0');

            DBG_Printf(DBG_INFO_L2, "CTRL db store gui node %s\n", mac);

            if (rc == SQLITE_OK)
            {
                rc = sqlite3_bind_double(stmt, 1, node.g->pos().x());
                DBG_Assert(rc == SQLITE_OK);
            }

            if (rc == SQLITE_OK)
            {
                rc = sqlite3_bind_double(stmt, 2, node.g->pos().y());
                DBG_Assert(rc == SQLITE_OK);
            }

            if (rc == SQLITE_OK)
            {
                rc = sqlite3_bind_text(stmt, 3, mac, -1, SQLITE_STATIC);
                DBG_Assert(rc == SQLITE_OK);
            }

            if (rc == SQLITE_OK)
            {
                rc = sqlite3_step(stmt);
                DBG_Assert(rc == SQLITE_DONE);
            }

            if (rc != SQLITE_DONE)
            {
                DBG_Printf(DBG_ERROR, "CTRL db fail to store gui node %s: %s\n", mac, sqlite3_errmsg(db));
                break;
            }

            rc = sqlite3_reset(stmt);
            DBG_Assert(rc == SQLITE_OK);

            if (rc != SQLITE_OK)
                break;

            rc = sqlite3_clear_bindings(stmt);
            DBG_Assert(rc == SQLITE_OK);

            if (rc != SQLITE_OK)
                break;

            node.g->setNeedSaveToDatabase(false);
        }
    }

    if (stmt)
    {
        rc = sqlite3_finalize(stmt);
        DBG_Assert(rc == SQLITE_OK);
    }

    rc = sqlite3_exec(db, "COMMIT TRANSACTION", nullptr, nullptr, nullptr);
    DBG_Assert(rc == SQLITE_OK);
    DBG_Assert(rc != SQLITE_BUSY); // TODO handle

    closeDb();

    QElapsedTimer t;
    t.start();

    m_saveNodesChanges = 0;
    Q_ASSERT(m_saveNodesTimer->interval() > 0);
    m_saveNodesTimer->start();

    DBG_Printf(DBG_INFO, "saved node state in %d ms\n", int(t.elapsed()));

#ifdef Q_OS_LINUX
    t.restart();
    //sync();
    DBG_Printf(DBG_INFO, "sync() in %d ms\n", int(t.elapsed()));
#endif
}
