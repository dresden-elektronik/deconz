/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_NODE_MODEL_H
#define ZM_NODE_MODEL_H

#include <QAbstractTableModel>
#include <QStringList>
#include "deconz/types.h"
#include "zm_node.h"

namespace deCONZ
{

class NodeModel;

NodeModel *nodeModel();

class NodeModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Columns {
        MacAddressColumn,
        NwkAddressColumn,
        NameColumn,
        ModelIdColumn,
        VendorColumn,
        VersionColumn,

        MaxColumn
    };
    explicit NodeModel(QObject *parent = 0);
    void addNode(NodeInfo node);
    void removeNode(NodeInfo node);
    void updateNode(NodeInfo node);
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    int columnCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;
    QModelIndex index(int row, int column, const QModelIndex &parent) const;

public Q_SLOTS:
    void setDeviceState(State state);

signals:
    void dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight);

private:
    QList<NodeInfo> m_nodes;
    QStringList m_sectionNames;
    State m_devState;
};

}  // namespace deCONZ

#endif // ZM_NODE_MODEL_H
