/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef NODE_LINK_H
#define NODE_LINK_H

#include <QColor>
#include <QString>
#include <QPainterPath>
#include <QPen>

class NodeSocket;

class NodeLink
{
public:
    enum LinkType
    {
        LinkNormal,
        LinkBinding,
        LinkRouting,
    };

    NodeLink(NodeSocket *src, NodeSocket *dst);
    ~NodeLink();

    void setSockets(NodeSocket *src, NodeSocket *dst);
    void setValue(qreal age);
    void setLinkType(LinkType type);
    LinkType linkType() const { return m_linkType; }
    void setMiddleText(const QString &text);
    const QString &middleText() const { return m_middleText; }

    NodeSocket *src() { return source; }
    NodeSocket *dst() { return dest; }

    void remSocket(NodeSocket *socket);

    bool connectsSockets(NodeSocket *a, NodeSocket *b);
    QRectF boundingRect() const;

    void updatePosition();
    bool isVisible() const { return m_visible; }
    void hide();
    void setVisible(bool visible);
    const QPen &pen() const { return m_pen; }
    const QPainterPath &path() const { return m_path; }

    QPointF m_p0;
    QPointF m_p1;
    QPointF m_p2;
    QPointF m_p3;
    QColor m_color;
    QPen m_pen;

private:
    enum LineMode { LineModeSimple, LineModeCurve };

    QPainterPath m_path;
    QRectF m_bb;
    bool m_visible = true;
    unsigned m_paintCount = 0;
    qreal m_age = 0.5;
    LinkType m_linkType;
    LineMode lineMode;
    NodeSocket *source = nullptr;
    NodeSocket *dest = nullptr;
    float m_hue = 0;
    float m_alpha = 0;
    QString m_middleText;
};

#endif // NODE_LINK_H
