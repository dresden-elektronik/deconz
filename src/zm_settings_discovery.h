/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_SETTINGS_DISCOVERY_H
#define ZM_SETTINGS_DISCOVERY_H

#include <QWidget>

namespace Ui {
    class zmSettingsDiscovery;
}

class zmSettingsDiscovery : public QWidget
{
    Q_OBJECT

public:
    explicit zmSettingsDiscovery(QWidget *parent = 0);
    ~zmSettingsDiscovery();

public Q_SLOTS:
    void save();
    void load();

private:
    Ui::zmSettingsDiscovery *ui;
};

#endif // ZM_SETTINGS_DISCOVERY_H
