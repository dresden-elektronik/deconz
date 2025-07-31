/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QMimeData>
#include <QTimerEvent>
#include <QPixmap>
#include <QWheelEvent>
#include <qmath.h>
#include <vector>
#include "deconz/atom_table.h"
#include "deconz/dbg_trace.h"
#include "deconz/u_sstream_ex.h"
#include "gui/gnode_link_group.h"
#include "zm_graphicsview.h"
#include "zm_gnode.h"
#include "actor_vfs_model.h"

struct NodeIndicator
{
    void *user;
    int runs;
};

class GraphicsViewPrivate
{
public:
    QTimer *m_marginTimer;
    int m_moveTimer;
    int m_indicationTimer;
    NodeLinkGroup *m_nodeLinkGroup;
    std::vector<NodeIndicator> indicators;
};

static zmGraphicsView *inst;
static GraphicsViewPrivate *inst_d;
static AT_AtomIndex ati_state;
static AT_AtomIndex ati_config;
static AT_AtomIndex ati_devices;

// defined in zm_gnode.cpp
extern void NV_IndicatorCallback(void *user);

void NV_AddNodeIndicator(void *user, int runs)
{
    if (!inst_d)
        return;

    if (runs <= 0)
        return;

    auto i = std::find_if(inst_d->indicators.begin(), inst_d->indicators.end(), [user](const NodeIndicator &x){ return x.user == user; });

    if (i != inst_d->indicators.end())
    {
        i->runs = runs; // update
        return;
    }

    NodeIndicator ind;
    ind.runs = runs;
    ind.user = user;

    inst_d->indicators.push_back(ind);
}

zmGraphicsView::zmGraphicsView(QWidget *parent) :
    QGraphicsView(parent),
    d_ptr(new GraphicsViewPrivate)
{
    setDragMode(QGraphicsView::ScrollHandDrag);
    setRenderHint(QPainter::Antialiasing);
    setAcceptDrops(true);

//    setCacheMode(QGraphicsView::CacheBackground);

    d_ptr->m_nodeLinkGroup = new NodeLinkGroup(this);
    d_ptr->m_moveTimer = 0;
    d_ptr->m_marginTimer = new QTimer(this);
    d_ptr->m_marginTimer->setSingleShot(true);

    connect(d_ptr->m_marginTimer, SIGNAL(timeout()),
            this, SLOT(updateMargins()));

    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    inst = this;
    inst_d = d_ptr;

    d_ptr->m_indicationTimer = startTimer(500);

    AT_AddAtom("config", qstrlen("config"), &ati_config);
    AT_AddAtom("state", qstrlen("state"), &ati_state);
    AT_AddAtom("devices", qstrlen("devices"), &ati_devices);

    connect(ActorVfsModel::instance(), &ActorVfsModel::dataChanged, this, &zmGraphicsView::vfsDataChanged);
}

zmGraphicsView::~zmGraphicsView()
{
    inst = nullptr;
    inst_d = nullptr;
    if (d_ptr)
    {
        delete d_ptr->m_nodeLinkGroup;
        delete d_ptr;
        d_ptr = nullptr;
    }
}

void zmGraphicsView::setScene(QGraphicsScene *scene)
{
    QGraphicsView::setScene(scene);
    //scene->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
    scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    updateMargins();

    //QColor sceneColor(250, 250, 250);
#if 0
    QColor sceneColor(245, 245, 245);

#if 0
    setCacheMode(QGraphicsView::CacheBackground);
    QPixmap pm(16, 16);
    QPainter painter(&pm);

    pm.fill(sceneColor);
    painter.setPen(QColor(235, 235, 235));
    painter.drawLine(0, 0, 0, 16);
    painter.drawLine(0, 0, 16, 0);

    QBrush bgBrush(pm);
#else
    QBrush bgBrush(sceneColor);
#endif
    scene->setBackgroundBrush(bgBrush);
#endif

    connect(scene, SIGNAL(sceneRectChanged(QRectF)),
            this, SLOT(onSceneRectChanged(QRectF)));
}

void zmGraphicsView::timerEvent(QTimerEvent *event)
{
    if (d_ptr->m_indicationTimer == event->timerId())
    {
        processIndications();
    }
//    if (m_moveTimer && (event->timerId() == m_moveTimer))
//    {
//        // processForces();
//    }
}

void zmGraphicsView::wheelEvent(QWheelEvent *event)
{
    qreal dy;
#if QT_VERSION < QT_VERSION_CHECK(5,14,0)
    dy = event->delta();
#else
    dy = event->angleDelta().y();
#endif

    QTransform tr = transform();
    qreal zoomBase = 1.0015;

    qreal scaleFactor = qPow(zoomBase, -dy);

    tr.scale(scaleFactor, scaleFactor);

    qreal scaleY = tr.m22();

    if (dy < 0.0) // zoom in
    {
        if (scaleY > 1.25)
        {
            scaleY = 1.25;
        }
    }
    else // zoom out
    {
        if (scaleY < 0.3)
        {
            scaleY = 0.3;
        }
    }

    QTransform tr2;
    tr2.translate(tr.dx(), tr.dy());
    tr2.scale(scaleY, scaleY);

    // scale(scaleFactor, scaleFactor);
    setTransform(tr2);
    event->accept();
}

void zmGraphicsView::drawBackground(QPainter *painter, const QRectF &rect)
{
    d_ptr->m_nodeLinkGroup->paint(painter, rect);
}

void zmGraphicsView::dragEnterEvent(QDragEnterEvent *event)
{
    const QMimeData *mimeData = event->mimeData();

    if (mimeData)
    {
        const auto fmts = mimeData->formats();

        for (const auto &fmt : fmts)
        {
            DBG_Printf(DBG_INFO, "fmt: %s\n", qPrintable(fmt));

            DBG_Printf(DBG_INFO, "%s\n", mimeData->data(fmt).constData());
        }

        if (mimeData->hasFormat("application/vnd.wireshark.displayfilter"))
        {
            event->acceptProposedAction();
        }
    }
}

void zmGraphicsView::dragMoveEvent(QDragMoveEvent *event)
{
    Q_UNUSED(event)
}

void zmGraphicsView::dropEvent(QDropEvent *event)
{
    DBG_Printf(DBG_INFO, "drop event:\n");

    const QMimeData *mimeData = event->mimeData();
    if (!mimeData)
    {
        return;
    }

    if (mimeData->hasFormat("application/vnd.wireshark.displayfilter"))
    {
        const QByteArray data = mimeData->data("application/vnd.wireshark.displayfilter");
        DBG_Printf(DBG_INFO, "%s\n", data.constData());
    }
}

void zmGraphicsView::processIndications()
{
    for (size_t i = 0; i < d_ptr->indicators.size(); i++)
    {
        NodeIndicator &ind = d_ptr->indicators[i];
        ind.runs--;
        NV_IndicatorCallback(ind.user);

        if (ind.runs <= 0)
        {
            d_ptr->indicators[i] = d_ptr->indicators.back();
            d_ptr->indicators.pop_back();
        }
    }
}

void zmGraphicsView::vfsDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles)
{
    // devices/00:0b:57:ff:fe:26:56:80/subdevices/00:0b:57:ff:fe:26:56:80-01/attr/swversion

    QModelIndex parent = topLeft.parent();

    // state/*
    AT_AtomIndex atiParent;
    atiParent.index = parent.data(ActorVfsModel::AtomIndexRole).toUInt();

    if (atiParent.index == ati_state.index || atiParent.index == ati_config.index)
    {
    }
    else
    {
        return; // filter for now
    }

    parent = parent.parent(); // <sub device id>
    parent = parent.parent(); // subdevices
    parent = parent.parent(); // <mac>

    AT_Atom aMac;
    AT_Atom aValueName;

    {
        const auto v1 = parent.data(ActorVfsModel::AtomIndexRole);
        const auto v2 = topLeft.data(ActorVfsModel::AtomIndexRole);

        if (v1.isNull() || v2.isNull())
            return;

        AT_AtomIndex ati_mac;
        AT_AtomIndex ati_value_name;
        ati_mac.index = v1.toUInt();
        ati_value_name.index = v2.toUInt();

        aMac = AT_GetAtomByIndex(ati_mac);
        aValueName = AT_GetAtomByIndex(ati_value_name);
    }

    if (aMac.data && aMac.len == 23 && aValueName.data)
    {
        U_SStream ss;
        uint64_t mac = 0;

        U_sstream_init(&ss, aMac.data, aMac.len);
        mac = U_sstream_get_mac_address(&ss);


        zmgNode *gnode = GUI_GetNodeWithMac(mac);
        if (gnode)
        {
            gnode->vfsModelUpdated(topLeft);
        }
    }
}

void zmGraphicsView::updateMargins()
{
    qreal minWith = 1000;
    qreal minheight = 700;

    QRectF itemRect = scene()->itemsBoundingRect();

    if (itemRect.width() < minWith)
    {
        qreal w = (minWith - itemRect.width()) / 2;
        itemRect = itemRect.adjusted(-w, 0, w, 0);
    }

    if (itemRect.height() < minheight)
    {
        qreal h = (minheight - itemRect.height()) / 2;
        itemRect = itemRect.adjusted(0, -h, 0, h);
    }

    itemRect = itemRect.adjusted(-1000, -1000, 1000, 1000);

    QRectF rect = sceneRect();

    if (qAbs(itemRect.width() - rect.width()) > 100 ||
        qAbs(itemRect.height() - rect.height()) > 100)
    {
        setSceneRect(itemRect);
    }


    d_ptr->m_marginTimer->stop();
}

void zmGraphicsView::repaintAll()
{
    d_ptr->m_nodeLinkGroup->repaintAll();
    update();
}

void zmGraphicsView::onSceneRectChanged(const QRectF &rect)
{
    Q_UNUSED(rect);

    if (d_ptr->m_marginTimer->isActive())
        d_ptr->m_marginTimer->stop();

    d_ptr->m_marginTimer->start(2000);

    d_ptr->m_nodeLinkGroup->setSceneRect(rect.adjusted(-96, -96, 96, 96));
}

#if 0
void zmGraphicsView::processForces()
{
    QList<zmgNode *> nodes;
    foreach (QGraphicsItem *item, scene()->items())
    {
        zmgNode *node = qgraphicsitem_cast<zmgNode *>(item);
        if (node)
        {
            nodes.append(node);
        }
    }

    foreach (zmgNode *node, nodes)
    {
        node->calculateForces();
    }

    bool itemsMoved = false;
    foreach (zmgNode *node, nodes)
    {
        QPointF pos = node->pos();

        node->advance(1);
        if (pos != node->pos())
        {
            itemsMoved = true;
        }
    }

    if (!itemsMoved)
    {
        killTimer(m_moveTimer);
        m_moveTimer = 0;
    }
}
#endif

//void zmGraphicsView::displayNode(zmgNode *node)
//{
//    if (node)
//    {
//        ensureVisible(node);
//    }
//}
