/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <vector>
#include <cmath>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <deconz/util.h>
#include <deconz/zdp_descriptors.h>
#include <deconz/zdp_profile.h>

#include "db_nodes.h"
#include "db_json_nodes.h"

/*
  {
    "ExtAddress": "0x00212effff048209",
    "NodeDescriptor": "EEAPXxFHKwBBKisAAA==",
    "NwkAddress": "0x0000",
    "SceneX": 188.485,
    "SceneY": -545.927,
    "SimpleDescriptors": [
      "AQQBBQABAwAACgAZAAMBACAAAAU=",
      "8uChZAABAAEhAA=="
    ],
    "UserDescriptor": ""
  }

*/
static DB_Node DB_GetNodeJson(const QJsonObject &obj)
{
    DB_Node result;
    bool ok = false;

    result.extAddr = obj.value("ExtAddress").toString().toULongLong(&ok, 16);
    result.nwkAddr = obj.value("NwkAddress").toString().toUShort(&ok, 16);
    result.sceneX = obj.value("SceneX").toDouble();
    result.sceneY = obj.value("SceneY").toDouble();

    if (result.extAddr == 0 || result.nwkAddr == 0) // it's ok to leave coordinator out
    {
        return { };
    }

    {
        DB_Descriptor nodeDescriptor;
        nodeDescriptor.type = ZDP_NODE_DESCRIPTOR_CLID;
        nodeDescriptor.data = QByteArray::fromBase64(obj.value("NodeDescriptor").toString().toLatin1());

        if (nodeDescriptor.data.isEmpty())
        {
            return { };
        }

        result.rawDescriptors.push_back(std::move(nodeDescriptor));
    }

    const auto sds = obj.value("SimpleDescriptors").toArray();

    for (const auto &val : sds)
    {
        DB_Descriptor simpleDescriptor;
        simpleDescriptor.type = ZDP_SIMPLE_DESCRIPTOR_CLID;
        simpleDescriptor.data = QByteArray::fromBase64(val.toString().toLatin1());

        if (!simpleDescriptor.data.isEmpty())
        {
            result.rawDescriptors.push_back(std::move(simpleDescriptor));
        }
    }

    if (!DB_ParseDescriptors(&result))
    {
        return { };
    }

    return result;
}

std::vector<DB_Node> DB_LoadNodesJson()
{
    std::vector<DB_Node> result;


    QFile file(deCONZ::getStorageLocation(deCONZ::NodeCacheLocation));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return result;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (err.error != QJsonParseError::NoError)
    {
        return result;
    }

    if (!doc.isArray())
    {
        return result;
    }

    const auto arr = doc.array();

    for (const auto &val : arr)
    {
        const auto node = DB_GetNodeJson(val.toObject());
        if (node.extAddr != 0)
        {
            result.push_back(std::move(node));
        }
    }

    return result;
}
