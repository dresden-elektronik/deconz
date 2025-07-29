/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_GRAPHICSVIEW_H
#define ZM_GRAPHICSVIEW_H

#include <QTimer>
#include <QGraphicsView>

class zmgNode;
class NodeLinkGroup;

class GraphicsViewPrivate;

class zmGraphicsView : public QGraphicsView
{
    Q_OBJECT

public:
    zmGraphicsView(QWidget *parent = 0);
    ~zmGraphicsView();
    void setScene(QGraphicsScene *scene);

public Q_SLOTS:
    void onSceneRectChanged(const QRectF &rect);
    void updateMargins();
    void repaintAll();
//    void processForces();

protected:
    void timerEvent(QTimerEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void processIndications();
    void vfsDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles = QVector<int>());

    GraphicsViewPrivate *d_ptr = nullptr;
};

#endif // ZM_GRAPHICSVIEW_H
