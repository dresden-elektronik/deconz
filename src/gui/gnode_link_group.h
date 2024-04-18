/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef GNODE_LINK_GROUP_H
#define GNODE_LINK_GROUP_H

class NodeLink;
class QPainter;
class QRectF;
class zmGraphicsView;
class NodeLinkGroupPrivate;

class NodeLinkGroup
{
public:
    enum RenderQuality
    {
        RenderQualityHigh,
        RenderQualityFast
    };

    enum LineMode
    {
        LineModeBezier,
        LineModeSimple
    };

    NodeLinkGroup(zmGraphicsView *view);
    ~NodeLinkGroup();
    void paint(QPainter *painter, const QRectF &rect);

    void setSceneRect(const QRectF &rect);

    void addLink(NodeLink *link);
    void removeLink(NodeLink *link);

    static void setRenderQuality(RenderQuality quality);
    static void markDirty(NodeLink *link);
    static NodeLinkGroup *instance();

private:
    NodeLinkGroupPrivate *d = nullptr;
};

#endif // GNODE_LINK_GROUP_H
