/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QPen>
#include "gui/theme.h"
#include "zm_gsourceroute.h"
#include "zm_node.h"

zmgSourceRoute::zmgSourceRoute(uint srHash, const std::vector<zmgNode*> &nodes, QObject *parent) :
    QObject(parent),
    QGraphicsPathItem(),
    m_srHash(srHash),
    m_nodes(nodes)
{
    setAcceptedMouseButtons(Qt::NoButton);

    for (auto *node : m_nodes)
    {
        connect(node, &zmgNode::moved, this, &zmgSourceRoute::updatePath);
    }

    updatePath();
}

void zmgSourceRoute::updatePath()
{
    if (m_nodes.empty())
    {
        return;
    }

    QPainterPath path;

    path.moveTo(mapFromItem(m_nodes.front(), m_nodes.front()->boundingRect().center()));

    const deCONZ::SourceRoute *sr = nullptr;
    auto *node = m_nodes.back();
    Q_ASSERT(node);
    if (node->data() && !node->data()->sourceRoutes().empty())
    {
        sr = &node->data()->sourceRoutes().back();
    }

    for (size_t i = 1; i < m_nodes.size(); i++)
    {
        path.lineTo(mapFromItem(m_nodes.at(i), m_nodes.at(i)->boundingRect().center()));
    }

    int gradientCheck = (!sr || sr->txOk() == 0) ? 1 : 0;

    if (m_path != path || m_gradientCheck != gradientCheck)
    {
        m_path = path;
        m_gradientCheck = gradientCheck;

        QLinearGradient gradient;
        QColor startColor = Theme_Color(ColorSourceRouteStart);
        QColor endColor = Theme_Color(ColorSourceRouteEnd);

        if (!sr || sr->txOk() == 0)
        {
            const int bri = startColor.lightness();
            const QColor gray(bri, bri, bri, 64);
            gradient.setColorAt(0, gray);
            gradient.setColorAt(1, gray.darker(110));
        }
        else
        {
            gradient.setColorAt(0, startColor);
            gradient.setColorAt(0.7, startColor);
            gradient.setColorAt(0.95, endColor);
            gradient.setColorAt(1, endColor);
        }

        gradient.setStart(path.pointAtPercent(0));
        gradient.setFinalStop(path.pointAtPercent(1));

        prepareGeometryChange();
        QPen pen(gradient, 1.5);
        //    pen.setStyle(Qt::DotLine);
        setPen(pen);
        setPath(path);
        //update();
    }
}
