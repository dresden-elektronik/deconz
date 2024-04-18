/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef DB_NODES_H
#define DB_NODES_H

struct DB_Descriptor
{
    int type;
    QByteArray data;
};

struct DB_Node
{
    quint64 extAddr = 0;
    int nwkAddr = -1;
    double sceneX = 0;
    double sceneY = 0;
    std::vector<DB_Descriptor> rawDescriptors;
    deCONZ::NodeDescriptor nodeDescriptor;
    std::vector<deCONZ::SimpleDescriptor> simpleDescriptors;
};

bool openDb();
bool closeDb();
bool DB_ExistsRestDevice(quint64 extAddr);
bool DB_ParseDescriptors(DB_Node *node);

#endif // DB_NODES_H
