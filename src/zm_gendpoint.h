/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_GENDPOINT_H
#define ZM_GENDPOINT_H

#include <QGraphicsItem>
#include <QGraphicsLayoutItem>
#include <QStaticText>
#include <QIcon>
#include <QUrl>

class zmgEndpointBox;

namespace deCONZ {
    class SimpleDescriptor;
}

class zmgEndpoint : public QGraphicsItem,
                    public QGraphicsLayoutItem
{
public:
    zmgEndpoint(zmgEndpointBox *box, QGraphicsItem *parent = 0);
    QRectF boundingRect() const;
    void setSimpleDescriptor(const deCONZ::SimpleDescriptor &d);
    void setUrl(const QUrl &url) { m_url = url; }

protected:
    QSizeF sizeHint(Qt::SizeHint which, const QSizeF &constraint) const;
    void setGeometry(const QRectF &rect);
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
    void mousePressEvent(QGraphicsSceneMouseEvent *event);
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event);
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event);

private:
    QIcon m_iconProfile;
    QIcon m_iconDevice;
    QStaticText m_device;
//    QString m_device;
    QString m_profile;
    quint8 m_endpoint;
    QString m_endpointText;
    QFont m_font;
    QString m_text;
    QRectF m_rect;
    QUrl m_url;
    zmgEndpointBox *m_box;
};

#endif // ZM_GENDPOINT_H
