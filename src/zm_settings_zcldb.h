/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_SETTINGS_ZCLDB_H
#define ZM_SETTINGS_ZCLDB_H

#include <QWidget>

namespace Ui {
    class zmSettingsZcldb;
}

class QModelIndex;
class QStringListModel;

class zmSettingsZcldb : public QWidget
{
    Q_OBJECT

public:
    explicit zmSettingsZcldb(QWidget *parent = 0);
    ~zmSettingsZcldb();

public Q_SLOTS:
    void save();
    void load();
    void addItem();
    void removeItem();
    void itemClicked(const QModelIndex &index);

Q_SIGNALS:
    void dataChanged();

private:
    bool m_dataChanged;
    Ui::zmSettingsZcldb *ui;
    QStringListModel *m_model;
    int m_selectedRow;
    QString m_lastAddPath;
};

#endif // ZM_SETTINGS_ZCLDB_H
