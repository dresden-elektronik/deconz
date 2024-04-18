/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_NETDESCRIPTOR_MODEL_H
#define ZM_NETDESCRIPTOR_MODEL_H

#include <QObject>

class zmNet;
class NetDescriptorModelPrivate;

class zmNetDescriptorModel : public QObject
{
    Q_OBJECT

public:
    explicit zmNetDescriptorModel(QObject *parent = nullptr);
    ~zmNetDescriptorModel();
    zmNet &currentNetwork();
    const zmNet &currentNetwork() const;
    void setCurrentNetwork(const zmNet &net);

signals:
    void updatedCurrentNetwork();

private:
    NetDescriptorModelPrivate *d = nullptr;
};

namespace deCONZ
{
    zmNetDescriptorModel *netModel();
}

#endif // ZM_NETDESCRIPTOR_MODEL_H
