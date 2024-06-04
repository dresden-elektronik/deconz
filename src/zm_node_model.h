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
#include <deconz/types.h>

namespace deCONZ
{

class NodeModel;
class NodeModelPrivate;

NodeModel *nodeModel();

/*
 * TODO(mpi): Refactor for GUI separation
 *
 *    - The deCONZ::NodeModel must only be part of the GUI
 *    - Remove from deCONZ::Controller
 */

class NodeModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column
    {
        MacAddressColumn,
        NwkAddressColumn,
        NameColumn,
        ModelIdColumn,
        VendorColumn,
        VersionColumn,

        MaxColumn
    };
    explicit NodeModel(QObject *parent = 0);
    ~NodeModel();
    void addNode(uint64_t extAddr, uint16_t nwkAddr);
    void removeNode(uint64_t extAddr);
    void setData(uint64_t extAddr, Column column, const QVariant &data);
    QVariant data(uint64_t extAddr, Column column) const;
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    int columnCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;
    QModelIndex index(int row, int column, const QModelIndex &parent) const;

public Q_SLOTS:
    void setDeviceState(State state);

private:
    NodeModelPrivate *d_ptr2 = nullptr;
};

}  // namespace deCONZ

#endif // ZM_NODE_MODEL_H
