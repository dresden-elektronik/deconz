/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QStandardItemModel>

#include "deconz/atom_table.h"
#include "deconz/dbg_trace.h"
#include "deconz/u_sstream_ex.h"
#include "actor_vfs_model.h"
#include "zm_node_info.h"
#include "ui_zm_node_info.h"
#include "zm_node.h"
#include "zm_node_model.h"

static AT_AtomIndex ati_core_aps;
static AT_AtomIndex ati_devices;

namespace {
    const char *InfoKeys[] = {
        "   Common Info",
        "   Name",
        "   Manufacturer",
        "   Model Identifier",
        "   Type",
        "   MAC Address",
        "   NWK Address",
        "   Node Descriptor",
        "   Frequency Band",
        "   User Descriptor",
        "   Complex Descriptor",
        "   Manufacturer Code",
        "   Max Buffer Size",
        "   Max Incoming Transfer Size",
        "   Max Outgoing Transfer Size",
        "   MAC Capabilities",
        "   Alternate PAN Coordinator",
        "   Device Type",
        "   Power Source",
        "   Receiver On When Idle",
        "   Security Support",
        "   Server Mask",
        "   Primary Trust Center",
        "   Backup Trust Center",
        "   Primary Binding Table Cache",
        "   Backup Binding Table Cache",
        "   Primary Discovery Cache",
        "   Backup Discovery Cache",
        "   Network Manager",
        "   Descriptor Capabilities",
        "   Extended Active Endpoint List",
        "   Extended Simple Descriptor List",
        "   Power Descriptor",
        "   Power Mode",
        "   Power Source",
        "   Power Level",
         0
    };

    enum InfoIndex
    {
        IdxCommon = 0,
            IdxName,
            IdxManufacturer,
            IdxModelId,
            IdxType,
            IdxExt,
            IdxNwk,

        IdxNodeDescr, // H1
            IdxFreqBand,
            IdxUserDescrAvail,
            IdxComplexrDescrAvail,
            IdxManufacturerCode,
            IdxMaxBufferSize,
            IdxMaxInTransferSize,
            IdxMaxOutTransferSize,
            IdxMacCapabilities, // H2
                IdxAltPanCoord,
                IdxDeviceType,
                IdxMainsPowered,
                IdxRecvOnWhenIdle,
                IdxSecurityCapability,
            IdxServerMask, // H2
                IdxPriTrustCenter,
                IdxBakTrustCenter,
                IdxPriBindCache,
                IdxBakBindCache,
                IdxPriDiscovCache,
                IdxBakDiscovCache,
                IdxNetMngr,
            IdxDescrCapabilities,
                IdxExtEndpointList,
                IdxExtSimpleDescrList,

        IdxPowerDescr, // H1
            IdxPowerMode,
            IdxPowerSource,
            IdxPowerLevel,

        IdxMax
    };
}

zmNodeInfo::zmNodeInfo(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::zmNodeInfo)
{
    ui->setupUi(this);

    AT_AddAtom("core_aps", qstrlen("core_aps"), &ati_core_aps);
    AT_AddAtom("devices", qstrlen("devices"), &ati_devices);

    // create new model
    QStandardItemModel *model = new QStandardItemModel(this);

    for (int row = 0; row < IdxMax; ++row)
    {
        m_info[row].key = new QStandardItem(InfoKeys[row]);
        model->setItem(row, 0, m_info[row].key);

        m_info[row].value = new QStandardItem;
        model->setItem(row, 1, m_info[row].value);
    }

    static_assert (IdxMax == 36, "m_info array size doesn't match enum IdMax");

    ui->tableView->setModel(model);

    // {
    //     QFont fn = font();
    //     fn.setWeight(QFont::Medium);
    //     std::array<int, 3> headerIdx = { IdxCommon, IdxNodeDescr, IdxPowerDescr };

    //     // H1
    //     // header rows have dark background and bright text
    //     for (int row : headerIdx)
    //     {
    //         m_info[row].key->setFont(fn);
    //     }
    // }

    updateHeader1Style();

    setNode(nullptr);
    ui->tableView->resizeColumnToContents(0);
    //ui->tableView->setFont(Theme_FontMonospace());
}

zmNodeInfo::~zmNodeInfo()
{
    delete ui;
}

QModelIndex VFS_GetActorIndex(ActorVfsModel *vfs, unsigned actorId)
{
    for (int row = 0; row < vfs->rowCount(); row++)
    {
        QModelIndex ia = vfs->index(row, ActorVfsModel::ColumnValue);
        if (ia.isValid() && ia.data().toUInt() == actorId)
        {
            return ia;
        }
    }

    return QModelIndex();
}

void zmNodeInfo::setNode(ActorVfsModel *vfs, uint64_t mac)
{
    Q_ASSERT(vfs);

    if (mac == 0)
    {
        return;
    }

    AT_AtomIndex ati_mac;

    {
        char buf[28];
        U_SStream ss;
        U_sstream_init(&ss, buf, sizeof(buf));
        U_sstream_put_mac_address(&ss, mac);

        if (AT_GetAtomIndex(ss.str, ss.pos, &ati_mac) == 0)
        {
            return;
        }
    }

    QModelIndex index = vfs->indexWithName(ati_core_aps.index, QModelIndex());

    if (!index.isValid())
    {
        return;
    }

    index = vfs->indexWithName(ati_devices.index, index);
    if (!index.isValid())
    {
        return;
    }

    index = vfs->indexWithName(ati_mac.index, index);

    if (!index.isValid())
    {
        return;
    }

    QString name = index.data().toString();
    DBG_Printf(DBG_INFO, "AM selected %s\n", qPrintable(name));
}


QString toHexString(uint16_t number)
{
    return QString("0x%1").arg(number, 4, 16, QChar('0'));
}

QString toHexString(uint64_t number)
{
    return QString("0x%1").arg(number, 16, 16, QChar('0'));
}

void zmNodeInfo::setNode(deCONZ::zmNode *data)
{
    if (m_data != data)
    {
        clear();
        m_data = data;
        m_state = Idle;
        stateCheck();
    }

    if (!data)
    {
        ui->tableView->hide();
        return;
    }

    if (!ui->tableView->isVisible())
    {
        ui->tableView->show();
    }

    uint64_t extAddr = data->address().ext();
    deCONZ::NodeModel *nModel = deCONZ::nodeModel();

    const deCONZ::NodeDescriptor &nd = m_data->nodeDescriptor();

    const QLatin1String unknownValue("unknown");

    QString nodeName = nModel->data(extAddr, deCONZ::NodeModel::NameColumn).toString();

    if (!nodeName.isEmpty())
    {
        ui->deviceName->setText(nodeName);
    }
    else
    {
        ui->deviceName->setText(toHexString(m_data->address().ext()));
    }

    QString manufacturer = nModel->data(extAddr, deCONZ::NodeModel::VendorColumn).toString();
    if (manufacturer.isEmpty())
    {
        manufacturer = unknownValue;
    }

    QString modelId = nModel->data(extAddr, deCONZ::NodeModel::ModelIdColumn).toString();
    if (modelId.isEmpty())
    {
        modelId = unknownValue;
    }

    ui->deviceName->hide();
    ui->deviceNameLabel->hide();

    setValue(IdxName, ui->deviceName->text());
    setValue(IdxManufacturer, manufacturer);
    setValue(IdxModelId, modelId);
    setValue(IdxType, m_data->deviceTypeString());
    setValue(IdxExt, toHexString(m_data->address().ext()));
    setValue(IdxNwk, toHexString(m_data->address().nwk()));

    setValue(IdxFreqBand, QString(m_data->nodeDescriptor().frequencyBandString()));
    setValue(IdxUserDescrAvail, nd.hasUserDescriptor());
    setValue(IdxComplexrDescrAvail, nd.hasComplexDescriptor());
    setValue(IdxManufacturerCode, toHexString(nd.manufacturerCode()));
    setValue(IdxMaxBufferSize, QString("%1").arg((uint)nd.maxBufferSize(), 0, 10, QChar('0')));
    setValue(IdxMaxInTransferSize, QString("%1").arg(nd.maxIncomingTransferSize(), 0, 10, QChar('0')));
    setValue(IdxMaxOutTransferSize, QString("%1").arg(nd.maxOutgoingTransferSize(), 0, 10, QChar('0')));

    const uint16_t serverMask = static_cast<uint>(m_data->nodeDescriptor().serverMask()) & 0xFFFF;

    setValue(IdxServerMask, toHexString(serverMask));

    setValue(IdxPriTrustCenter, serverMask & zme::PrimaryTrustCenter ? true : false);
    setValue(IdxBakTrustCenter, serverMask & zme::BackupTrustCenter ? true : false);
    setValue(IdxPriBindCache,   serverMask & zme::PrimaryBindingTableCache ? true : false);
    setValue(IdxBakBindCache,   serverMask & zme::BackupBindingTableCache ? true : false);
    setValue(IdxPriDiscovCache, serverMask & zme::PrimaryDiscoveryCache ? true : false);
    setValue(IdxBakDiscovCache, serverMask & zme::BackupDiscoveryCache ? true : false);
    setValue(IdxNetMngr,        serverMask & zme::NetworkManager ? true : false);

    setValue(IdxMacCapabilities, QString("0x%1").arg(m_data->macCapabilities(), 2, 16, QChar('0')));
    setValue(IdxDeviceType,        (m_data->macCapabilities() & deCONZ::MacDeviceIsFFD ? "FFD" : "RFD"));
    setValue(IdxAltPanCoord,        m_data->macCapabilities() & deCONZ::MacAlternatePanCoordinator ? true : false);
    setValue(IdxMainsPowered,      (m_data->macCapabilities() & deCONZ::MacIsMainsPowered ? "Mains" : "Battery"));
    setValue(IdxRecvOnWhenIdle,     m_data->macCapabilities() & deCONZ::MacReceiverOnWhenIdle ? true : false);
    setValue(IdxSecurityCapability, m_data->macCapabilities() & deCONZ::MacSecuritySupport ? true : false);

    setValue(IdxExtEndpointList, nd.hasEndpointList());
    setValue(IdxExtSimpleDescrList, nd.hasSimpleDescriptorList());

    const QLatin1String notAvailableValue("n/a");

    if (m_data->powerDescriptor().isValid())
    {
        switch (m_data->powerDescriptor().currentPowerMode())
        {
        case deCONZ::ModeOnWhenIdle: setValue(IdxPowerMode, "On When Idle"); break;
        case deCONZ::ModePeriodic: setValue(IdxPowerMode, "Periodic"); break;
        case deCONZ::ModeStimulated: setValue(IdxPowerMode, "Stimulated"); break;
        default: setValue(IdxPowerMode, unknownValue); break;
        }

        switch (m_data->powerDescriptor().currentPowerSource())
        {
        case deCONZ::PowerSourceMains: setValue(IdxPowerSource, "Mains"); break;
        case deCONZ::PowerSourceDisposable: setValue(IdxPowerSource, "Disposeable"); break;
        case deCONZ::PowerSourceRechargeable: setValue(IdxPowerSource, "Rechargeable"); break;
        default: setValue(IdxPowerSource, unknownValue); break;
        }

        switch (m_data->powerDescriptor().currentPowerLevel())
        {
        case deCONZ::PowerLevel100: setValue(IdxPowerLevel, "100%"); break;
        case deCONZ::PowerLevel66: setValue(IdxPowerLevel, "66%"); break;
        case deCONZ::PowerLevel33: setValue(IdxPowerLevel, "33%"); break;
        case deCONZ::PowerLevelCritical: setValue(IdxPowerLevel, "Critical"); break;
        default: setValue(IdxPowerLevel, unknownValue); break;
        }
    }
    else
    {
        setValue(IdxPowerMode, notAvailableValue);
        setValue(IdxPowerSource, notAvailableValue);
        setValue(IdxPowerLevel, notAvailableValue);
    }

    {
        std::array<int, 3> rowIdx = { IdxPowerMode, IdxPowerSource, IdxPowerLevel };

        auto fgColor = palette().text().color();

        if (!m_data->powerDescriptor().isValid())
        {
            fgColor = palette().color(QPalette::Disabled, QPalette::Text);
        }


        for (int row : rowIdx)
        {
            m_info[row].key->setForeground(fgColor);
            m_info[row].value->setForeground(fgColor);
        }
    }

    ui->tableView->resizeColumnToContents(0);
}

void zmNodeInfo::dataChanged(deCONZ::zmNode *data)
{
    if (data == m_data)
    {
        // refresh view
        setNode(data);
    }
}

bool zmNodeInfo::event(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange)
    {
        updateHeader1Style();
    }
    return QWidget::event(event);
}

void zmNodeInfo::clear()
{
    QStandardItemModel *model = static_cast<QStandardItemModel*>(ui->tableView->model());

    if (model)
    {
        for (auto &row : m_info)
        {
            row.value->setText(QLatin1String(""));
        }

        QString emptyValue;

        setValue(IdxNwk, emptyValue);
        setValue(IdxExt, emptyValue);
        setValue(IdxType, emptyValue);
        setValue(IdxName, emptyValue);
        setValue(IdxManufacturer, emptyValue);
        setValue(IdxModelId, emptyValue);

        setValue(IdxUserDescrAvail, false);
        setValue(IdxComplexrDescrAvail, false);

        setValue(IdxAltPanCoord, false);
        setValue(IdxDeviceType, "RFD");
        setValue(IdxMainsPowered, "Battery");
        setValue(IdxRecvOnWhenIdle, false);
        setValue(IdxSecurityCapability, false);

        setValue(IdxFreqBand, QString());

        setValue(IdxPriTrustCenter, false);
        setValue(IdxBakTrustCenter, false);
        setValue(IdxPriBindCache, false);
        setValue(IdxBakBindCache, false);
        setValue(IdxPriDiscovCache, false);
        setValue(IdxBakDiscovCache, false);
        setValue(IdxNetMngr, false);

        setValue(IdxExtEndpointList, false);
        setValue(IdxExtSimpleDescrList, false);

        setValue(IdxPowerMode, "");
        setValue(IdxPowerSource, "");
        setValue(IdxPowerLevel, "");
    }

    ui->deviceName->clear();
}

void zmNodeInfo::setValue(size_t idx, const QVariant &value)
{
    if (idx < m_info.size())
    {
        if (m_info[idx].value != nullptr)
        {
            m_info[idx].value->setData(value, Qt::DisplayRole);
        }
    }
}

//! Adapt h1 header colors to theme.
void zmNodeInfo::updateHeader1Style()
{
    QFont fn = font();
    fn.setWeight(QFont::Medium);
    std::array<int, 3> headerIdx = { IdxCommon, IdxNodeDescr, IdxPowerDescr };

    int bri1 = palette().highlight().color().lightness();
    int bri2 = palette().shadow().color().lightness();
    int bri = (bri1 + bri2) / 2;
    QBrush bg(QColor(bri, bri, bri));
    for (int row : headerIdx)
    {
        m_info[row].key->setFont(fn);
        m_info[row].key->setBackground(bg);
        m_info[row].key->setForeground(palette().highlightedText());
        m_info[row].value->setBackground(bg);
    }
}

void zmNodeInfo::stateCheck()
{
    switch (m_state)
    {
    case Idle:
        break;

    case Timeout:
        break;

    default:
        break;
    }
}
