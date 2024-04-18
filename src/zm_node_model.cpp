/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QFont>
#include "zm_node.h"
#include "zm_node_model.h"

namespace deCONZ {

NodeModel::NodeModel(QObject *parent) :
    QAbstractTableModel(parent)
{
    m_devState = UnknownState;
    m_sectionNames.append(QLatin1String("MAC"));
    m_sectionNames.append(QLatin1String("NWK"));
    m_sectionNames.append(QLatin1String("Name"));
    m_sectionNames.append(QLatin1String("Model"));
    m_sectionNames.append(QLatin1String("Vendor"));
    m_sectionNames.append(QLatin1String("Version"));
}

int NodeModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_nodes.size();
}

int NodeModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return MaxColumn;
}

QVariant NodeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    if (index.row() >= m_nodes.size())
        return QVariant();

    if (role == Qt::DisplayRole /*|| role == Qt::EditRole*/)
    {
        NodeInfo node = m_nodes[index.row()];

        if (index.column() == MacAddressColumn)
        {
            if (node.data->address().hasExt())
                return toQLatin1String(node.data->extAddressString());
            else return QLatin1String("Unknown");
        }
        else if (index.column() == NwkAddressColumn)
        {
            if (node.data->address().hasNwk())
                return QString("0x%1").arg(node.data->address().nwk(), int(4), int(16), QChar('0'));
            else return QLatin1String("Unknown");
        }
        else if (index.column() == NameColumn)
        {
            return node.data->userDescriptor();
        }
        else if (index.column() == VendorColumn)
        {
            return node.data->vendor();
        }
        else if (index.column() == ModelIdColumn)
        {
            return node.data->modelId();
        }
        else if (index.column() == VersionColumn)
        {
            return node.data->swVersion();
        }
    }
    else if (role == Qt::FontRole)
    {
        switch (index.column())
        {
        case NwkAddressColumn:
        case MacAddressColumn:
        {
            QFont font("Monospace");
            font.setStyleHint(QFont::TypeWriter);
            return font;
        }

        default:
            break;
        }
    }

    return QVariant();
}

QVariant NodeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        if (section >= 0 && section < m_sectionNames.size())
            return m_sectionNames[section];
    }
    return QVariant();
}

void NodeModel::addNode(NodeInfo node)
{
    if (!m_nodes.contains(node))
    {
        beginInsertRows(QModelIndex(), m_nodes.size(), m_nodes.size());
        m_nodes.append(node);
        endInsertRows();
    }
}

void NodeModel::removeNode(NodeInfo node)
{
    int idx = m_nodes.indexOf(node);
    if (idx >= 0)
    {
        beginRemoveRows(QModelIndex(), idx, idx);
        m_nodes.removeOne(node);
        endRemoveRows();
    }
}

void NodeModel::updateNode(NodeInfo node)
{
    int idx = m_nodes.indexOf(node);
    if (idx >= 0)
    {
        QModelIndex index = createIndex(idx, 0, node.id);
        emit dataChanged(index, index);
    }
}


QModelIndex NodeModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);

    if (column >=  0 && column < MaxColumn && row >= 0 && row < m_nodes.size())
    {
        return createIndex(row, column);
    }

    return QModelIndex();
}

void NodeModel::setDeviceState(State state)
{
    if (m_devState != state)
    {
        m_devState = state;
        beginResetModel();

        endResetModel();
    }
}

} // namespace deCONZ
