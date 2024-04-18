/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QApplication>
#include <QPalette>
#include <QPainter>
#include "zm_gsocket.h"
#include "zm_glink.h"


NodeSocket::NodeSocket(Direction direction, QGraphicsItem *parent) :
        QGraphicsRectItem(parent)
{
    look_direction = direction;
    radius = 5;
    setRect(0, 0, radius, radius);
    setPen(Qt::NoPen);
    setBrush((Qt::NoBrush));

    moveBy(3, 3);
}

NodeSocket::~NodeSocket()
{
	for (int i = 0; i < links.size(); i++) {
		links[i]->remSocket(this);
	}
}

void NodeSocket::disconnect(NodeSocket *other)
{
	int idx = sockets.indexOf(other);

	if (idx == -1)
		return;

	sockets.removeAt(idx);
}

void NodeSocket::connect(NodeSocket *other)
{
	if (this == other)
		return;

    if (sockets.indexOf(other) == -1)
    {
        sockets.push_back(other);
    }
}

void NodeSocket::addLink(NodeLink *link)
{
    if (links.indexOf(link) == -1)
    {
        links.push_back(link);
    }
}
