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
#include <QMap>
#include <QWidget>
#include <QSet>

namespace Ui {
    class zmBindDropbox;
}

struct BindingEntry
{
    quint64 srcAddr;
    quint8 srcEndpoint;
    quint16 clusterId;
    quint8 dstAddrMode;
    quint64 dstExtAddr;
    quint16 dstGroupAddr;
    quint8 dstEndpoint;

    bool operator==(const BindingEntry &other) const
    {
        return srcAddr == other.srcAddr &&
        srcEndpoint == other.srcEndpoint &&
        clusterId == other.clusterId &&
        dstAddrMode == other.dstAddrMode &&
        dstExtAddr == other.dstExtAddr &&
        dstGroupAddr == other.dstGroupAddr &&
        dstEndpoint == other.dstEndpoint;
    }
};

inline uint qHash(const BindingEntry &key, uint seed = 0)
{
    return qHash(key.srcAddr, seed) ^ qHash(key.srcEndpoint) ^ qHash(key.clusterId) ^
    qHash(key.dstAddrMode) ^ qHash(key.dstExtAddr) ^ qHash(key.dstGroupAddr) ^ qHash(key.dstEndpoint);
}

class zmBindDropbox;
class QAbstractButton;
class QLabel;
class QTableWidget;
class QTimer;

namespace deCONZ
{
struct BindReq;
class ApsDataIndication;
class BindingTable;
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
    void mgmtBindRspCallback(quint64 srcAddr, quint8 status, quint8 entries, quint8 startIndex, quint8 listCount, const deCONZ::BindingTable &table);
    void bindTimeout();
    void setSelectedNode(quint64 nodeAddr);

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
    void updateBindingTableView(quint64 srcAddr, const deCONZ::BindingTable &table);
    QString formatAddress64(quint64 value) const;
    QString formatHex16(quint16 value) const;
    QString formatHex8(quint8 value) const;
    QString clusterName(quint64 srcAddr, quint8 srcEndpoint, quint16 clusterId) const;
    bool hasDstData();
    void clear();
    void rebuildBindingTableView();

    QTimer *m_timer;
    QLabel *m_bindingTableInfo;
    QTableWidget *m_bindingTableView;
    Ui::zmBindDropbox *ui;
    bool m_hasSrcData;
    quint64 m_srcAddr;
    quint64 m_dstAddr;
    quint16 m_dstGroupAddr;
    quint64 m_binderAddr;
    quint8 m_srcEndpoint;
    quint8 m_dstEndpoint;
    quint16 m_cluster;
    quint64 m_selectedNodeAddr;
    QMap<quint64, QSet<BindingEntry>> m_bindingCache;
};

#endif // ZM_BINDDROPBOX_H
