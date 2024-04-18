/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef SLOT_H
#define SLOT_H

#include <QBrush>
#include <QGraphicsRectItem>
#include <QColor>
//#include <QVariant>
#include <QList>

#include "deconz/types.h"

class NodeLink;

class NodeSocket : public QGraphicsRectItem
{
public:
	enum Direction { LookLeft, LookTop, LookRight, LookBottom };

	NodeSocket(Direction direction, QGraphicsItem *parent = 0);
	~NodeSocket();

	void disconnect(NodeSocket *other);
	void connect(NodeSocket *other);

    void addLink(NodeLink *link);
    void removeLink(NodeLink *link) { links.removeOne(link); }

	void setData(const QVariant &data) { mydata = data; }
	QVariant data() const { return mydata; }
	const QColor &color() const { return brush_color; }
	void setColor(const QColor &c) { brush_color = c; setBrush(QBrush(c)); }

	Direction lookDirection() const { return look_direction; }

    enum { Type = UserType + deCONZ::GraphSocketType };
	int type() const { return Type; }

private:
	Direction look_direction;
	qreal radius;
	QColor brush_color;

	QList<NodeSocket*> sockets;
	QList<NodeLink*> links;
	QVariant mydata;
};

#endif // SLOT_H
