/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include "zm_settings_discovery.h"
#include "ui_zm_settings_discovery.h"
#include "zm_node.h"

zmSettingsDiscovery::zmSettingsDiscovery(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::zmSettingsDiscovery)
{
    ui->setupUi(this);
    load();
}

zmSettingsDiscovery::~zmSettingsDiscovery()
{
    delete ui;
}

void zmSettingsDiscovery::save()
{
    deCONZ::setFetchInterval(deCONZ::ReqNwkAddr, ui->nwkAddrReqLineEdit->text().toInt());
    deCONZ::setFetchInterval(deCONZ::ReqMgmtLqi, ui->mgmtLqiReqLineEdit->text().toInt());
}

void zmSettingsDiscovery::load()
{
    ui->nwkAddrReqLineEdit->setText(QString::number(deCONZ::getFetchInterval(deCONZ::ReqNwkAddr)));
    ui->mgmtLqiReqLineEdit->setText(QString::number(deCONZ::getFetchInterval(deCONZ::ReqMgmtLqi)));
}
