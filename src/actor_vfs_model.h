/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ACTORVFSMODEL_H
#define ACTORVFSMODEL_H

#include <QAbstractItemModel>

struct am_message;
class ActorVfsModelPrivate;

class ActorVfsModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Column
    {
        ColumnName = 0,
        ColumnType = 1,
        ColumnValue = 2,

        ColumnMax = 3
    };

    explicit ActorVfsModel(QObject *parent = nullptr);
    ~ActorVfsModel();

    QVariant data(const QModelIndex &index, int role) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    int listDirectoryResponse(struct am_message *msg);
    int readEntryResponse(struct am_message *msg);
    void continueFetching();

    void addActorId(unsigned actorId);

private Q_SLOTS:
    void fetchTimerFired();

private:

    ActorVfsModelPrivate *priv = nullptr;
};

#endif // ACTORVFSMODEL_H
