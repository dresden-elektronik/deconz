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
#include "deconz/u_sstream_ex.h"
#include "actor_vfs_model.h"
#include "zm_node_info.h"
#include "ui_zm_node_info.h"
#include "zdp_descriptors.h"

static AT_AtomIndex ati_core_aps;
static AT_AtomIndex ati_rest_plugin;
static AT_AtomIndex ati_devices;
static AT_AtomIndex ati_node_desc;
static AT_AtomIndex ati_nwk;
static AT_AtomIndex ati_attr;
static AT_AtomIndex ati_manufacturername;
static AT_AtomIndex ati_modelid;
static AT_AtomIndex ati_name;
static AT_AtomIndex ati_subdevices;

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

QString toHexString(uint16_t number)
{
    return QString("0x%1").arg(number, 4, 16, QChar('0'));
}

QString toHexString(uint64_t number)
{
    return QString("0x%1").arg(number, 16, 16, QChar('0'));
}

zmNodeInfo::zmNodeInfo(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::zmNodeInfo)
{
    ui->setupUi(this);

    AT_AddAtom("core_aps", qstrlen("core_aps"), &ati_core_aps);
    AT_AddAtom("rest_plugin", qstrlen("rest_plugin"), &ati_rest_plugin);
    AT_AddAtom("devices", qstrlen("devices"), &ati_devices);
    AT_AddAtom("node_desc", qstrlen("node_desc"), &ati_node_desc);
    AT_AddAtom("nwk", qstrlen("nwk"), &ati_nwk);
    AT_AddAtom("attr", qstrlen("attr"), &ati_attr);
    AT_AddAtom("manufacturername", qstrlen("manufacturername"), &ati_manufacturername);
    AT_AddAtom("modelid", qstrlen("modelid"), &ati_modelid);
    AT_AddAtom("name", qstrlen("name"), &ati_name);
    AT_AddAtom("subdevices", qstrlen("subdevices"), &ati_subdevices);

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

    ui->tableView->hide();
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

static AT_AtomIndex atomIndexForMac(uint64_t mac)
{
    AT_AtomIndex ati_mac;
    char buf[28];
    U_SStream ss;

    U_sstream_init(&ss, buf, sizeof(buf));
    U_sstream_put_mac_address(&ss, mac);

    if (AT_GetAtomIndex(ss.str, ss.pos, &ati_mac))
    {
        return ati_mac;
    }

    ati_mac.index = 0;
    return ati_mac;
}

void zmNodeInfo::setNode(ActorVfsModel *vfs, uint64_t mac)
{
    if (!vfs)
        return;

    if (mac == 0)
    {
        ui->tableView->hide();
        return;
    }

    if (m_mac != mac)
    {
        clear();
        m_mac = mac;
    }

    uint16_t nwkAddress = 0xfffe;
    QString nodeName;
    QString manufacturerName;
    QString modelId;
    deCONZ::NodeDescriptor nd;

    AT_AtomIndex ati_mac = atomIndexForMac(mac);
    if (ati_mac.index == 0)
        return;

    // get data from core
    QModelIndex index = vfs->indexWithName(ati_core_aps.index, QModelIndex());

    if (!index.isValid())
        return;

    index = vfs->indexWithName(ati_devices.index, index);
    if (!index.isValid())
        return;

    index = vfs->indexWithName(ati_mac.index, index);

    if (!index.isValid())
        return;

    {
        QModelIndex nwkIndex = vfs->indexWithName(ati_nwk.index, index);
        if (nwkIndex.isValid())
        {
            nwkAddress = (uint16_t)nwkIndex.data(ActorVfsModel::RawValueRole).toUInt();
        }
    }

    {
        QModelIndex ndIndex = vfs->indexWithName(ati_node_desc.index, index);
        if (ndIndex.isValid())
        {
            QString ndHex = ndIndex.sibling(ndIndex.row(), ActorVfsModel::ColumnValue).data().toString();
            if (ndHex.startsWith("0x"))
            {
                QByteArray ndData = QByteArray::fromHex(ndHex.mid(2).toLocal8Bit());
                QDataStream stream(ndData);
                stream.setByteOrder(QDataStream::LittleEndian);
                nd.readFromStream(stream);
            }
        }
    }

    // get data from REST plugin
    index = vfs->indexWithName(ati_rest_plugin.index, QModelIndex());
    if (!index.isValid())
        return;

    index = vfs->indexWithName(ati_devices.index, index);
    if (!index.isValid())
        return;

    index = vfs->indexWithName(ati_mac.index, index);
    if (!index.isValid())
        return;

    QModelIndex subDevicesIndex = vfs->indexWithName(ati_subdevices.index, index);
    if (subDevicesIndex.isValid() && 0 < vfs->rowCount(subDevicesIndex))
    {
        // get main subdevice (first)
        QModelIndex subDeviceIndex = vfs->index(0, 0, subDevicesIndex);

        if (subDeviceIndex.isValid())
        {
            QModelIndex attrIndex = vfs->indexWithName(ati_attr.index, subDeviceIndex);
            if (attrIndex.isValid())
            {
                {
                    QModelIndex nameIndex = vfs->indexWithName(ati_name.index, attrIndex);
                    if (nameIndex.isValid())
                    {
                        QString name = nameIndex.sibling(nameIndex.row(), ActorVfsModel::ColumnValue).data().toString();
                        if (!name.isEmpty())
                            nodeName = name;
                    }
                }

                {
                    QModelIndex manufacturerIndex = vfs->indexWithName(ati_manufacturername.index, attrIndex);
                    if (manufacturerIndex.isValid())
                    {
                        QString mf = manufacturerIndex.sibling(manufacturerIndex.row(), ActorVfsModel::ColumnValue).data().toString();
                        if (!mf.isEmpty())
                            manufacturerName = mf;
                    }
                }

                {
                    QModelIndex modelidIndex = vfs->indexWithName(ati_modelid.index, attrIndex);
                    if (modelidIndex.isValid())
                    {
                        QString mid = modelidIndex.sibling(modelidIndex.row(), ActorVfsModel::ColumnValue).data().toString();
                        if (!mid.isEmpty())
                            modelId = mid;
                    }
                }
            }
        }
    }

    if (!ui->tableView->isVisible())
    {
        ui->tableView->show();
    }

    if (nodeName.isEmpty())
        nodeName = toHexString(m_mac);

    ui->deviceName->setText(nodeName);

    const QLatin1String unknownValue("unknown");

    if (manufacturerName.isEmpty())
        manufacturerName = unknownValue;

    if (modelId.isEmpty())
        modelId = unknownValue;

    ui->deviceName->hide();
    ui->deviceNameLabel->hide();

    const auto macCapabilities = nd.macCapabilities();
    const uint16_t serverMask = static_cast<uint>(nd.serverMask()) & 0xFFFF;
    QString deviceType;

    if (macCapabilities & deCONZ::MacDeviceIsFFD)
    {
        if (nwkAddress == 0x0000)
        {
            deviceType = "Coordinator";
        }
        else
        {
            deviceType = "Router";
        }
    }
    else
    {
        deviceType = "End device";
    }

    setValue(IdxName, ui->deviceName->text());
    setValue(IdxManufacturer, manufacturerName);
    setValue(IdxModelId, modelId);
    setValue(IdxType, deviceType);
    setValue(IdxExt, toHexString(m_mac));
    setValue(IdxNwk, toHexString(nwkAddress));

    setValue(IdxFreqBand, QString(nd.frequencyBandString()));
    setValue(IdxUserDescrAvail, nd.hasUserDescriptor());
    setValue(IdxComplexrDescrAvail, nd.hasComplexDescriptor());
    setValue(IdxManufacturerCode, toHexString(nd.manufacturerCode()));
    setValue(IdxMaxBufferSize, QString("%1").arg((uint)nd.maxBufferSize(), 0, 10, QChar('0')));
    setValue(IdxMaxInTransferSize, QString("%1").arg(nd.maxIncomingTransferSize(), 0, 10, QChar('0')));
    setValue(IdxMaxOutTransferSize, QString("%1").arg(nd.maxOutgoingTransferSize(), 0, 10, QChar('0')));


    setValue(IdxServerMask, toHexString(serverMask));

    setValue(IdxPriTrustCenter, serverMask & zme::PrimaryTrustCenter ? true : false);
    setValue(IdxBakTrustCenter, serverMask & zme::BackupTrustCenter ? true : false);
    setValue(IdxPriBindCache,   serverMask & zme::PrimaryBindingTableCache ? true : false);
    setValue(IdxBakBindCache,   serverMask & zme::BackupBindingTableCache ? true : false);
    setValue(IdxPriDiscovCache, serverMask & zme::PrimaryDiscoveryCache ? true : false);
    setValue(IdxBakDiscovCache, serverMask & zme::BackupDiscoveryCache ? true : false);
    setValue(IdxNetMngr,        serverMask & zme::NetworkManager ? true : false);


    setValue(IdxMacCapabilities, QString("0x%1").arg((uint32_t)macCapabilities, 2, 16, QChar('0')));
    setValue(IdxDeviceType,        (macCapabilities & deCONZ::MacDeviceIsFFD ? "FFD" : "RFD"));
    setValue(IdxAltPanCoord,        macCapabilities & deCONZ::MacAlternatePanCoordinator ? true : false);
    setValue(IdxMainsPowered,      (macCapabilities & deCONZ::MacIsMainsPowered ? "Mains" : "Battery"));
    setValue(IdxRecvOnWhenIdle,     macCapabilities & deCONZ::MacReceiverOnWhenIdle ? true : false);
    setValue(IdxSecurityCapability, macCapabilities & deCONZ::MacSecuritySupport ? true : false);

    setValue(IdxExtEndpointList, nd.hasEndpointList());
    setValue(IdxExtSimpleDescrList, nd.hasSimpleDescriptorList());

    const QLatin1String notAvailableValue("n/a");

    deCONZ::PowerDescriptor powerDescriptor;

    if (powerDescriptor.isValid())
    {
        switch (powerDescriptor.currentPowerMode())
        {
        case deCONZ::ModeOnWhenIdle: setValue(IdxPowerMode, "On When Idle"); break;
        case deCONZ::ModePeriodic: setValue(IdxPowerMode, "Periodic"); break;
        case deCONZ::ModeStimulated: setValue(IdxPowerMode, "Stimulated"); break;
        default: setValue(IdxPowerMode, unknownValue); break;
        }

        switch (powerDescriptor.currentPowerSource())
        {
        case deCONZ::PowerSourceMains: setValue(IdxPowerSource, "Mains"); break;
        case deCONZ::PowerSourceDisposable: setValue(IdxPowerSource, "Disposeable"); break;
        case deCONZ::PowerSourceRechargeable: setValue(IdxPowerSource, "Rechargeable"); break;
        default: setValue(IdxPowerSource, unknownValue); break;
        }

        switch (powerDescriptor.currentPowerLevel())
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

        if (!powerDescriptor.isValid())
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

void zmNodeInfo::dataChanged(ActorVfsModel *vfs, uint64_t mac)
{
    if (mac == m_mac)
    {
        m_mac = 0; // reset
        setNode(vfs, mac);
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
