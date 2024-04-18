/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_GENDPOINTBOX_H
#define ZM_GENDPOINTBOX_H

#include <QGraphicsWidget>

#include "deconz/zcl.h"

class zmgCluster;
class zmgNode;

namespace deCONZ {
    class zmNode;
    class SimpleDescriptor;
    class ZclCluster;
}

class zmgEndpointBox : public QGraphicsWidget
{
public:
    zmgEndpointBox(QGraphicsItem *parent = 0);
    void updateEndpoints(zmgNode *node);
    int endpointSize() const { return m_endpoints.size(); }
    zmgCluster *getCluster(quint8 endpoint, quint16 cluster, deCONZ::ZclClusterSide side);
    void endpointDoubleClicked(quint8 endpoint);
    void clusterClicked(quint8 endpoint, quint16 clusterId, deCONZ::ZclClusterSide clusterSide);
    void clusterDoubleClicked(bool scrollAttr);
    void clear();
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
    zmgNode *node() const { return m_node; }

protected:

private:
    void addCluster(const deCONZ::ZclCluster &cl, int clusterSide, const deCONZ::SimpleDescriptor &sd);
    void addEndpoint(const deCONZ::SimpleDescriptor &sd);
    void removeItem(QGraphicsLayoutItem *item);
    zmgNode *m_node;
    QList<int> m_endpoints;
    QList<zmgCluster*> m_clusters;
};

#endif // ZM_GENDPOINTBOX_H
