/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QTimerEvent>
#include <QPixmap>
#include <QWheelEvent>
#include <qmath.h>
#include <vector>
#include "gui/gnode_link_group.h"
#include "zm_graphicsview.h"

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
        if (scaleY > 1.0)
        {
            scaleY = 1.0;
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
