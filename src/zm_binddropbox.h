/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_BINDDROPBOX_H
#define ZM_BINDDROPBOX_H
#include <QDebug>
#include <QWidget>

namespace Ui {
    class zmBindDropbox;
}

class zmBindDropbox;
class QAbstractButton;
class QLabel;
class QTimer;

namespace deCONZ
{
struct BindReq;
class ApsDataIndication;
zmBindDropbox *bindDropBox();
}

class zmBindDropbox : public QWidget
{
    Q_OBJECT

public:
    explicit zmBindDropbox(QWidget *parent = 0);
    ~zmBindDropbox();

public Q_SLOTS:
    void bind();
    void unbind();
    void bindIndCallback(const deCONZ::ApsDataIndication &ind);
    void bindTimeout();

protected:
    void dragEnterEvent(QDragEnterEvent *event);
    void dragMoveEvent(QDragMoveEvent *event);
    void dropEvent(QDropEvent *event);

private Q_SLOTS:
    void dstRadioButtonClicked(QAbstractButton *button);
    void dstGroupTextChanged(const QString &text);
    void checkButtons();

private:
    bool setU8(QLabel *label, quint8 *value, const QString &source);
    bool setU16(QLabel *label, quint16 *value, const QString &source);
    bool setU64(QLabel *label, quint64 *value, const QString &source);
    bool hasDstData();
    void clear();
    QTimer *m_timer;
    Ui::zmBindDropbox *ui;
    bool m_hasSrcData;
    quint64 m_srcAddr;
    quint64 m_dstAddr;
    quint16 m_dstGroupAddr;
    quint64 m_binderAddr;
    quint8 m_srcEndpoint;
    quint8 m_dstEndpoint;
    quint16 m_cluster;
};

#endif // ZM_BINDDROPBOX_H
