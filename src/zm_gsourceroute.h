/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_GSOURCE_ROUTE_H
#define ZM_GSOURCE_ROUTE_H

#include <QObject>
#include <QGraphicsPathItem>
#include "zm_gnode.h"

class zmgSourceRoute : public QObject,
                       public QGraphicsPathItem
{
    Q_OBJECT

public:
    zmgSourceRoute(uint srHash, const std::vector<zmgNode*> &nodes, QObject *parent);
    uint uuidHash() const { return m_srHash; }

public Q_SLOTS:
    void updatePath();

private:
    int m_gradientCheck = -1;
    QPainterPath m_path;
    uint m_srHash = 0;
    std::vector<zmgNode*> m_nodes;
};

#endif // ZM_GSOURCE_ROUTE_H
