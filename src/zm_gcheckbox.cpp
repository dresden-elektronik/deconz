/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QDebug>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QStylePainter>
#include <QStyleOptionGraphicsItem>
#include <QStyleOptionComplex>

#include "zm_gcheckbox.h"

zmgCheckBox::zmgCheckBox(const QString &text, QGraphicsItem *parent) :
    QGraphicsWidget(parent),
    m_id(-1),
    m_checkRect(0, 0, 20, 20),
    m_state(Qt::Unchecked)
{
    m_text = new QGraphicsSimpleTextItem(text, this);
    m_text->moveBy(m_checkRect.width(), 1);
    m_text->setBrush(palette().window());
    QFont fn = font();
    fn.setPointSize(8);
    m_text->setFont(fn);

    setAcceptedMouseButtons(Qt::LeftButton);
    setPreferredSize(m_checkRect.width() + childrenBoundingRect().width(), m_checkRect.height());
}

QString zmgCheckBox::text() const
{
    return m_text->text();
}

void zmgCheckBox::setText(const QString &text)
{
    m_text->setText(text);
}

void zmgCheckBox::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option)
//    QGraphicsWidget::paint(painter, option, widget);
    QStylePainter p(painter->device(), widget);
    p.setTransform(painter->transform());
    //QStyleOption opt(static_cast<const QStyleOption &>(*option));
    QStyleOptionButton opt;

    if (m_state == Qt::Unchecked)
    {
        opt.state |= QStyle::State_Off;
    }
    else
    {
        opt.state |= QStyle::State_On;
    }

    opt.rect = m_checkRect.toRect();
    opt.text = QLatin1String(" ");

    //p.drawPrimitive(QStyle::PE_IndicatorCheckBox, opt);
    p.drawControl(QStyle::CE_PushButton, opt);
}

void zmgCheckBox::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    event->accept();
}

void zmgCheckBox::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_checkRect.contains(event->pos()))
    {
        if (m_state == Qt::Unchecked)
        {
            m_state = Qt::Checked;
        }
        else
        {
            m_state = Qt::Unchecked;
        }

        emit stateChanged(this, m_state);
        update();
    }

    QGraphicsWidget::mouseReleaseEvent(event);
}
