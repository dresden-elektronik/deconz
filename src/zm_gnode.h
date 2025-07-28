/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef NN_NODE
#define NN_NODE

#include <array>
#include <QGraphicsObject>

#include "deconz/zcl.h"
#include "deconz/timeref.h"

class NodeSocket;
class NodeLink;
class QGraphicsRectItem;
class QTimer;

/*!
    \class Node

    \brief A baseclass for nodes in the QGraphicsView.
 */
class zmgEndpointBox;
struct IndicationDef;

namespace deCONZ {
    class zmNode;
}

class zmgNode : public QGraphicsObject
{
	Q_OBJECT

public:
    enum Socket
    {
        NeighborSocket = 0,
        DataSocket = 1
    };

    explicit zmgNode(deCONZ::zmNode *data, QGraphicsItem *parent = 0);
    virtual ~zmgNode();
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void setOtauActive(const deCONZ::SteadyTimeRef ref)
    {
        if (!isValid(m_otauActiveTime))
        {
            update();
        }
        m_otauActiveTime = ref;
    }
    deCONZ::zmNode *data() const { return m_data; }
    NodeSocket *socket(Socket sock) const { return m_sockets[sock]; }
    NodeSocket *socket(quint8 endpoint, quint16 cluster, deCONZ::ZclClusterSide side);
    void addLink(NodeLink *link);
    void remLink(NodeLink *link);
    bool hasLink(NodeLink *link);
    void updateLinks();
    void updateLink(NodeLink *link);
    bool needSaveToDatabase() const;
    void setBattery(int battery);
    void setNeedSaveToDatabase(bool needSave);

    bool ownsSocket(NodeSocket *socket) const;

    enum { Type = UserType + deCONZ::GraphNodeType };
    int type() const override { return Type; }
    void toggleEndpointDropdown();
    void toggleConfigDropdown();
    void updated(deCONZ::RequestId id);
    void indicate(deCONZ::Indication type);
    void checkVisible();
    NodeLink *link(int i);
    int linkCount() const { return static_cast<int>(m_links.size()); }
    int selectionOrder() const { return m_selectionCounter; }
    void requestUpdate();
    void setName(const QString &name);
    const QString &name() const { return m_name; }
    void setAddress(quint16 nwk, quint64 mac);
    void setDeviceType(deCONZ::DeviceType type);
    void setLastSeen(qint64 lastSeen);
    void setHasDDF(int hasDDF);
    void indicationTick();

signals:
    void moved();

    void socketConnectRequest(NodeSocket *src, NodeSocket *dst);
    void linkDisconnectRequest(NodeLink *link);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

    void updateParameters();
    void resetIndicator();

protected:
    deCONZ::SteadyTimeRef m_otauActiveTime{};
    QGraphicsEllipseItem *m_indicator = nullptr;
    deCONZ::zmNode *m_data = nullptr;
    bool m_epDropDownVisible;
    bool m_configVisible;

    std::array<NodeSocket*, 2> m_sockets;
    std::vector<NodeLink*> m_links;
    size_t m_linksIter = 0;
    zmgEndpointBox *m_epBox = nullptr;
    QRectF m_endpointToggle;
    const IndicationDef *m_indDef = nullptr;
    int m_indCount = 0;
    deCONZ::Indication m_indType;
    QRectF m_indRect;
    QString m_name;
    QString m_extAddress;
    quint64 m_extAddressCache = 0;
    quint16 m_nwkAddressCache = 0xFFFF;
    qint64 m_lastSeen = 0;
    int m_moveWatcher = -1;
    int m_hasDDF = 0;
    bool m_needSaveToDatabase;
    int m_selectionCounter = -1;
    QPixmap m_pm;
    int m_width;
    int m_height;
    int m_battery = -1;
    bool m_isZombie = false;
    bool m_dirty = false;
    deCONZ::DeviceType m_deviceType = deCONZ::UnknownDevice;
};

void GUI_InitNodeActor();

#endif
