/*
 * Copyright (c) 2013-2026 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef SETTINGS_PROXY_H
#define SETTINGS_PROXY_H

#include <QWidget>

namespace Ui {
    class SettingsProxy;
}

class SettingsProxy : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsProxy(QWidget *parent = 0);
    ~SettingsProxy();

public Q_SLOTS:
    void save();
    void load();

private Q_SLOTS:
    void httpProxyEnabledChanged();

private:
    // to check for changes track former set values (on load)
    bool m_httpProxyEnabled = false;
    QString m_httpProxy;

    Ui::SettingsProxy *ui = nullptr;
};

#endif // SETTINGS_PROXY_H
