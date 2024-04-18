/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QtMath>
#include "deconz/dbg_trace.h"
#include "zm_glink.h"
#include "zm_gsocket.h"
#include "gui/gnode_link_group.h"

NodeLink::NodeLink(NodeSocket *src, NodeSocket *dst)
{
    m_linkType = LinkNormal;
    source = src;
    dest = dst;
#ifdef LOWRES_GRAPHICS
    //lineMode = LineModeSimple;
    lineMode = LineModeCurve;
#else
//    lineMode = LineModeSimple;
    lineMode = LineModeCurve;
#endif

    Q_ASSERT(source && dest);

    m_color = QColor(200, 200, 200);
    m_pen = QPen(m_color, 2.0);

    m_visible = false;

    source->addLink(this);
    dest->addLink(this);

    if (NodeLinkGroup::instance())
        NodeLinkGroup::instance()->addLink(this);
}

NodeLink::~NodeLink()
{
    if (source)
    {
        source->disconnect(dest);
        source->removeLink(this);
    }

    if (dest)
    {
        dest->disconnect(source);
        dest->removeLink(this);
    }

    if (NodeLinkGroup::instance())
        NodeLinkGroup::instance()->removeLink(this);

    source = 0;
    dest = 0;
}

void NodeLink::setSockets(NodeSocket *src, NodeSocket *dst)
{
    if (source)
    {
        source->disconnect(dest);
        source->removeLink(this);
    }

    if (dest)
    {
        dest->disconnect(source);
        dest->removeLink(this);
    }

    source = src;
    dest = dst;

    if (source)
    {
        source->addLink(this);
    }

    if (dest)
    {
        dest->addLink(this);
    }
}

/*!
    Sets the value of the node (adjust color).

    \param age 0 .. 1.0
 */
void NodeLink::setValue(qreal age)
{
    if (age < 0.0) { age = 0.0; }
    else if (age > 1.0) { age = 1.0; };

    if (linkType() != LinkNormal)
    {
        return;
    }

    if (qFuzzyCompare(1.0 + age, 1.0 + m_age))
    {
        // nothing todo
        return;
    }

    m_age = age; // remember

    const qreal from = 120.0f / 360.0f; // green
    qreal hue = from * (1.0f - age);

    if (hue < 0)
        hue = 0.0f;

    qreal alpha = 1.0;
    if (m_linkType == LinkNormal)
    {
        alpha = 1.0 - age;

        if (alpha < 0.15)
        {
            alpha = 0.15;
        }
    }

    if (qAbs(m_hue - hue) > 0.1 || qAbs(m_alpha - alpha) > 0.1)
    {
        m_hue = hue;
        m_alpha = alpha;
        m_color = QColor::fromHsvF(hue, 0.85, 0.9, alpha);
        m_pen = QPen(m_color, m_pen.widthF());

        if (m_visible)
        {
            NodeLinkGroup::markDirty(this);
        }
    }
}

void NodeLink::setLinkType(LinkType type)
{
    if (m_linkType != type)
    {
        m_linkType = type;

        switch (m_linkType)
        {
        case LinkBinding:
            m_color = QColor(80, 80, 80);
            m_pen = QPen(m_color, m_pen.widthF());
            break;

        case LinkRouting:
            m_color = QColor(40, 80, 240);
            m_pen = QPen(m_color, m_pen.widthF());
            break;

        default:
            break;
        }
    }
}

void NodeLink::setMiddleText(const QString &text)
{
    if (m_middleText != text)
    {
        m_middleText = text;
        NodeLinkGroup::markDirty(this);
    }
}

#if 0
void NodeLink::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    //painter->setRenderHint(QPainter::Antialiasing);

//    if ((m_linkType == LinkBinding) && (option->state & QStyle::State_MouseOver))
//    {
//        setPen(QPen(m_color.darker(130), pen().widthF()));
//    }
//    else
//    {
//        setPen(QPen(m_color, pen().widthF()));
//    }
    m_paintCount++;

//    DBG_Printf(DBG_ERROR, "LNK paint() %p, paint count: %u, w: %f, h: %f\n", this, m_paintCount, option->exposedRect.width(), option->exposedRect.height());

//    QGraphicsPathItem::paint(painter, option, widget);

    // TODO: make this a extra graphicsitem
    if (!m_middleText.isEmpty())
    {
        painter->setPen(Qt::black);
        const QPointF mid = path().pointAtPercent(.5);
        painter->drawText(mid, m_middleText);
    }
}
#endif

void NodeLink::remSocket(NodeSocket *socket)
{
    if (source == socket) source = nullptr;
    if (dest == socket) dest = nullptr;

    if (isVisible() && (!source || !dest))
    {
        hide();
    }
}

bool NodeLink::connectsSockets(NodeSocket *a, NodeSocket *b)
{
    if (source == a && dest == b)
        return true;

    if (source == b && dest == a)
        return true;

    return false;
}

QRectF NodeLink::boundingRect() const
{
    if (!m_middleText.isEmpty())
    {
        return m_bb.adjusted(-100, -100, 100, 100);
    }

    return m_bb;
}

static bool doPointsDiffer(const QPointF &a, const QPointF &b)
{
    if (!qFuzzyCompare(a.x(), b.x())) return true;
    if (!qFuzzyCompare(a.y(), b.y())) return true;

    return false;
}

void NodeLink::updatePosition()
{
    DBG_Assert(source && dest);
    if (!source || !dest)
        return;

    qreal dhalf = source->boundingRect().height() / 2;
    dhalf -= m_pen.widthF() * 0.5;
    dhalf += source->pen().widthF() * 0.5;

    QPointF p0(source->mapToScene(source->pos()));
    QPointF p3(dest->mapToScene(dest->pos()));

    if (lineMode == LineModeCurve)
    {
        qreal dist = 0.41 * qMax(qFabs(p0.x() - p3.x()), qFabs(p0.y() - p3.y()));

        QPointF p1(p0);

        switch (source->lookDirection()) {
        case NodeSocket::LookLeft:   { p1 -= QPointF(dist, 0); } break;
        case NodeSocket::LookRight:  { p1 += QPointF(dist, 0); } break;
        case NodeSocket::LookTop:    { p1 -= QPointF(0, dist); } break;
        case NodeSocket::LookBottom: { p1 += QPointF(0, dist); } break;
        default: break;
        }

        QPointF p2(p3);

        switch (dest->lookDirection()) {
        case NodeSocket::LookLeft:   { p2 -= QPointF(dist, 0); } break;
        case NodeSocket::LookRight:  { p2 += QPointF(dist, 0); } break;
        case NodeSocket::LookTop:    { p2 -= QPointF(0, dist); } break;
        case NodeSocket::LookBottom: { p2 += QPointF(0, dist); } break;
        default: break;
        }

        if (doPointsDiffer(p0, m_p0) || doPointsDiffer(p1, m_p1) || doPointsDiffer(p2, m_p2) || doPointsDiffer(p3, m_p3))
        {
//            DBG_Printf(DBG_INFO, "LNK updatepositions\n");
            NodeLinkGroup::markDirty(this);

            m_p0 = p0;
            m_p1 = p1;
            m_p2 = p2;
            m_p3 = p3;
            QPainterPath p;
            p.moveTo(p0);
            p.cubicTo(p1, p2, p3);
            m_path = p;
            qreal a = m_pen.widthF() * 3;
            m_bb = m_path.boundingRect().adjusted(-a, -a, a, a);

            NodeLinkGroup::markDirty(this);
        }
    }
    else if (lineMode == LineModeSimple)
    {
        QPointF p1(p0);
        QPointF p2(p3);

        p1.setX(p0.x() - 64.0);
        p2.setX(p3.x() - 64.0f);

        if (doPointsDiffer(p0, m_p0) || doPointsDiffer(p1, m_p1) || doPointsDiffer(p2, m_p2) || doPointsDiffer(p3, m_p3))
        {
            NodeLinkGroup::markDirty(this);

            m_p0 = p0;
            m_p1 = p1;
            m_p2 = p2;
            m_p3 = p3;

            QPainterPath p;

            p.moveTo(p0);
            p.lineTo(p1);
            p.lineTo(p2);
            p.lineTo(p3);
            m_path = p;
            qreal a = m_pen.widthF() * 3;
            m_bb = m_path.boundingRect().adjusted(-a, -a, a, a);

            NodeLinkGroup::markDirty(this);
        }
    }   
}

void NodeLink::hide()
{
    if (m_visible)
    {
        m_visible = false;
        NodeLinkGroup::markDirty(this);
    }
}

void NodeLink::setVisible(bool visible)
{
    if (m_visible != visible)
    {
        m_visible = visible;
        NodeLinkGroup::markDirty(this);
    }
}
