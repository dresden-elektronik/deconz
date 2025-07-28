/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QWidget>
// #include <QStyleOptionButton>
// #include <QStyleOptionGroupBox>
#include <QStyleOptionGraphicsItem>
// #include <QStylePainter>
#include <QDrag>
#include <QMimeData>

#include "deconz/zdp_descriptors.h"
#include "zcl_private.h"
#include "zm_gendpoint.h"
#include "zm_gendpointbox.h"

static int IconSize = 28;

zmgEndpoint::zmgEndpoint(zmgEndpointBox *box, QGraphicsItem *parent) :
    QGraphicsItem(parent),
    m_endpoint(0),
    m_box(box)
{
    m_font = QApplication::font();
    m_font.setPixelSize(IconSize * 0.6);
}

QRectF zmgEndpoint::boundingRect() const
{
    return m_rect;
}

QSizeF zmgEndpoint::sizeHint(Qt::SizeHint which, const QSizeF &constraint) const
{
    qreal width = 0.0;
    QSizeF size;
    QFontMetrics fm(m_font);
    Q_UNUSED(constraint)

#if (QT_VERSION < QT_VERSION_CHECK(5, 11, 0))
    width += fm.width(m_endpointText) + 2 * fm.averageCharWidth();

    if (m_device.textWidth() > fm.width(m_profile))
    {
        width += m_device.textWidth();
    }
    else
    {
        width += fm.width(m_profile);
    }
#else
    width += fm.horizontalAdvance(m_endpointText) + 2 * fm.averageCharWidth();

    if (m_device.textWidth() > fm.horizontalAdvance(m_profile))
    {
        width += m_device.textWidth();
    }
    else
    {
        width += fm.horizontalAdvance(m_profile);
    }
#endif

    switch (which)
    {
    case Qt::MinimumSize:
    case Qt::PreferredSize:
        width += 2 * IconSize; // icon space
        width += IconSize + fm.averageCharWidth(); // end freespace
        size.setWidth(width);
        size.setHeight(2 * IconSize + 1.5 * fm.averageCharWidth() + m_device.size().height() + fm.leading());
        break;

    default:
        break;
    }

    return size;
}

void zmgEndpoint::setGeometry(const QRectF &rect)
{
    m_rect = rect;
}

void zmgEndpoint::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    QPalette pal = widget->palette();
    const QColor nodeColorBright(pal.window().color());

    painter->setPen(Qt::NoPen);
    painter->setBrush(nodeColorBright);
    painter->drawRoundedRect(option->rect.adjusted(2, 1, -2, -1), 2, 2);

    painter->setPen(QPen(pal.windowText().color(), 1));
    painter->setFont(m_font);

    qreal x = m_rect.x() + painter->fontMetrics().averageCharWidth();
    qreal y = m_rect.y() + painter->fontMetrics().averageCharWidth();

    painter->drawText(x, y + painter->fontMetrics().height(), m_endpointText);

#if (QT_VERSION < QT_VERSION_CHECK(5, 11, 0))
    x += painter->fontMetrics().width(m_endpointText) +
         painter->fontMetrics().averageCharWidth();
#else
    x += painter->fontMetrics().horizontalAdvance(m_endpointText) +
         painter->fontMetrics().averageCharWidth();
#endif

    if (!m_iconProfile.isNull())
    {
        qreal iconX = x;

        m_iconProfile.paint(painter, iconX, y, IconSize, IconSize);

        iconX += IconSize;

        if (!m_iconDevice.isNull())
        {
            m_iconDevice.paint(painter, iconX, y, IconSize, IconSize);
        }

        x = iconX + IconSize;
        x += painter->fontMetrics().averageCharWidth();
    }

    painter->drawText(x, y + painter->fontMetrics().height(), m_profile);
    y += IconSize + painter->fontMetrics().leading();
    painter->drawStaticText(x, y, m_device);
}

void zmgEndpoint::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    Q_UNUSED(event)
    m_box->endpointDoubleClicked(m_endpoint);
}

void zmgEndpoint::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (QLineF(event->screenPos(), event->buttonDownScreenPos(Qt::LeftButton))
            .length() < QApplication::startDragDistance())
    {
        return;
    }

    QDrag *drag = new QDrag(event->widget());
    QMimeData *mime = new QMimeData;
    mime->setUrls(QList<QUrl>() << m_url);
    drag->setMimeData(mime);
    drag->exec();
}

void zmgEndpoint::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    Q_UNUSED(event)
}

void zmgEndpoint::setSimpleDescriptor(const deCONZ::SimpleDescriptor &d)
{
    deCONZ::ZclProfile profile = deCONZ::zclDataBase()->profile(d.profileId());
    deCONZ::ZclDevice device = deCONZ::zclDataBase()->device(d.profileId(), d.deviceId());

    m_iconProfile = profile.icon();
    m_iconDevice = device.icon();
    m_endpoint = d.endpoint();
    m_endpointText = QString("%1").arg(d.endpoint(), 2, 16, QLatin1Char('0')).toUpper();
    m_device.setText(device.name());
    m_device.setTextWidth(200);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAnywhere);
    m_device.setTextOption(option);
    m_profile = profile.name();

    update();
}
