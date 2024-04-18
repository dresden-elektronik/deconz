/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QGraphicsLinearLayout>
#include <QGraphicsScene>
#include <QPainter>
#include <QUrlQuery>
#include <QDockWidget>

#include "zm_config.h"
#include "zm_cluster_info.h"
#include "zm_gendpointbox.h"
#include "zm_gendpoint.h"
#include "zm_gcluster.h"
#include "zm_gsocket.h"
#include "zm_glink.h"
#include "zm_global.h"
#include "zm_gnode.h"
#include "zm_node.h"

zmgEndpointBox::zmgEndpointBox(QGraphicsItem *parent) :
    QGraphicsWidget(parent),
    m_node(0)
{
    QGraphicsLinearLayout *lay = new QGraphicsLinearLayout;
    lay->setOrientation(Qt::Vertical);
    lay->setSpacing(1);
    lay->setContentsMargins(3, 3, 3, 3);
    setLayout(lay);
    setZValue(0.1);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsFocusable, true);
    setCursor(Qt::ArrowCursor);
}

void zmgEndpointBox::updateEndpoints(zmgNode *node)
{
    m_node = node;

    if (!node)
    {
        return;
    }

//    if (node->data()->simpleDescriptors().isEmpty())
//    {
//        clear();
//    }

    // this will also breake all bindings!
    clear();

    foreach (const deCONZ::SimpleDescriptor &sd, node->data()->simpleDescriptors())
    {
        if (!m_endpoints.contains(sd.endpoint()))
        {
            addEndpoint(sd);
            m_endpoints.append(sd.endpoint());
        }
    }
}

zmgCluster *zmgEndpointBox::getCluster(quint8 endpoint, quint16 cluster, deCONZ::ZclClusterSide side)
{
    foreach (zmgCluster *cl, m_clusters)
    {
        if ((cl->endpoint() == endpoint) && (cl->id() == cluster) && (cl->clusterSide() == side))
        {
            return cl;
        }
    }

    return 0;
}

void zmgEndpointBox::endpointDoubleClicked(quint8 endpoint)
{
    if (m_node->data())
    {
        deCONZ::clusterInfo()->setEndpoint(m_node->data(), endpoint);
    }
}

void zmgEndpointBox::clusterClicked(quint8 endpoint, quint16 clusterId, deCONZ::ZclClusterSide clusterSide)
{
    if (m_node->data())
    {
        deCONZ::clusterInfo()->setEndpoint(m_node->data(), endpoint);
        deCONZ::clusterInfo()->showCluster(clusterId, clusterSide);
        if (!m_node->isSelected())
        {
            scene()->clearSelection();
            m_node->setSelected(true);
        }
    }
}

void zmgEndpointBox::clusterDoubleClicked(bool scrollAttr)
{
    auto *clusterInfo = deCONZ::clusterInfo();

    if (!clusterInfo || !clusterInfo->parent())
    {
        return;
    }

    QWidget *dock = qobject_cast<QWidget*>(clusterInfo->parent()->parent());
    QDockWidget *dock1 = nullptr;
    if (dock && dock->parent())
    {
        dock1 = qobject_cast<QDockWidget*>(dock->parent());
    }

    if (dock1)
    {
        dock1->show();
        dock1->raise();
    }

    if (scrollAttr)
    {
        clusterInfo->scrollToAttributes();
    }
}

void zmgEndpointBox::clear()
{
    prepareGeometryChange();

    while (layout()->count() > 0)
    {
        QGraphicsLayoutItem *item = layout()->itemAt(0);

        layout()->removeAt(0);
        delete item;
    }

    m_endpoints.clear();
    m_clusters.clear();
}

void zmgEndpointBox::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    painter->setPen(Qt::NoPen);
    painter->setBrush(palette().dark());

    qreal left;
    qreal top;
    qreal right;
    qreal bottom;

    layout()->getContentsMargins(&left, &top, &right, &bottom);
    left += 1;
    right += 1;
    painter->drawRoundedRect(boundingRect().adjusted(left, top, -right, 0), 2, 2);

    QGraphicsWidget::paint(painter, option, widget);
}

void zmgEndpointBox::addEndpoint(const deCONZ::SimpleDescriptor &sd)
{
    zmgEndpoint *ep = new zmgEndpoint(this, this);
    ep->setSimpleDescriptor(sd);
    QGraphicsLinearLayout *lay = (QGraphicsLinearLayout*)layout();
    lay->addItem(ep);

#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
    QUrl url;
    QUrlQuery urlq;
#else
    QUrl url;
    QUrl &urlq = url;
#endif
    url.setScheme(EP_URL_SCHEME);
    urlq.addQueryItem(CL_ITEM_PROFILE_ID, QString::number(sd.profileId(), 16));
    urlq.addQueryItem(CL_ITEM_DEVICE_ID, QString::number(sd.deviceId(), 16));
    urlq.addQueryItem(CL_ITEM_ENDPOINT, QString::number(sd.endpoint(), 16));
    urlq.addQueryItem(CL_ITEM_EXT_ADDR, QString::number(m_node->data()->address().ext(), 16));
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
    url.setQuery(urlq);
#endif
    ep->setUrl(url);

    for (const deCONZ::ZclCluster &cl : sd.inClusters())
    {
        addCluster(cl, deCONZ::ServerCluster, sd);
    }

    for (const deCONZ::ZclCluster &cl: sd.outClusters())
    {
        addCluster(cl, deCONZ::ClientCluster, sd);
    }
}

void zmgEndpointBox::addCluster(const deCONZ::ZclCluster &cl, int clusterSide, const deCONZ::SimpleDescriptor &sd)
{
    Q_ASSERT(m_node);

    zmgCluster *gcl = new zmgCluster(this, this);

    QUrl url;
    QUrlQuery urlq;

    url.setScheme(CL_URL_SCHEME);
    urlq.addQueryItem(CL_ITEM_PROFILE_ID, "0x" + QString::number(sd.profileId(), 16));
    urlq.addQueryItem(CL_ITEM_DEVICE_ID, "0x" + QString::number(sd.deviceId(), 16));
    urlq.addQueryItem(CL_ITEM_CLUSTER_ID, "0x" + QString::number(cl.id(), 16));
    urlq.addQueryItem(CL_ITEM_NAME, cl.name());
    urlq.addQueryItem(CL_ITEM_ENDPOINT, "0x" + QString::number(sd.endpoint(), 16));
    urlq.addQueryItem(CL_ITEM_EXT_ADDR, "0x" + QString::number(m_node->data()->address().ext(), 16));
    urlq.addQueryItem(CL_ITEM_CLUSTER_SIDE, clusterSide == deCONZ::ServerCluster ? "server" : "client");
    url.setQuery(urlq);

    gcl->setUrl(url);
    gcl->setAttributeCount(cl.attributes().size());
    QGraphicsLinearLayout *lay = (QGraphicsLinearLayout*)layout();
    lay->addItem(gcl);
//    lay->setStretchFactor(gcl, 0.001);
    m_clusters.append(gcl);
}

void zmgEndpointBox::removeItem(QGraphicsLayoutItem *item)
{
    delete item;
}
