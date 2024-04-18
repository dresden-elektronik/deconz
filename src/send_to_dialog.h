/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef SEND_TO_DIALOG_H
#define SEND_TO_DIALOG_H

#include <QDialog>
#include "deconz/types.h"
#include "deconz/aps.h"

namespace Ui {
class SendToDialog;
}

class SendToDialog : public QDialog
{
    Q_OBJECT
    
public:
    explicit SendToDialog(QWidget *parent = 0);
    ~SendToDialog();

    enum AddressMode
    {
        BroadcastAll,
        BroadcastRouters,
        BroadcastRxOnWhenIdle,
        Group,
        Unicast
    };

    deCONZ::Address address();
    void setAddress(const deCONZ::Address &addr);
    quint8 endpoint();
    void setEndpoint(quint8 ep);
    void setNwkAddress(quint16 nwk);
    void setGroupAddress(quint16 group);
    deCONZ::ApsAddressMode addressMode();
    void setAddressMode(AddressMode mode);

public Q_SLOTS:
    void reloadAddress();
    void displayAddress();
    void addressEditChanged(const QString &text);
    void endpointEditChanged(const QString &text);

private:
    Ui::SendToDialog *ui;
    quint8 m_endpoint;
    deCONZ::Address m_addr;
};

#endif // SEND_TO_DIALOG_H
