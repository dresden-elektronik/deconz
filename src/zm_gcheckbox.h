/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_GCHECKBOX_H
#define ZM_GCHECKBOX_H

#include <QGraphicsWidget>

class zmgCheckBox : public QGraphicsWidget
{
    Q_OBJECT

public:
    explicit zmgCheckBox(const QString &text, QGraphicsItem *parent = 0);
    bool isChecked() { return (m_state == Qt::Checked); }
    void setChecked(bool checked) { m_state = (checked) ? Qt::Checked : Qt::Unchecked; update(); }
    void setText(const QString &text);
    QString text() const;
    void setId(int id) { m_id = id; }
    int id() const { return m_id; }

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
    void mousePressEvent(QGraphicsSceneMouseEvent *event);
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event);

Q_SIGNALS:
    void stateChanged(zmgCheckBox *checkBox, int state);

private:
    int m_id;
    QGraphicsSimpleTextItem *m_text;
    QRectF m_checkRect;
    int m_state;
};

#endif // ZM_GCHECKBOX_H
