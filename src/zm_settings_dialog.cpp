/*
 * Copyright (c) 2013-2026 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QPushButton>
#include "zm_settings_dialog.h"
#include "ui_zm_settings_dialog.h"
#include "zm_settings_zcldb.h"
#include "zm_settings_discovery.h"
#include "gui/settings_proxy.h"
#include "deconz/util.h"
#include "deconz/zcl.h"
#include "zcl_private.h"

zmSettingsDialog::zmSettingsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::zmSettingsDialog)
{
    ui->setupUi(this);
    ui->categoryListWidget->setIconSize(QSize(48, 48));
    setWindowTitle(tr("deCONZ Preferences"));
    QPushButton *okButton = ui->buttonBox->button(QDialogButtonBox::Ok);
    QPushButton *cancelButton = ui->buttonBox->button(QDialogButtonBox::Cancel);

    connect(ui->categoryListWidget, SIGNAL(clicked(QModelIndex)),
            this, SLOT(categoryClicked(QModelIndex)));

    connect(ui->buttonBox, SIGNAL(clicked(QAbstractButton*)),
            this, SLOT(buttonClicked(QAbstractButton*)));

    okButton->setIconSize(QSize(0, 0));
    cancelButton->setIconSize(QSize(0, 0));

    // ZCLDB
    m_zcldb = new zmSettingsZcldb(this);
    m_zcldb->load();
    ui->stackedWidget->addWidget(m_zcldb);

    // Discovery
    m_discovery = new zmSettingsDiscovery(this);
    ui->stackedWidget->addWidget(m_discovery);

    // Proxy
    m_proxy = new SettingsProxy(this);
    ui->stackedWidget->addWidget(m_proxy);

    ui->stackedWidget->setCurrentIndex(0);
}

zmSettingsDialog::~zmSettingsDialog()
{
    delete ui;
}

void zmSettingsDialog::categoryClicked(const QModelIndex &index)
{
    if (index.isValid())
    {
        ui->stackedWidget->setCurrentIndex(index.row());

        if (ui->stackedWidget->currentWidget() == m_zcldb)
        {
            m_zcldb->load();
        }
        else if (ui->stackedWidget->currentWidget() == m_discovery)
        {
            m_discovery->load();
        }
        else if (ui->stackedWidget->currentWidget() == m_proxy)
        {
            m_proxy->load();
        }
    }
}

void zmSettingsDialog::dataChanged()
{
}

void zmSettingsDialog::buttonClicked(QAbstractButton *button)
{
    switch (ui->buttonBox->standardButton(button))
    {
    case QDialogButtonBox::Ok:
    {
        if (ui->stackedWidget->currentWidget() == m_discovery)
        {
            m_discovery->save();
        }
        else if (ui->stackedWidget->currentWidget() == m_zcldb)
        {
            m_zcldb->save();
            QString zclFile = deCONZ::getStorageLocation(deCONZ::ZcldbLocation);
            deCONZ::zclDataBase()->initDbFile(zclFile);
            deCONZ::zclDataBase()->reloadAll(zclFile);
        }
        else if (ui->stackedWidget->currentWidget() == m_proxy)
        {
            m_proxy->save();
        }
    }
        break;

    default:
        break;
    }
}
