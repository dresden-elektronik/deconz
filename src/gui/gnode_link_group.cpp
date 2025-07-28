/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QPainter>
#include <QPixmap>
#include <QtMath>
#include <array>
#include <vector>
#include "deconz/dbg_trace.h"
#include "gui/theme.h"
#include "zm_glink.h"
#include "gnode_link_group.h"
#include "zm_graphicsview.h"

enum {
    TILE_SIZE = 256,
    MAX_CACHE_TILES = 96
};

struct Tile
{
    unsigned paintAge;
    QRectF rect;
    QPixmap pm;
};

class NodeLinkGroupPrivate
{
public:
    std::vector<NodeLink*> links;
    QRectF sceneRect;
    QRectF dirtyRect;
    zmGraphicsView *view;
    NodeLinkGroup::RenderQuality quality;
    NodeLinkGroup::LineMode lineMode;
    unsigned paintAge = 0;
    std::array<Tile, MAX_CACHE_TILES> tiles;
};


static NodeLinkGroup *inst = nullptr;

NodeLinkGroup::NodeLinkGroup(zmGraphicsView *view) :
    d(new NodeLinkGroupPrivate)
{
    Q_ASSERT(inst == nullptr);
    d->dirtyRect = QRectF(0,0,0,0);
    d->view = view;
    d->quality = RenderQualityHigh;
    d->lineMode = LineModeBezier;

    inst = this;

    for (size_t i = 0; i < d->tiles.size(); i++)
    {
        d->tiles[i].paintAge = 0;
        d->tiles[i].pm = QPixmap(TILE_SIZE, TILE_SIZE);
        d->tiles[i].pm.fill(Qt::yellow);
    }
}

NodeLinkGroup::~NodeLinkGroup()
{
    inst = nullptr;
    delete d;
    d = nullptr;
}

void NodeLinkGroup::paint(QPainter *painter, const QRectF &rect)
{
    const QBrush sceneColor(Theme_Color(ColorNodeViewBackground));

    d->paintAge++;
    if (d->quality == RenderQualityHigh)
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setClipping(false);

    //painter->eraseRect(rect);
    painter->fillRect(rect, sceneColor);
    QRectF rr = rect;

    if (d->dirtyRect.isValid())
    {
        //painter->fillRect(d->dirtyRect, Qt::red);
        rr = rect.united(d->dirtyRect);
    }

    qreal startX = qFloor(d->sceneRect.x());
    qreal startY = qFloor(d->sceneRect.y());
    for (;startX + TILE_SIZE < rr.x();)
        startX += TILE_SIZE;

    for (;startY + TILE_SIZE < rr.y();)
        startY += TILE_SIZE;

    int nx = 1;
    for (;TILE_SIZE * nx < (int)rr.width() + TILE_SIZE;)
        nx++;

    int ny = 1;
    for (;TILE_SIZE * ny < (int)rr.height() + TILE_SIZE;)
        ny++;


    static unsigned colorIter = 0;
    static const Qt::GlobalColor colors[] = {Qt::red, Qt::blue, Qt::green, Qt::cyan };

    for (int ty = 0; ty < ny; ty++)
    {
        const qreal y = startY + ty * TILE_SIZE;
        for (int tx = 0; tx < nx; tx++)
        {
            const qreal x = startX + tx * TILE_SIZE;
            QRectF tileRect(x, y, TILE_SIZE, TILE_SIZE);

            unsigned oldest = 0;
            unsigned tileN = 0;
            int occluded = 0;

            for (; tileN < d->tiles.size(); tileN++)
            {
                if (d->tiles[tileN].rect.contains(x + 10.0, y + 10.0))
                    break;

                if (d->tiles[tileN].paintAge < d->tiles[oldest].paintAge)
                {
                    if (d->tiles[oldest].rect.isValid())
                        oldest = tileN;
                }
            }

            // have a cached tile
            bool redraw = false;
            if (tileN < d->tiles.size())
            {
                oldest = d->tiles.size(); // invalidate
                // need update?
                if (d->dirtyRect.intersects(tileRect))
                {
                    redraw = true;
                }
            }
            else // update the oldest tile
            {
                for (NodeLink *link : d->links)
                {
                    if (!link->isVisible())
                        continue;

                    if (tileRect.intersects(link->boundingRect()))
                    {
                        occluded++;
                        break;
                    }
                }

                if (occluded == 0)
                {
                    painter->fillRect(tileRect, sceneColor);
                    //painter->eraseRect(tileRect);
                    continue;
                }

                tileN = oldest;
                redraw = true;
                // DBG_Printf(DBG_INFO, "BG PAINT[%u] x: %d, y: %d, (links to draw: %u)\n", tileN, tx, ty,  occluded);
            }

            Tile &tile = d->tiles[tileN];
            tile.paintAge = d->paintAge;

            if (redraw)
            {
                tile.rect = tileRect;
                //tile.pm.fill(colors[colorIter]);

                QPainter p(&tile.pm);
                QTransform transform;

                transform.translate(-tile.rect.x(), -tile.rect.y());

                p.setTransform(transform);

                p.setRenderHint(QPainter::Antialiasing, false);
                p.fillRect(tileRect, sceneColor);

#ifdef ARCH_ARM
                p.setRenderHint(QPainter::Antialiasing, false);
#else

                if (d->quality == RenderQualityHigh)
                {
                    p.setRenderHint(QPainter::Antialiasing, true);
                }
                else
                {
                    p.setRenderHint(QPainter::Antialiasing, false);
                }
#endif
                p.setOpacity(1);
                p.setClipRect(tileRect);
                //p.eraseRect(tileRect);

                //p.setPen(QPen(colors[colorIter], 4));
                //p.setBrush(Qt::NoBrush);
                //p.drawRect(tileRect);

                occluded = 0;

                for (NodeLink *link : d->links)
                {
                    if (!link->isVisible())
                        continue;

                    if (tileRect.intersects(link->boundingRect()))
                    {
                        p.setPen(link->pen());

                        if (d->lineMode == LineModeSimple)
                        {
                            p.drawLine(link->m_p0, link->m_p3);
                        }
                        else if (d->lineMode == LineModeBezier)
                        {
                            p.drawPath(link->path());
                        }

                        occluded++;
                    }

                    if (!link->middleText().isEmpty())
                    {
                        QPointF pt = link->path().pointAtPercent(0.5);
                        p.setPen(Qt::black);
                        p.drawText(pt, link->middleText());
                    }
                }

                if (occluded == 0)
                {
                    painter->fillRect(tileRect, sceneColor);
                    // painter->eraseRect(tileRect);
                    //painter->fillRect(tileRect, Qt::darkCyan);

                    tile.rect = {};
                    tile.paintAge = 0;
                    continue;
                }
                // DBG_Printf(DBG_INFO, "BG TILES[%u] x: %d, y: %d, (links: %u)\n", tileN, tx, ty, occluded);
            }

            colorIter = (colorIter + 1) % (sizeof(colors) / sizeof(colors[0]));

            const QRectF source(0, 0, TILE_SIZE, TILE_SIZE);
            painter->drawPixmap(QPointF(x, y), tile.pm, source);
        }
    }

    if (d->dirtyRect.isValid())
    {
        d->dirtyRect = {};
    }
}

void NodeLinkGroup::setSceneRect(const QRectF &rect)
{
    if (d->sceneRect != rect)
    {
        d->sceneRect = rect;
        d->dirtyRect = rect;
        d->view->scene()->invalidate();
    }
}

void NodeLinkGroup::addLink(NodeLink *link)
{
    const auto i = std::find(d->links.cbegin(), d->links.cend(), link);

    if (i == d->links.cend())
    {
        d->links.push_back(link);
        markDirty(link);

#if 0
#ifdef __arm__
        // TODO test: don't slow down rendering too much
        if (d->lineMode != LineModeSimple && d->links.size() > 50)
        {
            d->lineMode = LineModeSimple;
        }
#endif
#endif
    }
}

void NodeLinkGroup::removeLink(NodeLink *link)
{
    auto i = std::find(d->links.begin(), d->links.end(), link);

    if (i != d->links.end())
    {
        markDirty(link);
        d->links.erase(i);
    }
}

void NodeLinkGroup::repaintAll()
{
    d->dirtyRect = d->sceneRect;
    d->view->scene()->invalidate();
}

void NodeLinkGroup::setRenderQuality(RenderQuality quality)
{
    if (!inst)
        return;

    NodeLinkGroupPrivate *d = inst->d;
    if (d->quality != quality)
    {
        d->quality = quality;

        // if we switch from fast rendering to high quality, re-render all tiles
        if (quality == RenderQualityHigh)
        {
            for (Tile &tile : d->tiles)
            {
                if (tile.rect.isValid())
                    d->dirtyRect = d->dirtyRect.united(tile.rect);
            }

            if (d->dirtyRect.isValid())
                d->view->scene()->invalidate(d->dirtyRect, QGraphicsScene::BackgroundLayer);
        }
    }
}

void NodeLinkGroup::markDirty(NodeLink *link)
{
    if (inst && link->isVisible())
    {
        NodeLinkGroupPrivate *d = inst->d;
        d->dirtyRect = d->dirtyRect.united(link->boundingRect());
        if (d->dirtyRect.isValid())
            d->view->scene()->invalidate(d->dirtyRect, QGraphicsScene::BackgroundLayer);
    }
}

NodeLinkGroup *NodeLinkGroup::instance()
{
    return inst;
}

