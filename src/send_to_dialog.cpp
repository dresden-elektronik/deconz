/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include "send_to_dialog.h"
#include <QDebug>
#include "ui_send_to_dialog.h"
#include "deconz/util.h"
#include "deconz/util_private.h"

SendToDialog::SendToDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SendToDialog)
{
    ui->setupUi(this);

    connect(ui->broadcastAllRadioButton, SIGNAL(clicked()),
            this, SLOT(displayAddress()));

    connect(ui->broadcastRoutersRadioButton, SIGNAL(clicked()),
            this, SLOT(displayAddress()));

    connect(ui->broadcastRxOnWhenIdleRadioButton, SIGNAL(clicked()),
            this, SLOT(displayAddress()));

    connect(ui->groupRadioButton, SIGNAL(clicked()),
            this, SLOT(displayAddress()));

    connect(ui->unicastRadioButton, SIGNAL(clicked()),
            this, SLOT(displayAddress()));

    connect(ui->addressEdit, SIGNAL(textEdited(QString)),
            this, SLOT(addressEditChanged(QString)));

    connect(ui->endpointEdit, SIGNAL(textEdited(QString)),
            this, SLOT(endpointEditChanged(QString)));
}

SendToDialog::~SendToDialog()
{
    delete ui;
}

deCONZ::Address SendToDialog::address()
{
    deCONZ::Address addr = m_addr;

    if (ui->broadcastAllRadioButton->isChecked())
    {
        addr.setNwk(deCONZ::BroadcastAll);
    }
    else if (ui->broadcastRoutersRadioButton->isChecked())
    {
        addr.setNwk(deCONZ::BroadcastRouters);
    }
    else if (ui->broadcastRxOnWhenIdleRadioButton->isChecked())
    {
        addr.setNwk(deCONZ::BroadcastRxOnWhenIdle);
    }

    return addr;
}

void SendToDialog::setAddress(const deCONZ::Address &addr)
{
    m_addr = addr;
}

quint8 SendToDialog::endpoint()
{
    return m_endpoint;
}

void SendToDialog::setNwkAddress(quint16 nwk)
{
    // remember
    m_addr.setNwk(nwk);
}

void SendToDialog::setGroupAddress(quint16 group)
{
    // remember
    m_addr.setGroup(group);
}

void SendToDialog::setEndpoint(quint8 ep)
{
    if (ep != m_endpoint)
    {
        m_endpoint = ep;

        QString str = QString("0x%1").arg(ep, 2, 16, QLatin1Char('0'));
        ui->endpointEdit->setText(str);
    }
}

deCONZ::ApsAddressMode SendToDialog::addressMode()
{
    if (ui->groupRadioButton->isChecked())
    {
        return deCONZ::ApsGroupAddress;
    }

    return deCONZ::ApsNwkAddress;
}

void SendToDialog::setAddressMode(SendToDialog::AddressMode mode)
{
    switch (mode)
    {
    case BroadcastAll: ui->broadcastAllRadioButton->setChecked(true); break;
    case BroadcastRouters: ui->broadcastRoutersRadioButton->setChecked(true); break;
    case BroadcastRxOnWhenIdle: ui->broadcastRxOnWhenIdleRadioButton->setChecked(true); break;
    case Group: ui->groupRadioButton->setChecked(true); break;
    case Unicast: ui->unicastRadioButton->setChecked(true); break;
    default:
        break;
    }
}

void SendToDialog::reloadAddress()
{
    deCONZ::Address addr;
    quint8 endpoint;
    deCONZ::ApsAddressMode addrMode; // will be ignored

    deCONZ::getDestination(&addr, &addrMode, &endpoint);

    if (addr.isNwkUnicast())
    {
        setNwkAddress(addr.nwk());
    }
    setEndpoint(endpoint);
    displayAddress();
}

void SendToDialog::displayAddress()
{
    quint16 addr;

    if (ui->broadcastAllRadioButton->isChecked())
    {
        addr = deCONZ::BroadcastAll;
    }
    else if (ui->broadcastRoutersRadioButton->isChecked())
    {
        addr = deCONZ::BroadcastRouters;
    }
    else if (ui->broadcastRxOnWhenIdleRadioButton->isChecked())
    {
        addr = deCONZ::BroadcastRxOnWhenIdle;
    }
    else if (ui->groupRadioButton->isChecked())
    {
        addr = m_addr.group();
    }
    else
    {
        addr = m_addr.nwk();
    }

    QString str = "0x" + QString("%1").arg(addr, 4, 16, QLatin1Char('0')).toUpper();
    ui->addressEdit->setText(str);

    // tmp address to preserve unicast address
    deCONZ::Address dst = m_addr;
    if (addressMode() != deCONZ::ApsGroupAddress)
    {
        dst.setNwk(addr); // push broadcast addresses
    }

    deCONZ::setDestination(dst, addressMode(), endpoint());
}

void SendToDialog::addressEditChanged(const QString &text)
{
    quint16 addr = text.toUShort(nullptr, 16);

    if (ui->groupRadioButton->isChecked())
    {
        m_addr.setGroup(addr);
    }
    else if (ui->unicastRadioButton->isChecked())
    {
        m_addr.setNwk(addr);
    }

    displayAddress();
}

void SendToDialog::endpointEditChanged(const QString &text)
{
    quint16 ep = text.toUShort(nullptr, 16);
    m_endpoint = ep;

    displayAddress();
}
