/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_NODE_INFO_H
#define ZM_NODE_INFO_H

#include <QWidget>
#include <array>

namespace Ui {
    class zmNodeInfo;
}

class zmNodeInfo;
class zmgNode;
class QStandardItem;
class QModelIndex;
class ActorVfsModel;

namespace deCONZ {
    class zmNode;
    zmNodeInfo *nodeInfo();
}

class zmNodeInfo : public QWidget
{
    Q_OBJECT

public:
    explicit zmNodeInfo(QWidget *parent = nullptr);
    ~zmNodeInfo();
    void setNode(ActorVfsModel *vfs, uint64_t mac);
    void setNode(deCONZ::zmNode *data);
    void dataChanged(deCONZ::zmNode *data);

private slots:

private:
    enum NodeInfoState
    {
        Idle,
        Timeout
    };

    void clear();
    void stateCheck();
    void setValue(size_t idx, const QVariant &value);

    Ui::zmNodeInfo *ui = nullptr;
    deCONZ::zmNode *m_data = nullptr;

    struct InfoKeyValue
    {
        QStandardItem *key = nullptr;
        QStandardItem *value = nullptr;
    };

    NodeInfoState m_state = Idle;
    std::array<InfoKeyValue, 36> m_info{};
};

#endif // ZM_NODE_INFO_H
