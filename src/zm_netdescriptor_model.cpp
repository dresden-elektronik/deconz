/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include "deconz/net_descriptor.h"
#include "zm_netdescriptor_model.h"

class NetDescriptorModelPrivate
{
public:
    zmNet network;
};

zmNetDescriptorModel::zmNetDescriptorModel(QObject *parent) :
    QObject(parent)
{
    d = new NetDescriptorModelPrivate;
}

zmNetDescriptorModel::~zmNetDescriptorModel()
{
    delete d;
    d = nullptr;
}

zmNet &zmNetDescriptorModel::currentNetwork()
{
    return d->network;
}

const zmNet &zmNetDescriptorModel::currentNetwork() const
{
    return d->network;
}

void zmNetDescriptorModel::setCurrentNetwork(const zmNet &net)
{
    if (&d->network != &net)
    {
        d->network = net;
    }
    emit updatedCurrentNetwork();
}
