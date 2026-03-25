/*
 * Copyright (c) 2013-2026 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <deconz/aps_controller.h>

#include "settings_proxy.h"
#include "ui_settings_proxy.h"

SettingsProxy::SettingsProxy(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsProxy)

{
    ui->setupUi(this);
    ui->checkBoxHttpProxy->setChecked(false);
    ui->lineEditHttpProxy->setEnabled(false);
    connect(ui->checkBoxHttpProxy, &QCheckBox::clicked, this, &SettingsProxy::httpProxyEnabledChanged);
}

SettingsProxy::~SettingsProxy()
{
    delete ui;
}

void SettingsProxy::save()
{
    if (m_httpProxyEnabled != ui->checkBoxHttpProxy->isChecked() ||
        m_httpProxy != ui->lineEditHttpProxy->text())
    {
        // TODO(mpi): query parameters via AM
        auto *apsCtrl = deCONZ::ApsController::instance();
        if (!apsCtrl)
            return;

        if (ui->checkBoxHttpProxy->isChecked())
        {
            QString httpProxy = ui->lineEditHttpProxy->text();

            int scheme = httpProxy.indexOf("://");
            if (scheme > 0)
            {
                httpProxy.remove(0, scheme + 3);
            }

            if (!httpProxy.contains(':'))
                return; // must have a port

            auto ls = httpProxy.split(':');
            if (ls.size() != 2 || ls[0].isEmpty() || ls[1].isEmpty())
                return;

            int port = ls[1].toInt();
            if (port <= 0 || port > 0xFFFF)
                return;

            apsCtrl->setParameter(deCONZ::ParamHttpProxy, ls[0]);
            apsCtrl->setParameter(deCONZ::ParamHttpProxyPort, (uint16_t)port);
        }
    }
}

void SettingsProxy::load()
{
    // TODO(mpi): query parameters via AM
    auto *apsCtrl = deCONZ::ApsController::instance();
    if (!apsCtrl)
        return;

    QString httpProxy = apsCtrl->getParameter(deCONZ::ParamHttpProxy);
    uint16_t httpProxyPort = apsCtrl->getParameter(deCONZ::ParamHttpProxyPort);

    if (httpProxy.isEmpty() || httpProxy == "none" || httpProxyPort == 0)
    {
        ui->lineEditHttpProxy->clear();
        m_httpProxyEnabled = true;
        m_httpProxy.clear();
    }
    else
    {
        if (!httpProxy.startsWith("http://"))
            httpProxy = "http://" + httpProxy;
        ui->lineEditHttpProxy->setText(QString("%1:%2").arg(httpProxy).arg(QString::number(httpProxyPort)));
        m_httpProxyEnabled = true;
        m_httpProxy = ui->lineEditHttpProxy->text();
    }

    ui->lineEditHttpProxy->setEnabled(m_httpProxyEnabled);
    ui->checkBoxHttpProxy->setChecked(m_httpProxyEnabled);
}

void SettingsProxy::httpProxyEnabledChanged()
{
    ui->lineEditHttpProxy->setEnabled(ui->checkBoxHttpProxy->isChecked());
}
