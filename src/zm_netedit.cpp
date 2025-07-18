/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QDebug>
#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QTimer>
#include <QStandardItemModel>
#include <QFontDatabase>
#include "gui/theme.h"
#include "zm_netedit.h"
#include "zm_netdescriptor_model.h"
#include "zm_controller.h"
#include "zm_master.h"
#include "ui_zm_netedit.h"
#include "deconz/dbg_trace.h"

zmNetEdit::zmNetEdit(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::zmNetEdit)
{
    ui->setupUi(this);

    setWindowTitle(tr("deCONZ Network Settings"));

    QFont monoFont = Theme_FontMonospace();

    ui->panIdEdit->setFont(monoFont);
    ui->extPanIdEdit->setFont(monoFont);
    ui->apsUseExtPanIdEdit->setFont(monoFont);
    ui->extEdit->setFont(monoFont);
    ui->nwkEdit->setFont(monoFont);
    ui->tcAddressEdit->setFont(monoFont);
    ui->networkKeyEdit->setFont(monoFont);
    ui->tcLinkKeyEdit->setFont(monoFont);
    ui->tcMasterKeyEdit->setFont(monoFont);

    connect(ui->refreshButton, SIGNAL(clicked()),
            this, SLOT(onRefresh()));

    connect(ui->acceptButton, SIGNAL(clicked()),
            this, SLOT(onAccept()));

    connect(ui->predefinedPanIdCheckBox, SIGNAL(clicked(bool)),
            this, SLOT(predefinedPanIdToggled(bool)));

    connect(ui->staticNwkAddrCheckBox, SIGNAL(clicked(bool)),
            this, SLOT(staticNwkAddressToggled(bool)));

    connect(ui->customMacAddrCheckBox, SIGNAL(clicked(bool)),
            this, SLOT(customMacAddressToggled(bool)));

    connect(ui->doneButton, SIGNAL(clicked()),
            this, SLOT(hide()));

    m_configTimer = new QTimer(this);
    m_configTimer->setInterval(700);
    m_configTimer->setSingleShot(true);
    connect(m_configTimer, SIGNAL(timeout()),
            this, SLOT(configTimeout()));

    QGridLayout *grid = new QGridLayout(ui->channelMaskwidget);

    int col = 0;
    int j = 0;
    for (int i = 11; i < 27; i++, j++)
    {
        QLabel *label = new QLabel(QString("%1").arg(i));
        QCheckBox *chBox = new QCheckBox;
        grid->addWidget(label, 0, col);
        grid->addWidget(chBox, 1, col);
        m_channels.append(chBox);
        col++;
    }

    grid->setMargin(4);

    ui->endpointGroupBox->setEnabled(true);
    m_endpointLayout = new QVBoxLayout(ui->endpointGroupBox);
    ui->endpointGroupBox->setLayout(m_endpointLayout);
    m_configCount = 0;
    ui->configStatus->clear();
    m_state = IdleState;

    // hide depricated ZLL tab
    ui->tabWidget->removeTab(2);
}

void zmNetEdit::setNetDescriptorModel(zmNetDescriptorModel *model)
{
     m_model = model;
}

zmNetEdit::~zmNetEdit()
{
    for (auto *&e : m_endpoints)
    {
        delete e;
        e = nullptr;
    }
    m_endpoints.clear();
    delete ui;
}

void zmNetEdit::onReadParameterResponse()
{
    m_configTimer->stop();
    m_configTimer->start();
}

void zmNetEdit::onUpdatedCurrentNetwork()
{
    if (m_model)
    {
        setNetwork(m_model->currentNetwork());
    }
}

static const char *strNwkAddress(char *buf, uint16_t nwk)
{
    snprintf(buf, 23, "0x%04x", nwk);
    return buf;
}

static const char *strExtAddress(char *buf, unsigned long long ext)
{
    snprintf(buf, 23, "0x%016llx", ext);
    return buf;
}

void zmNetEdit::setNetwork(const zmNet &net)
{
    char buf[24];

    ui->panIdEdit->setText(strNwkAddress(buf, net.pan().nwk()));
    ui->extPanIdEdit->setText(strExtAddress(buf, net.pan().ext()));
    ui->apsUseExtPanIdEdit->setText(strExtAddress(buf, net.panAps().ext()));
    ui->extEdit->setText(strExtAddress(buf, net.ownAddress().ext()));
    ui->nwkEdit->setText(strNwkAddress(buf, net.ownAddress().nwk()));

    ui->zllActiveCheckBox->setChecked(net.connectMode() == deCONZ::ConnectModeZll);
    ui->zllFactoryNewCheckBox->setChecked(net.zllFactoryNew());

    if (!net.trustCenterLinkKey().isEmpty())
    {
        ui->tcLinkKeyEdit->setText(QString("0x") + net.trustCenterLinkKey().toHex());
    }
    else
    {
        ui->tcLinkKeyEdit->setText("0x00000000000000000000000000000000");
    }

    if (!net.trustCenterMasterKey().isEmpty())
    {
        ui->tcMasterKeyEdit->setText(QString("0x") + net.trustCenterMasterKey().toHex());
    }
    else
    {
        ui->tcMasterKeyEdit->setText("0x00000000000000000000000000000000");
    }

    if (!net.networkKey().isEmpty())
    {
        ui->networkKeyEdit->setText(QString("0x") + net.networkKey().toHex());
    }
    else
    {
        ui->networkKeyEdit->setText("0x00000000000000000000000000000000");
    }

    ui->networkKeySequenceNumberEdit->setValue((int)net.networkKeySequenceNumber());

    if (!net.zllKey().isEmpty())
    {
        ui->zllKeyEdit->setText(QString("0x") + net.zllKey().toHex());
    }
    else
    {
        ui->zllKeyEdit->setText("0x00000000000000000000000000000000");
    }

    ui->tcAddressEdit->setText(strExtAddress(buf, net.trustCenterAddress().ext()));


    if (net.securityLevel() <= 0x07)
    {
        ui->securityLevelComboBox->setCurrentIndex(net.securityLevel());
    }
    else
    {
        ui->securityLevelComboBox->setCurrentIndex(0x00); // no security
    }

    switch (net.securityMode())
    {
    case ZM_NO_SECURITY:
    case ZM_STD_PRECONFIGURED_NETWORK_KEY:
    case ZM_STD_NETWORK_KEY_FROM_TC:
    case ZM_HIGH_NO_MASTER_BUT_TC_LINK_KEY:
    case ZM_HIGH_WITH_MASTER_KEY: // fallthrough
        ui->securityModeComboBox->setCurrentIndex(net.securityMode());
        break;

    default:
        DBG_Printf(DBG_ERROR, "%s got unknown security mode %u\n", Q_FUNC_INFO, net.securityMode());
        // display no security
        ui->securityModeComboBox->setCurrentIndex(0);
        break;
    }

    int ch = 11;
    for (int i = 0; i < m_channels.size(); i++, ch++)
    {

        QCheckBox *checkBox = m_channels[i];

        if (net.channel() == (i + 11))
        {
            checkBox->setStyleSheet("background-color: #ededed; border: 1px solid #dddddd; padding: 2px; border-radius: 5px;");
        }
        else
        {
            checkBox->setStyleSheet("");
        }

        if (net.channelMask() & (1 << ch))
        {
            checkBox->setChecked(true);
        }
        else
        {
            checkBox->setChecked(false);
        }
    }

    ui->predefinedPanIdCheckBox->setChecked(net.predefinedPanId());
    ui->panIdEdit->setEnabled(net.predefinedPanId());

    ui->staticNwkAddrCheckBox->setChecked(net.staticAddress());
    // TODO: custom mac address

    if (net.deviceType() == deCONZ::Coordinator)
    {
        ui->deviceTypeComboBox->setCurrentIndex(0);
    }
    else
    {
         ui->deviceTypeComboBox->setCurrentIndex(1);
    }

    ui->nwkUpdateIdspinBox->setValue(net.nwkUpdateId());
}

void zmNetEdit::onRefresh()
{
    if (deCONZ::controller()->getNetworkConfig() == 0)
    {
        m_configTimer->start();
        ui->configStatus->setText(tr("busy ..."));
        m_state = BusyState;
        setButtons();
    }
}

void zmNetEdit::onAccept()
{
    if (!m_model)
    {
        return;
    }

    zmNet &net = m_model->currentNetwork();
    uint8_t items[32];
    uint8_t n = 1; // dummy for length at [0]

    // predefined pan id
    if (ui->predefinedPanIdCheckBox->isChecked())
    {
        net.setPredefinedPanId(true);
        net.pan().setNwk(ui->panIdEdit->text().toUShort(0, 16));

        items[n++] = ZM_DID_NWK_PANID;
    }
    else
    {
        net.setPredefinedPanId(false);
    }
    items[n++] = ZM_DID_STK_PREDEFINED_PANID;

    net.pan().setExt(ui->extPanIdEdit->text().toULongLong(0, 16));
    net.panAps().setExt(ui->apsUseExtPanIdEdit->text().toULongLong(0, 16));

    net.ownAddress().setExt(ui->extEdit->text().toULongLong(0, 16));

    // trust center address
    if (ui->tcAddressEdit->text().isEmpty())
    {
        if (ui->securityModeComboBox->currentIndex() != ZM_NO_SECURITY)
        {
            // TODO: notify user whats wrong
            qDebug() << Q_FUNC_INFO << "trust center address required";
            return;
        }
    }

    net.trustCenterAddress().setExt(ui->tcAddressEdit->text().toULongLong(0, 16));

    // specify own nwk address
    if (ui->staticNwkAddrCheckBox->isChecked())
    {
        net.ownAddress().setNwk(ui->nwkEdit->text().toUShort(0, 16));
        net.setStaticAddress(true);
        items[n++] = ZM_DID_NWK_NETWORK_ADDRESS;
    }
    else
    {
        net.setStaticAddress(false);
    }
    items[n++] = ZM_DID_STK_STATIC_NETWORK_ADDRESS;

    // network key
    if (ui->networkKeyEdit->text().size() == 32 + 2)
    {
        QByteArray key = QByteArray::fromHex(qPrintable(ui->networkKeyEdit->text()) + 2);

        if (key.size() != 16)
        {
            qDebug() << "invalid network key" << key.toHex();
        }
        else
        {
            net.setNetworkKey(key);
        }
    }
    else
    {
        qDebug() << Q_FUNC_INFO << "invalid network key length";
    }

    // ZLL key
    if (ui->zllKeyEdit->text().size() == 32 + 2)
    {
        QByteArray key = QByteArray::fromHex(qPrintable(ui->zllKeyEdit->text()) + 2);

        if (key.size() != 16)
        {
            qDebug() << "invalid zll key" << key.toHex();
        }
        else
        {
            net.setZllKey(key);
        }
    }
    else
    {
        qDebug() << Q_FUNC_INFO << "invalid zll key length";
    }

    // zll active
    if (ui->zllActiveCheckBox->isChecked())
    {
        net.setConnectMode(deCONZ::ConnectModeZll);
    }
    else
    {
        net.setConnectMode(deCONZ::ConnectModeNormal);
    }

    // zll factory new
    net.setZllFactoryNew(ui->zllFactoryNewCheckBox->isChecked());

    // tc link key
    if (ui->tcLinkKeyEdit->text().size() == 32 + 2)
    {
        QByteArray key = QByteArray::fromHex(qPrintable(ui->tcLinkKeyEdit->text()) + 2);

        if (key.size() != 16)
        {
            qDebug() << "invalid tc link key" << key.toHex();
        }
        else
        {
            net.setTrustCenterLinkKey(key);
        }
    }
    else
    {
        qDebug() << Q_FUNC_INFO << "invalid tc link key length";
    }

    // tc master key
    if (ui->tcMasterKeyEdit->text().size() == 32 + 2)
    {
        QByteArray key = QByteArray::fromHex(qPrintable(ui->tcMasterKeyEdit->text()) + 2);

        if (key.size() != 16)
        {
            qDebug() << "invalid tc master key" << key.toHex();
        }
        else
        {
            net.setTrustCenterMasterKey(key);
        }
    }
    else
    {
        qDebug() << Q_FUNC_INFO << "invalid tc master key length";
    }

    // channel
    uint32_t channelMask = 0;
    int ch = 11;
    for (int i = 0; i < m_channels.size(); i++, ch++)
    {
        if (m_channels[i]->isChecked())
        {
            channelMask |= (1 << ch);
        }
    }

    net.setChannelMask(channelMask);

    // device type
    if (ui->deviceTypeComboBox->currentIndex() == 0)
    {
        net.setDeviceType(deCONZ::Coordinator);
    }
    else
    {
        net.setDeviceType(deCONZ::Router);
    }

    // security level
    int idx = ui->securityLevelComboBox->currentIndex();
    idx = ((idx >= 0x00) && (idx <= 0x07)) ? idx : 0x00;
    net.setSecurityLevel((quint8)idx);

    // security mode
    idx = ui->securityModeComboBox->currentIndex();
    switch (idx)
    {
    case ZM_NO_SECURITY:
    case ZM_STD_PRECONFIGURED_NETWORK_KEY:
    case ZM_STD_NETWORK_KEY_FROM_TC:
    case ZM_HIGH_NO_MASTER_BUT_TC_LINK_KEY:
    case ZM_HIGH_WITH_MASTER_KEY:
        net.setSecurityMode(idx);
        break;

    default:
        net.setSecurityMode(ZM_HIGH_NO_MASTER_BUT_TC_LINK_KEY);
        break;
    }

    uint nwkUpdateId = (uint)ui->nwkUpdateIdspinBox->value();
    DBG_Assert(nwkUpdateId < 256);
    net.setNwkUpdateId(nwkUpdateId);

    items[n++] = ZM_DID_STK_NWK_UPDATE_ID;
    items[n++] = ZM_DID_APS_USE_EXTENDED_PANID;
    items[n++] = ZM_DID_APS_DESIGNED_COORDINATOR;
    items[n++] = ZM_DID_APS_CHANNEL_MASK;
    if (ui->customMacAddrCheckBox->isChecked())
    {
        items[n++] = ZM_DID_MAC_ADDRESS;
    }
    items[n++] = ZM_DID_STK_CONNECT_MODE;
    items[n++] = ZM_DID_STK_SECURITY_MODE;
//    items[n++] = ZM_DID_STK_ENDPOINT;
    items[n++] = ZM_DID_APS_TRUST_CENTER_ADDRESS;
    items[n++] = ZM_DID_STK_NETWORK_KEY;
    items[n++] = ZM_DID_STK_LINK_KEY;
    items[n++] = ZM_DID_ZLL_KEY;
    items[n++] = ZM_DID_ZLL_FACTORY_NEW;

    Q_ASSERT(n < sizeof(items));
    items[0] = n - 1;
    m_configTimer->start(); // TODO: remove timeout based handling trust master instead

    // handle network tab
    if (ui->tabWidget->currentWidget() == ui->tabNetwork)
    {
        deCONZ::controller()->setNetworkConfig(net, items);
        emit deCONZ::controller()->configurationChanged();
    }
    // handle endpoints tab
    else if (ui->tabWidget->currentWidget() == ui->tabEndpoints)
    {
        for (size_t i = 0; i < m_endpoints.size(); i++)
        {
            Endpoint *ep = m_endpoints[i];
            getEndpointData(ep);
            deCONZ::controller()->setEndpointConfig(ep->index, ep->descriptor);
        }
    }
    // handle ZLL tab
    else if (ui->tabWidget->currentWidget() == ui->tabZll)
    {
        deCONZ::controller()->setNetworkConfig(net, items);
    }

    ui->configStatus->setText(tr("busy ..."));
    m_state = BusyState;
    setButtons();
}

void zmNetEdit::setSimpleDescriptor(quint8 index, const deCONZ::SimpleDescriptor &descriptor)
{
    Endpoint *ep = getEndpointWidget(index);
    ep->descriptor = descriptor;
    setEndpointData(ep);
}

zmNetEdit::Endpoint * zmNetEdit::getEndpointWidget(int index)
{
    for (size_t i = 0; i < m_endpoints.size(); i++)
    {
        if (m_endpoints[i]->index == index)
        {
            return m_endpoints[i];
        }
    }

    Endpoint *ep = new Endpoint;
    ep->groupBox = new QGroupBox;
    ep->groupBox->setTitle(tr("[ %1 ]").arg(index + 1));
    ep->index = index;
    ep->endpoint = new QLineEdit;
    ep->profileId = new QLineEdit;
    ep->deviceId = new QLineEdit;
    ep->deviceVersion = new QLineEdit;
    ep->inClusters = new QLineEdit;
    ep->outClusters = new QLineEdit;

    m_endpointLayout->invalidate();
    ui->endpointGroupBox->setSizePolicy(QSizePolicy::Expanding,
                                        QSizePolicy::Expanding);
    m_endpointLayout->setSizeConstraint(QLayout::SetMinimumSize);

    QFormLayout *form = new QFormLayout;
    form->addRow(tr("&Endpoint"), ep->endpoint);
    form->addRow(tr("&Profile ID"), ep->profileId);
    form->addRow(tr("&Device ID"), ep->deviceId);
    form->addRow(tr("&Device version"), ep->deviceVersion);
    form->addRow(tr("&In clusters"), ep->inClusters);
    form->addRow(tr("&Out clusters"), ep->outClusters);
    form->setFieldGrowthPolicy(QFormLayout::FieldGrowthPolicy::ExpandingFieldsGrow);
    ep->groupBox->setLayout(form);

    m_endpointLayout->addWidget(ep->groupBox);

    m_endpoints.push_back(ep);

    return ep;
}

void zmNetEdit::setEndpointData(zmNetEdit::Endpoint *ep)
{
    QString text;
    text = "0x" + QString("%1").arg(ep->descriptor.endpoint(), 2, 16, QLatin1Char('0')).toUpper();
    ep->endpoint->setText(text);
    text = "0x" + QString("%1").arg(ep->descriptor.profileId(), 4, 16, QLatin1Char('0')).toUpper();
    ep->profileId->setText(text);
    text = "0x" + QString("%1").arg(ep->descriptor.deviceId(), 4, 16, QLatin1Char('0')).toUpper();
    ep->deviceId->setText(text);
    text = "0x" + QString("%1").arg(ep->descriptor.deviceVersion(), 2, 16, QLatin1Char('0')).toUpper();
    ep->deviceVersion->setText(text);

    QStringList clusters;
    text.clear();

    // in clusters
    for (const auto &cl : ep->descriptor.inClusters())
    {
        text = "0x" + QString("%1").arg(cl.id(), 4, 16, QLatin1Char('0')).toUpper();
        clusters.append(text);
    }

    if (!clusters.isEmpty())
    {
         text = clusters.join(",");
    }

    ep->inClusters->setText(text);

    // out clusters
    clusters.clear();
    text.clear();
    for (const auto &cl : ep->descriptor.outClusters())
    {
        text = "0x" + QString("%1").arg(cl.id(), 4, 16, QLatin1Char('0')).toUpper();
        clusters.append(text);
    }

    if (!clusters.isEmpty())
    {
         text = clusters.join(",");
    }

    ep->outClusters->setText(text);
}

void zmNetEdit::getEndpointData(zmNetEdit::Endpoint *ep)
{
    bool ok;
    quint8 endpoint;
    quint16 profileId;
    quint16 deviceId;
    quint8 deviceVersion;

    endpoint = ep->endpoint->text().toUShort(&ok, 16);
    if (!ok || (endpoint == 0x00) || (endpoint == 0xFF))
        return;

    profileId = ep->profileId->text().toUShort(&ok, 16);
    if (!ok)
        return;

    deviceId = ep->deviceId->text().toUShort(&ok, 16);
    if (!ok)
        return;

    deviceVersion = (quint8)ep->deviceVersion->text().toUShort(&ok, 16);
    if (!ok)
    {
        deviceVersion = 0x00;
    }

    ep->descriptor.setEndpoint(endpoint);
    ep->descriptor.setProfileId(profileId);
    ep->descriptor.setDeviceId(deviceId);
    ep->descriptor.setDeviceVersion(deviceVersion);

    QStringList clusters;

    // TODO: only replace if something changed
    ep->descriptor.inClusters().clear();
    clusters = ep->inClusters->text().split(",");
    foreach (QString clusterId, clusters)
    {
        quint16 id = clusterId.toUShort(&ok, 16);
        if (ok)
        {
            deCONZ::ZclCluster cl;
            cl.setId(id);
            ep->descriptor.inClusters().push_back(cl);
        }
        else
        {
            DBG_Printf(DBG_INFO, "EP edit could not read in clusterId: %s\n", qPrintable(clusterId));
        }
    }

    ep->descriptor.outClusters().clear();
    clusters = ep->outClusters->text().split(",");
    foreach (QString clusterId, clusters)
    {
        quint16 id = clusterId.toUShort(&ok, 16);
        if (ok)
        {
            deCONZ::ZclCluster cl;
            cl.setId(id);
            ep->descriptor.outClusters().push_back(cl);
        }
        else
        {
            DBG_Printf(DBG_INFO, "EP edit could not read out clusterId: %s\n", qPrintable(clusterId));
        }
    }
}

void zmNetEdit::configTimeout()
{
    m_state = IdleState;
    ui->configStatus->clear();
    m_configCount = 0;
    setButtons();
    onUpdatedCurrentNetwork();
}

void zmNetEdit::setDeviceState(deCONZ::State state)
{
    Q_UNUSED(state);
    setButtons();
}

void zmNetEdit::setButtons()
{
    bool enabled = false;

    if (deCONZ::master()->connected())
    {
        if (m_state == IdleState)
        {
            enabled = true;
        }
        else if (m_state == BusyState)
        {
            enabled = false;
        }
    }
    else
    {
        enabled = false;
    }

    ui->acceptButton->setEnabled(enabled);
    ui->refreshButton->setEnabled(enabled);
}

void zmNetEdit::checkFeatures()
{
    {
        bool hiSec = false;
        bool linkSec = true;

        QStandardItemModel *model3 =
        qobject_cast<QStandardItemModel*>(ui->securityModeComboBox->model());

        if (model3)
        {
            QModelIndex index;
            QStandardItem *item;

            index = model3->index(3, ui->securityModeComboBox->modelColumn(), ui->securityModeComboBox->rootModelIndex());
            item = model3->itemFromIndex(index);
            if (item)
            {
                item->setEnabled(linkSec);
            }

            index = model3->index(4, ui->securityModeComboBox->modelColumn(), ui->securityModeComboBox->rootModelIndex());
            item = model3->itemFromIndex(index);
            if (item)
            {
                item->setEnabled(hiSec);
            }
        }
    }
}

void zmNetEdit::predefinedPanIdToggled(bool checked)
{
    ui->predefinedPanIdCheckBox->setChecked(checked);
    ui->panIdEdit->setEnabled(checked);
}

void zmNetEdit::staticNwkAddressToggled(bool checked)
{
    ui->staticNwkAddrCheckBox->setChecked(checked);
    ui->nwkEdit->setEnabled(checked);
}

void zmNetEdit::customMacAddressToggled(bool checked)
{
    ui->customMacAddrCheckBox->setChecked(checked);
    ui->extEdit->setEnabled(checked);
}

void zmNetEdit::onNetStartDone(uint8_t zdoStatus)
{
    QString msg(tr("Starting network failed: "));

    switch (zdoStatus)
    {
    case deCONZ::NwkInvalidParameterStatus:
        msg.append(tr("NWK_INVALID_PARAMETER_STATUS"));
        break;

    case deCONZ::NwkNotPermittedStatus:
        msg.append(tr("NWK_NOT_PERMITTED_STATUS"));
        break;

    case deCONZ::MacNoBeaconStatus:
        msg.append(tr("NWK_MAC_NO_BEACON_STATUS"));
        break;

    case deCONZ::NwkNoNetworkStatus:
        msg.append(tr("NWK_NO_NETWORK_STATUS"));
        break;

    default:
        msg.append(QString(" Status: %1").arg(zdoStatus, 2, 16,QChar('0')));
        break;
    }

    DBG_Printf(DBG_INFO, "NET %s\n", qPrintable(msg));
//    ui->zdoStartNetResultLabel->setText(msg); // TODO: show message to user
}

// delayed init
void zmNetEdit::init()
{
    connect(deCONZ::netModel(), &zmNetDescriptorModel::updatedCurrentNetwork,
            this, &zmNetEdit::onReadParameterResponse);
    setButtons();
}

bool zmNetEdit::apsAcksEnabled()
{
    return ui->apsAcksCheckBox->isChecked();
}

bool zmNetEdit::staticNwkAddress()
{
    return ui->staticNwkAddrCheckBox->isChecked();
}

void zmNetEdit::setApsAcksEnabled(bool enabled)
{
    ui->apsAcksCheckBox->setChecked(enabled);
}

void zmNetEdit::setHAConfig(QVariantMap epData)
{
    Endpoint ep;
    bool ok;
    if (epData.contains("endpoint"))
    {
        ep.descriptor.setEndpoint(epData["endpoint"].toString().toUInt(&ok,16));
    }
    if (epData.contains("profileId"))
    {
        ep.descriptor.setProfileId(epData["profileId"].toString().toUInt(&ok,16));
    }
    if (epData.contains("deviceId"))
    {
        ep.descriptor.setDeviceId(epData["deviceId"].toString().toUInt(&ok,16));
    }
    if (epData.contains("deviceVersion"))
    {
        ep.descriptor.setDeviceVersion(epData["deviceVersion"].toString().toUInt(&ok,16));
    }

    if (epData.contains("inClusters"))
    {
        QVariantList inClusters = epData["inClusters"].toList();

        for (int i = 0; i < inClusters.length(); i++)
        {
            uint id = inClusters.at(i).toString().toUInt(&ok,16);
            deCONZ::ZclCluster in;
            in.setId(id);
            ep.descriptor.inClusters().push_back(in);
        }
    }
    if (epData.contains("outClusters"))
    {
        QVariantList outClusters = epData["outClusters"].toList();

        for (int i = 0; i < outClusters.length(); i++)
        {
            int id = outClusters.at(i).toString().toUInt(&ok,16);
            deCONZ::ZclCluster out;
            out.setId(id);
            ep.descriptor.outClusters().push_back(out);
        }

    }
    if (epData.contains("index"))
    {
        deCONZ::controller()->setEndpointConfig(epData["index"].toUInt(), ep.descriptor);
    }
}

QVariantMap zmNetEdit::getHAConfig(int index)
{
    QVariantMap epData;

    if (index >= 0 && unsigned(index) < m_endpoints.size())
    {
        Endpoint *ep = m_endpoints[index];
        getEndpointData(ep);

        QVariantList inClusters;
        QVariantList outClusters;
        epData["index"] =  index;
        epData["endpoint"] = QString("0x%1").arg(QString::number(ep->descriptor.endpoint(),16));
        epData["profileId"] = QString("0x%1").arg(QString::number(ep->descriptor.profileId(),16));
        epData["deviceId"] = QString("0x%1").arg(QString::number(ep->descriptor.deviceId(),16));
        epData["deviceVersion"] = QString("0x%1").arg(QString::number(ep->descriptor.deviceVersion(),16));

        for (const auto &cl : ep->descriptor.inClusters())
        {
            const QString id = QString("0x%1").arg(QString::number(cl.id(),16));
            inClusters.append(id);
        }

        epData["inClusters"] = inClusters;

        for (const auto &cl : ep->descriptor.outClusters())
        {
            const QString id = QString("0x%1").arg(QString::number(cl.id(),16));
            outClusters.append(id);
        }

        epData["outClusters"] = outClusters;
    }
    return epData;
}
