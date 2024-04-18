/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
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
    m_okButton = ui->buttonBox->button(QDialogButtonBox::Ok);
    connect(ui->categoryListWidget, SIGNAL(clicked(QModelIndex)),
            this, SLOT(categoryClicked(QModelIndex)));

    connect(ui->buttonBox, SIGNAL(clicked(QAbstractButton*)),
            this, SLOT(buttonClicked(QAbstractButton*)));

    // ZCLDB
    m_zcldb = new zmSettingsZcldb(this);
    ui->stackedWidget->addWidget(m_zcldb);

    // Discovery
    zmSettingsDiscovery *discovery = new zmSettingsDiscovery(this);
    ui->stackedWidget->addWidget(discovery);
    connect(m_okButton, SIGNAL(clicked()),
            discovery, SLOT(save()));

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
        m_zcldb->save();
        QString zclFile = deCONZ::getStorageLocation(deCONZ::ZcldbLocation);
        deCONZ::zclDataBase()->initDbFile(zclFile);
        deCONZ::zclDataBase()->reloadAll(zclFile);
    }
        break;

    default:
        break;
    }
}
