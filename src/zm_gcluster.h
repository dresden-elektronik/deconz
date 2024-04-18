/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_GCLUSTER_H
#define ZM_GCLUSTER_H

#include <QGraphicsItem>
#include <QGraphicsLayoutItem>
#include <QIcon>
#include <QStaticText>
#include <QUrl>

#include "deconz/zcl.h"

class NodeSocket;
class zmgEndpointBox;

class zmgCluster : public QGraphicsItem,
                   public QGraphicsLayoutItem
{
public:
    explicit zmgCluster(zmgEndpointBox *box, QGraphicsItem *parent = 0);
    QRectF boundingRect() const;
    void setUrl(const QUrl &url);
    void setAttributeCount(int count) { m_attributeCount = count; }
    void setIcon(const QIcon &icon);
    quint8 endpoint() const { return m_endpoint; }
    quint16 id() const { return m_id; }
    deCONZ::ZclClusterSide clusterSide() const { return m_isServer ? deCONZ::ServerCluster : deCONZ::ClientCluster; }
    NodeSocket *socket() const { return m_socket; }

protected:
    void dragEnterEvent(QGraphicsSceneDragDropEvent *event);
    void dragMoveEvent(QGraphicsSceneDragDropEvent *event);
    void dropEvent(QGraphicsSceneDragDropEvent *event);
    void mousePressEvent(QGraphicsSceneMouseEvent *event);
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event);
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event);
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event);
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
    QSizeF sizeHint(Qt::SizeHint which, const QSizeF &constraint) const;
    void setGeometry(const QRectF &rect);

private:
    bool urlClusterId(const QUrl &url, quint16 *id);
    bool urlExtAddress(const QUrl &url, uint64_t *addr);
    bool urlEndpoint(const QUrl &url, quint8 *ep);
    quint16 m_id;
    uint64_t m_extAddr;
    quint8 m_endpoint;
    QUrl m_url;
    QIcon m_icon;
    QRectF m_rect;
    QString m_text;
    QString m_textId;
    bool m_isServer;
    QFont m_font;
    NodeSocket *m_socket;
    int m_attributeCount;
    zmgEndpointBox *m_box;
};

#endif // ZM_GCLUSTER_H
