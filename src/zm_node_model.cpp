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
#include <QStringList>
#include <vector>
#include "zm_node_model.h"

namespace deCONZ {

class NodeModelEntry
{
public:
    QString macAddress;
    QString nwkAddress;
    QString name;
    QString model;
    QString vendor;
    QString version;
    uint64_t mac;
    uint16_t nwk;
};

class NodeModelPrivate
{
public:
    std::vector<NodeModelEntry> entries;
    QStringList m_sectionNames;
    State m_devState;
};

NodeModel::NodeModel(QObject *parent) :
    QAbstractTableModel(parent),
    d_ptr2(new NodeModelPrivate)
{
    d_ptr2->m_devState = UnknownState;
    d_ptr2->m_sectionNames.append(QLatin1String("MAC"));
    d_ptr2->m_sectionNames.append(QLatin1String("NWK"));
    d_ptr2->m_sectionNames.append(QLatin1String("Name"));
    d_ptr2->m_sectionNames.append(QLatin1String("Model"));
    d_ptr2->m_sectionNames.append(QLatin1String("Vendor"));
    d_ptr2->m_sectionNames.append(QLatin1String("Version"));
}

NodeModel::~NodeModel()
{
    delete d_ptr2;
    d_ptr2 = nullptr;
}

int NodeModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return (int)d_ptr2->entries.size();
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

    if (index.row() >= d_ptr2->entries.size())
        return QVariant();

    if (role == Qt::DisplayRole /*|| role == Qt::EditRole*/)
    {
        const NodeModelEntry &entry = d_ptr2->entries[index.row()];

        if (index.column() == MacAddressColumn)
        {
            return entry.macAddress;
        }
        else if (index.column() == NwkAddressColumn)
        {
            return entry.nwkAddress;
        }
        else if (index.column() == NameColumn)
        {
            return entry.name;
        }
        else if (index.column() == VendorColumn)
        {
            return entry.vendor;
        }
        else if (index.column() == ModelIdColumn)
        {
            return entry.model;
        }
        else if (index.column() == VersionColumn)
        {
            return entry.version;
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
        if (section >= 0 && section < d_ptr2->m_sectionNames.size())
            return d_ptr2->m_sectionNames[section];
    }
    return QVariant();
}

void NodeModel::addNode(uint64_t extAddr, uint16_t nwkAddr)
{
    if (extAddr == 0)
    {
        return;
    }

    for (size_t i = 0; i < d_ptr2->entries.size(); i++)
    {
        if (d_ptr2->entries[i].mac == extAddr)
        {
            return;
        }
    }

    int row = (int)d_ptr2->entries.size();
    beginInsertRows(QModelIndex(), row, row);

    NodeModelEntry entry;
    entry.mac = extAddr;
    entry.nwk = nwkAddr;
    entry.macAddress = QString("0x%1").arg(extAddr, int(16), int(16), QChar('0'));;
    entry.nwkAddress = QString("0x%1").arg(nwkAddr, int(4), int(16), QChar('0'));

    d_ptr2->entries.push_back(entry);

    endInsertRows();
}

void NodeModel::removeNode(uint64_t extAddr)
{
    for (size_t i = 0; i < d_ptr2->entries.size(); i++)
    {
        if (d_ptr2->entries[i].mac == extAddr)
        {
            beginRemoveRows(QModelIndex(), i, i);
            d_ptr2->entries[i] = d_ptr2->entries.back();
            d_ptr2->entries.pop_back();
            endRemoveRows();
            return;
        }
    }
}

void NodeModel::setData(uint64_t extAddr, Column column, const QVariant &data)
{
    for (size_t i = 0; i < d_ptr2->entries.size(); i++)
    {
        NodeModelEntry &entry = d_ptr2->entries[i];
        if (entry.mac == extAddr)
        {

            bool updated = false;
            if (column == NwkAddressColumn && entry.nwk != data.toUInt())
            {
                    entry.nwk = data.toUInt();
                    entry.nwkAddress = QString("0x%1").arg(entry.nwk, int(4), int(16), QChar('0'));
                    updated = true;
            }

            if (column == ModelIdColumn && entry.model != data.toString())
            {
                entry.model = data.toString();
                updated = true;
            }

            if (column == VendorColumn && entry.vendor != data.toString())
            {
                entry.vendor = data.toString();
                updated = true;
            }

            if (column == NameColumn && entry.name != data.toString())
            {
                entry.name = data.toString();
                updated = true;
            }

            if (column == VersionColumn && entry.version != data.toString())
            {
                entry.version = data.toString();
                updated = true;
            }

            if (updated)
            {
                QModelIndex topLeft = index((int)i, column, QModelIndex());
                QModelIndex bottomRight = index((int)i, column, QModelIndex());
                emit dataChanged(topLeft, bottomRight, { Qt::DisplayRole });
            }
            return;
        }
    }
}

QVariant NodeModel::data(uint64_t extAddr, Column column) const
{
    for (size_t i = 0; i < d_ptr2->entries.size(); i++)
    {
        if (d_ptr2->entries[i].mac == extAddr)
        {
            return data(index((int)i, column, QModelIndex()), Qt::DisplayRole);
        }
    }

    return QVariant();
}


QModelIndex NodeModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);

    if (column >= 0 && column < MaxColumn && row >= 0 && row < (int)d_ptr2->entries.size())
    {
        const NodeModelEntry &entry = d_ptr2->entries[row];
        return createIndex(row, column, nullptr);
    }

    return QModelIndex();
}

void NodeModel::setDeviceState(State state)
{
    if (d_ptr2->m_devState != state)
    {
        d_ptr2->m_devState = state;
        beginResetModel();

        endResetModel();
    }
}

} // namespace deCONZ
