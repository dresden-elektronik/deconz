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
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QWidget>
#include <QStyleOptionButton>
#include <QStyleOptionGroupBox>
#include <QStyleOptionGraphicsItem>
#include <QStylePainter>
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
    QStylePainter p(painter->device(), widget);
    QStyleOptionButton opt;
    QColor nodeColorBright(220, 220, 220);
    QColor nodeColorDark(180, 180, 180);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setTransform(painter->transform());
    opt.initFrom(widget);

//    opt.features |= QStyleOptionButton::DefaultButton;
//    opt.icon = m_iconDevice;
//    opt.iconSize = QSize(32, 32);
//    opt.text = m_device;
//    opt.palette = option->palette;
    opt.rect = option->rect;
    opt.state = option->state;

    QLinearGradient grad(option->rect.topLeft(), option->rect.bottomLeft());
    grad.setColorAt(1.0, nodeColorDark.darker(130));
    grad.setColorAt(0.99, nodeColorDark);
    grad.setColorAt(0.05, nodeColorBright);
    grad.setColorAt(0.025, nodeColorBright.lighter(220));
    grad.setColorAt(0.0, nodeColorBright.lighter(250));

    // fake shadow
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(nodeColorDark.darker(102), 1.1));
    p.drawRoundedRect(option->rect.adjusted(2, 2, -1, -1), 3, 3);
    p.setPen(QPen(nodeColorDark.darker(130), 0.8));
    p.drawRoundedRect(option->rect.adjusted(3, 3, -2, -2), 3, 3);

    p.setPen(Qt::NoPen);
    p.setBrush(grad);
//    p.setPen(QPen(borderColor, borderWidth));
    p.drawRoundedRect(option->rect.adjusted(2, 2, -2, -2), 3, 3);

    p.setPen(QPen(Qt::black));

//    p.drawPrimitive(QStyle::PE_PanelButtonTool, opt);
//    p.drawControl(QStyle::CE_PushButtonLabel, opt);

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


//    x = m_rect.x() + painter->fontMetrics().averageCharWidth();
    y += IconSize + painter->fontMetrics().leading();
//    painter->drawText(x, y + painter->fontMetrics().height(), m_device);
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
