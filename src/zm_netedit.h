/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_NETEDIT_H
#define ZM_NETEDIT_H

#include <QDialog>
#include <vector>

#include "deconz/types.h"
#include "deconz/zdp_descriptors.h"
#include "deconz/net_descriptor.h"

class zmNetEdit;
class QCheckBox;
class QLineEdit;
class QGroupBox;
class QLabel;
class QVBoxLayout;
class QTimer;
class zmNetDescriptorModel;

namespace Ui {
    class zmNetEdit;
}
namespace deCONZ
{
zmNetEdit *netEdit();

enum ConfigEvent
{
    ConfigPush,
    ConfigDone
};
}

class zmNetEdit : public QDialog
{
    Q_OBJECT

public:
    explicit zmNetEdit(QWidget *parent = 0);
    void setNetDescriptorModel(zmNetDescriptorModel *model);
    void setHAConfig(QVariantMap epData);
    QVariantMap getHAConfig(int index);
    ~zmNetEdit();

public slots:
    void onReadParameterResponse();
    void onUpdatedCurrentNetwork();
    void setNetwork(const zmNet &net);
    void setSimpleDescriptor(quint8 index, const deCONZ::SimpleDescriptor &descriptor);
    void configTimeout();
    void setDeviceState(deCONZ::State state);
    void predefinedPanIdToggled(bool checked);
    void staticNwkAddressToggled(bool checked);
    void customMacAddressToggled(bool checked);
    void onNetStartDone(uint8_t zdoStatus);
    void init();
    bool apsAcksEnabled();
    bool staticNwkAddress();
    void setApsAcksEnabled(bool enabled);
    void checkFeatures();

private slots:
    void onRefresh();
    void onAccept();
    void setButtons();

Q_SIGNALS:
    void restartButtonPressed();

private:  
    struct Endpoint {
        QGroupBox *groupBox;
        QLineEdit *endpoint;
        QLineEdit *profileId;
        QLineEdit *deviceId;
        QLineEdit *deviceVersion;
        QLineEdit *inClusters;
        QLineEdit *outClusters;
        int index;
        deCONZ::SimpleDescriptor descriptor;
    };

    enum NetEditState
    {
        IdleState,
        BusyState
    };

    Endpoint *getEndpointWidget(int index);
    void setEndpointData(Endpoint *ep);
    void getEndpointData(Endpoint *ep);

    Ui::zmNetEdit *ui = nullptr;
    zmNetDescriptorModel *m_model = nullptr;
    QList<QCheckBox*> m_channels;
    std::vector<Endpoint*> m_endpoints;
    QVBoxLayout *m_endpointLayout;
    int m_configCount;
    QTimer *m_configTimer = nullptr;
    NetEditState m_state;
};

#endif // ZM_NETEDIT_H
