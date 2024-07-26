/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QRadioButton>
#include <QButtonGroup>
#include <QStandardItemModel>
#include <QSignalMapper>
#include <QScrollArea>
#include <QDateTime>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QUrl>
#include <QUrlQuery>
#include <QPainter>

#include "zm_cluster_info.h"
#include "deconz/dbg_trace.h"
#include "deconz/util_private.h"
#include "zm_attribute_info.h"
#include "zm_controller.h"
#include "zm_command_info.h"
#include "ui_zm_cluster_info.h"
#include "zm_node.h"
#include "zcl_private.h"

using namespace deCONZ;
using namespace deCONZ::literals;

#define ATTR_URL_ROLE (Qt::UserRole + 3)

zmClusterInfo::zmClusterInfo(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::zmClusterInfo),
    m_node(0),
    m_attributeDialog(0)
{
    ui->setupUi(this);
    m_attrModel = new QStandardItemModel(this);
    m_init = false;
    QStringList attrHeaders;

    attrHeaders.append("id");
    attrHeaders.append("name");
    attrHeaders.append("type");
    attrHeaders.append("access");
    attrHeaders.append("value");
    attrHeaders.append("mfc");

    m_attrModel->setHorizontalHeaderLabels(attrHeaders);

    ui->attrTableView->setModel(m_attrModel);
    ui->attrTableView->setEditTriggers(QTableView::NoEditTriggers);
    ui->discoveredOnlyCheckBox->setChecked(false);

    connect(ui->commandInfo, SIGNAL(zclCommandRequest(deCONZ::ZclCluster,deCONZ::ZclClusterSide,deCONZ::ZclCommand)),
            this, SLOT(zclCommandRequest(deCONZ::ZclCluster,deCONZ::ZclClusterSide,deCONZ::ZclCommand)));

    connect(ui->attributeUpdate, SIGNAL(clicked()),
            this, SLOT(readAttributesButtonClicked()));

    connect(ui->attributeDiscover, SIGNAL(clicked()),
            this, SLOT(discoverAttributesButtonClicked()));

    connect(ui->attrTableView, SIGNAL(doubleClicked(QModelIndex)),
            this, SLOT(attributeDoubleClicked(QModelIndex)));

    connect(ui->discoveredOnlyCheckBox, SIGNAL(toggled(bool)),
            this, SLOT(showAttributes()));

    ui->attributeDiscover->hide();
    ui->attributeGroupBox->hide();
    ui->discoveredOnlyCheckBox->hide();

    ui->attrTableView->viewport()->installEventFilter(this);
}

zmClusterInfo::~zmClusterInfo()
{
    delete ui;
}

void zmClusterInfo::setEndpoint(deCONZ::zmNode *node, int endpoint)
{
    m_init = false;

    if (node != m_node || endpoint != m_endpoint)
    {
        clear();
    }

    if (!node)
    {
        clear();
        return;
    }

    SimpleDescriptor *sd = node->getSimpleDescriptor(endpoint);

    if (!sd)
    {
        clear();
        return;
    }

    // set GUI address info
    deCONZ::ApsAddressMode addrMode; // presave
    quint8 dummyEndpoint;
    Address addr;
    deCONZ::getDestination(&addr, &addrMode, &dummyEndpoint);
    // update nwk and ext address
    // leave group address untouched
    addr.setNwk(node->address().nwk());
    addr.setExt(node->address().ext());
    // set new address and endpoint but with old address mode
    deCONZ::setDestination(addr, addrMode, sd->endpoint());

    m_endpoint = endpoint;
    m_node = node;
}

void zmClusterInfo::showCluster(quint16 id, deCONZ::ZclClusterSide clusterSide)
{
    if (!m_node)
    {
        return;
    }

    SimpleDescriptor *sd = m_node->getSimpleDescriptor(m_endpoint);
    if (!sd)
    {
        return;
    }

    if (id == 0x0019 && clusterSide != deCONZ::ClientCluster)
    {
        clusterSide = deCONZ::ClientCluster;
    }

    deCONZ::ZclCluster *cluster = sd->cluster(id, clusterSide);

    if (cluster)
    {
        m_init = false;
        m_clusterSide = clusterSide;
        m_clusterId = id;
        m_zclReadAttributeReqId = 0;
        m_readAttrTimeRef = {};
        ui->clusterGroupBox->setTitle(cluster->name() + " Cluster");
        ui->clusterDescription->setText(cluster->description());
        showAttributes();
        ui->commandInfo->setCluster(sd->profileId(), *cluster, m_clusterSide);
        return;
    }

    m_clusterId = InvalidClusterId;
}

void zmClusterInfo::clear()
{
    m_attrModel->removeRows(0, m_attrModel->rowCount());
    ui->clusterGroupBox->setTitle("");
    ui->clusterDescription->clear();
    m_node = 0;
    m_endpoint = InvalidEndpoint;
    m_clusterId = InvalidClusterId;
    m_zclReadAttributeReqId = 0;
    m_readAttrTimeRef = {};
}

/*! Refresh cluster attributes and command widgets with nodes current data.
 */
void zmClusterInfo::refresh()
{
    if (m_node && m_endpoint != InvalidEndpoint && m_clusterId != InvalidClusterId)
    {
        SimpleDescriptor *sd = m_node->getSimpleDescriptor(m_endpoint);

        if (sd)
        {
            deCONZ::ZclCluster *cluster = sd->cluster(m_clusterId, m_clusterSide);
            if (cluster)
            {
                showAttributes();
                ui->commandInfo->setCluster(sd->profileId(), *cluster, m_clusterSide);
            }
        }
    }
}

/*! Refresh cluster command widgets if node and cluster is the currently displayed.
    \param node - the node which has updated
    \param cluster - the cluster with updated data
 */
void zmClusterInfo::refreshNodeCommands(deCONZ::zmNode *node, deCONZ::ZclCluster *cluster)
{
    if (!node || !cluster)
    {
        return;
    }

    if (!m_node)
    {
        return;
    }

    if (m_node == node && cluster->id() == m_clusterId)
    {
        SimpleDescriptor *sd = m_node->getSimpleDescriptor(m_endpoint);
        auto *cl = getCluster();

        if (sd && cl)
        {
            ui->commandInfo->setCluster(sd->profileId(), *cl, m_clusterSide);
        }
    }
}

/*! Refresh cluster attributes if node and cluster is the currently displayed.
    \param node - the node which has updated
    \param cluster - the cluster with updated data
 */
void zmClusterInfo::refreshNodeAttributes(deCONZ::zmNode *node, quint8 endpoint, deCONZ::ZclCluster *cluster)
{
    if (!node || !cluster)
    {
        return;
    }

    if (m_node == node && m_node && m_endpoint == endpoint && m_clusterId == cluster->id())
    {
        if (m_clusterSide == deCONZ::ServerCluster && cluster->isServer())
        {
            showAttributes();
        }
        else if (m_clusterSide == deCONZ::ClientCluster && cluster->isClient())
        {
            showAttributes();
        }
    }
}

ZclCluster *zmClusterInfo::getCluster()
{
    if (m_node && m_clusterId !=  InvalidClusterId && m_endpoint != InvalidEndpoint)
    {
        return m_node->getCluster(m_endpoint, m_clusterId, m_clusterSide);
    }

    return nullptr;
}

bool zmClusterInfo::eventFilter(QObject *object, QEvent *event)
{
    if (object == ui->attrTableView->viewport())
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            QMouseEvent *e = static_cast<QMouseEvent*>(event);
            if (e->button() != Qt::LeftButton)
                return false;

            m_startDragPos = e->pos();
        }
        else if (event->type() == QEvent::MouseMove)
        {
            QMouseEvent *e = static_cast<QMouseEvent*>(event);
            if (e->button() == Qt::LeftButton)
                return false;

            if ((e->pos() - m_startDragPos).manhattanLength() < QApplication::startDragDistance())
                return false;

            return dragSelectedAttribute();
        }
    }

    return false;
}

void zmClusterInfo::scrollToAttributes()
{
    if (!parent() || !parent()->parent())
    {
        return;
    }

    QScrollArea *scrollArea = qobject_cast<QScrollArea*>(parent()->parent());
    if (scrollArea)
    {
        scrollArea->ensureWidgetVisible(ui->attrTableView);
    }
}

void zmClusterInfo::readAttributesButtonClicked()
{
    const auto now = deCONZ::steadyTimeRef();
    if (m_zclReadAttributeReqId == 0)
    {

    }
    else if (isValid(m_readAttrTimeRef) && (now - m_readAttrTimeRef < deCONZ::TimeMs{8000}))
    {
        return;
    }

    m_attrIndex = 0;
    proceedReadAttributes();
}

void zmClusterInfo::discoverAttributesButtonClicked()
{
    zclDiscoverAttributesRequest(0x0000);
}

void zmClusterInfo::zclDiscoverAttributesRequest(quint16 startIndex)
{
    if (m_node && m_endpoint != InvalidEndpoint && m_clusterId != InvalidClusterId)
    {
        SimpleDescriptor *sd = m_node->getSimpleDescriptor(m_endpoint);
        auto *cluster = getCluster();
        if (!sd || !cluster)
        {
            return;
        }

        const quint8 MaxDiscoverAttributes = 4;

        deCONZ::ZclCommand command;
        deCONZ::ZclAttribute attr;

        command.setId(deCONZ::ZclDiscoverAttributesId);
        command.setResponseId(deCONZ::ZclDiscoverAttributesResponseId);
        command.setIsProfileWide(true);
        command.setDisableDefaultResponse(true);

        attr.setDataType(deCONZ::Zcl16BitUint);
        attr.setValue((quint64)startIndex); // start attribute
        command.parameters().push_back(attr);

        attr.setDataType(deCONZ::Zcl8BitUint);
        attr.setValue((quint64)MaxDiscoverAttributes);
        command.parameters().push_back(attr);

        deCONZ::controller()->zclCommandRequest(m_node->address(), deCONZ::ApsNwkAddress, *sd, *cluster, command);
    }
}

void zmClusterInfo::apsDataConfirm(const deCONZ::ApsDataConfirm &conf)
{
    if (conf.id() == m_zclReadAttributeReqId && conf.dstEndpoint() == m_endpoint && m_node)
    {
        if (m_node->address().ext() == conf.dstAddress().ext() ||
            (conf.dstAddressMode() == deCONZ::ApsNwkAddress && m_node->address().nwk() == conf.dstAddress().nwk()))
        {
            m_zclReadAttributeReqId = 0;
            m_readAttrTimeRef = {};
            proceedReadAttributes();
        }
    }

    if (m_apsReqIds.empty())
    {
        return;
    }

    auto i = m_apsReqIds.begin();
    auto end = m_apsReqIds.end();

    for (; i != end; ++i)
    {
        if (*i == conf.id())
        {
            m_apsReqIds.erase(i);
            break;
        }
    }

    if (m_apsReqIds.empty())
    {
        ui->commandInfo->zclAllRequestsConfirmed();
    }
}

void zmClusterInfo::attributeDoubleClicked(const QModelIndex &index)
{
    if (m_attributeDialog)
    {
        return;
    }

    auto *cluster = getCluster();
    if (!cluster)
    {
        return;
    }

    QVariant data = m_attrModel->item(index.row(), 0)->data();

    if (data.type() == QVariant::UInt)
    {
        bool ok;
        uint id = data.toUInt();

        zmAttributeInfo *info = new zmAttributeInfo(this);
        info->setModal(false);

        connect(info, SIGNAL(zclWriteAttribute(deCONZ::ZclAttribute)),
                this, SLOT(zclWriteAttribute(deCONZ::ZclAttribute)));

        connect(info, SIGNAL(zclReadAttribute(deCONZ::ZclAttribute)),
                this, SLOT(zclReadAttribute(deCONZ::ZclAttribute)));

        connect(info, SIGNAL(zclReadReportConfiguration(deCONZ::ZclAttribute)),
                this, SLOT(zclReadReportConfiguration(deCONZ::ZclAttribute)));

        connect(info, SIGNAL(zclWriteReportConfiguration(deCONZ::ZclAttribute,quint8)),
                this, SLOT(zclWriteReportConfiguration(deCONZ::ZclAttribute,quint8)));

        connect(info, SIGNAL(finished(int)),
                this, SLOT(attributeDialogeFinished(int)));

        ok = false;
        for (uint i = 0; i < cluster->attributes().size(); i++)
        {
            const deCONZ::ZclAttribute &attr = cluster->attributes()[i];
            if (attr.id() == id)
            {
                QString mfc = "0x" + QString("%1").arg(attr.manufacturerCode(), 4, 16, QLatin1Char('0')).toUpper();

                if (mfc == m_attrModel->data(m_attrModel->index(index.row(), 5)).toString())
                {
                    info->setAttribute(m_endpoint, m_clusterId, m_clusterSide, attr);
                    ok = true;
                    break;
                }
            }
        }

        if (ok)
        {
            info->show();
            info->raise();
            info->activateWindow();
            m_attributeDialog = info;
        }
        else
        {
            DBG_Printf(DBG_INFO, "attribute id: 0x%04X not found\n", id);
            delete info;
        }
    }
}

bool zmClusterInfo::zclCommandRequest(const deCONZ::ZclCluster &cluster, deCONZ::ZclClusterSide side, const deCONZ::ZclCommand &command)
{
    if (m_node && m_endpoint < InvalidEndpoint)
    {
        // push modified command to node cache
        SimpleDescriptor *sd = m_node->getSimpleDescriptor(m_endpoint);
        if (sd)
        {
            deCONZ::ZclCluster *cl = sd->cluster(cluster.id(), side);
            if (cl)
            {
                for (uint i = 0; i < cl->commands().size(); i++)
                {
                    deCONZ::ZclCommand &ccmd = cl->commands()[i];
                    if (ccmd.id() == command.id() && (ccmd.directionReceived() == command.directionReceived()))
                    {
                        ccmd = command;
                        break;
                    }
                }
            }
        }

        Address addr;
        deCONZ::ApsAddressMode addressMode;
        quint8 endpoint;

        m_apsReqIds.clear();

        if (sd && deCONZ::getDestination(&addr, &addressMode, &endpoint))
        {
            // just forward
            int ret = deCONZ::controller()->zclCommandRequest(addr, addressMode, *sd, cluster, command);
            if (ret > 0) // a valid aps request id is greater 0
            {
                m_apsReqIds.push_back(ret);
            }
            else
            {
                ui->commandInfo->zclCommandRequestError();
            }
        }

        if (!m_apsReqIds.empty())
        {
            return true;
        }
    }

    return false;
}

void zmClusterInfo::zclWriteAttribute(const deCONZ::ZclAttribute &attribute)
{
    if (m_node && m_endpoint != InvalidEndpoint && m_clusterId != InvalidClusterId)
    {
        deCONZ::ZclCommand command;
        deCONZ::ZclAttribute attr;

        command.setId(deCONZ::ZclWriteAttributesId);
        command.setResponseId(deCONZ::ZclWriteAttributesResponseId);
        command.setIsProfileWide(true);
        command.setDisableDefaultResponse(true); // get the write attributes response
        command.setManufacturerId(attribute.manufacturerCode());

        attr.setDataType(deCONZ::ZclAttributeId);
        attr.setValue((quint64)attribute.id());
        command.parameters().push_back(attr);

        attr.setDataType(deCONZ::Zcl8BitUint);
        attr.setValue((quint64)attribute.dataType());
        command.parameters().push_back(attr);

        command.parameters().push_back(attribute);

//        QByteArray payload;
//        QDataStream stream(&payload, QIODevice::WriteOnly);
//        stream.setByteOrder(QDataStream::LittleEndian);

//        stream << attribute.id();
//        stream << attribute.dataType();
//        attribute.writeToStream(stream);

        SimpleDescriptor *sd = m_node->getSimpleDescriptor(m_endpoint);
        auto *cluster = getCluster();
        if (!sd || !cluster)
        {
            return;
        }

        deCONZ::controller()->zclCommandRequest(m_node->address(), deCONZ::ApsNwkAddress, *sd, *cluster, command);
    }
}

void zmClusterInfo::zclReadAttribute(const deCONZ::ZclAttribute &attribute)
{
    if (m_node && m_endpoint != InvalidEndpoint && m_clusterId != InvalidClusterId)
    {
        SimpleDescriptor *sd = m_node->getSimpleDescriptor(m_endpoint);
        auto *cluster = getCluster();

        if (!sd || !cluster)
        {
            return;
        }

        deCONZ::ZclCommand command;
        deCONZ::ZclAttribute attr;

        command.setId(deCONZ::ZclReadAttributesId);
        command.setResponseId(deCONZ::ZclReadAttributesResponseId);
        command.setIsProfileWide(true);
        command.setDisableDefaultResponse(true); // get the write attributes response
        command.setManufacturerId(attribute.manufacturerCode());

        attr.setDataType(deCONZ::ZclAttributeId);
        attr.setValue((quint64)attribute.id());
        command.parameters().push_back(attr);

        int ret = deCONZ::controller()->zclCommandRequest(m_node->address(), deCONZ::ApsNwkAddress, *sd, *cluster, command);
        if (ret > 0)
        {
            m_apsReqIds.push_back(ret);
        }
    }
}

void zmClusterInfo::zclWriteReportConfiguration(const deCONZ::ZclAttribute &attribute, quint8 direction)
{
    if (m_node && m_endpoint != InvalidEndpoint && m_clusterId != InvalidClusterId)
    {
        deCONZ::ZclCommand command;
        deCONZ::ZclAttribute attr;

        command.setId(deCONZ::ZclConfigureReportingId);
        command.setResponseId(deCONZ::ZclConfigureReportingResponseId);
        command.setIsProfileWide(true);
        command.setDisableDefaultResponse(true); // get the write attributes response
        command.setManufacturerId(attribute.manufacturerCode());

        if (direction == 0x00)
        {
            attr.setDataType(deCONZ::Zcl8BitUint);
            attr.setValue((quint64)direction);
            command.parameters().push_back(attr);

            attr.setDataType(deCONZ::ZclAttributeId);
            attr.setValue((quint64)attribute.id());
            command.parameters().push_back(attr);

            attr.setDataType(deCONZ::Zcl8BitUint);
            attr.setValue((quint64)attribute.dataType());
            command.parameters().push_back(attr);

            attr.setDataType(deCONZ::Zcl16BitUint);
            attr.setValue((quint64)attribute.minReportInterval());
            command.parameters().push_back(attr);

            attr.setDataType(deCONZ::Zcl16BitUint);
            attr.setValue((quint64)attribute.maxReportInterval());
            command.parameters().push_back(attr);

            deCONZ::ZclDataType dataType = deCONZ::zclDataBase()->dataType(attribute.dataType());

            if (dataType.isValid() && dataType.isAnalog())
            {
                attr = attribute; // for data type
                attr.setNumericValue(attribute.reportableChange());
                command.parameters().push_back(attr);
//                attribute.writeReportableChangeToStream(stream);
            }

            SimpleDescriptor *sd = m_node->getSimpleDescriptor(m_endpoint);
            auto *cluster = getCluster();

            if (!sd || !cluster)
            {
                return;
            }

            deCONZ::controller()->zclCommandRequest(m_node->address(), deCONZ::ApsNwkAddress, *sd, *cluster, command);
        }
        else if (direction == 0x01)
        {

        }
    }
}

void zmClusterInfo::zclReadReportConfiguration(const deCONZ::ZclAttribute &attribute)
{
    if (m_node && m_endpoint != InvalidEndpoint && m_clusterId != InvalidClusterId)
    {
        deCONZ::ZclCommand command;
        deCONZ::ZclAttribute attr;

        command.setId(deCONZ::ZclReadReportingConfigId);
        command.setIsProfileWide(true);
        command.setDisableDefaultResponse(true); // get the write attributes response
        command.setManufacturerId(attribute.manufacturerCode());

        attr.setDataType(deCONZ::Zcl8BitUint);
        attr.setValue((quint64)0x00); // direction
        command.parameters().push_back(attr);

        attr.setDataType(deCONZ::ZclAttributeId);
        attr.setValue((quint64)attribute.id());
        command.parameters().push_back(attr);

        SimpleDescriptor *sd = m_node->getSimpleDescriptor(m_endpoint);
        auto *cluster = getCluster();
        if (!sd || !cluster)
        {
            return;
        }

        deCONZ::controller()->zclCommandRequest(m_node->address(), deCONZ::ApsNwkAddress, *sd, *cluster, command);
    }
}

void zmClusterInfo::zclCommandResponse(const deCONZ::ApsDataIndication &ind, const deCONZ::ZclFrame &zclFrame)
{
    // only forward response for current showed cluster
    if (m_node && m_clusterId != InvalidClusterId)
    {
        auto *cluster = getCluster();
        if (!cluster)
        {
            return;
        }

        if (ind.clusterId() != cluster->oppositeId())
        {
            return;
        }

        if (ind.srcAddress().hasExt() && (m_node->address().ext() == ind.srcAddress().ext()))
        {
        }
        else if (ind.srcAddress().hasNwk() && (m_node->address().nwk() == ind.srcAddress().nwk()))
        {
        }
        else
        {
            return;
        }

        ui->commandInfo->zclCommandResponse(ind, zclFrame);

        if (m_attributeDialog != 0)
        {
            // forward only profile wide commands
            if ((zclFrame.frameControl() & deCONZ::ZclFCClusterCommand) == 0)
            {
                m_attributeDialog->zclCommandResponse(zclFrame);
            }
        }
    }
}

void zmClusterInfo::attributeDialogeFinished(int result)
{
    Q_UNUSED(result)
    m_attributeDialog = 0;
}

void zmClusterInfo::showAttributes()
{
    if (m_clusterId == InvalidClusterId)
    {
         DBG_Printf(DBG_INFO, "can't set attributes no cluster choosen");
         ui->attributeGroupBox->hide();
         return;
    }

    if (!m_node || m_node->nodeDescriptor().isNull())
    {
        return;
    }

    auto *cluster = getCluster();
    if (!cluster)
    {
        return;
    }

    if (!m_init)
    {
        m_attrModel->setRowCount(0);
        //m_attrModel->setRowCount(m_cluster.attributes().size() + m_cluster.attributeSets().size());
        m_attrModel->setColumnCount(6);
        ui->attrTableView->horizontalHeader()->stretchLastSection();
    }

    int row = 0;
    QVariant data;
    bool discoveredOnly = ui->discoveredOnlyCheckBox->isChecked();
    const auto mfcode = m_node->nodeDescriptor().manufacturerCode_t();

    std::vector<deCONZ::ZclAttributeSet> attributeSets;
    attributeSets.reserve(cluster->attributeSets().size() + 1);

    if (!cluster->attributeSets().empty())
    {
        deCONZ::ZclAttributeSet a(0xFFFF, QLatin1String("")); // generic
        attributeSets.push_back(a);
    }

    for (const auto &a : cluster->attributeSets())
    {
        attributeSets.push_back(a);
    }

    // if attribute sets are available
    if (!attributeSets.empty())
    {
        for (uint s = 0; s < attributeSets.size(); s++)
        {
            const deCONZ::ZclAttributeSet &attrSet =  attributeSets[s];

            if (attrSet.manufacturerCode() > 0 && mfcode != ManufacturerCode_t(attrSet.manufacturerCode()))
            {
                continue;
            }

            if (!attrSet.description().isEmpty())
            {
                if (!m_init)
                {
                    m_attrModel->setRowCount(m_attrModel->rowCount() + 1);
                }

                m_attrModel->setData(m_attrModel->index(row, 0), QString::number((uint)attrSet.id(), 16));
                m_attrModel->setData(m_attrModel->index(row, 1), attrSet.description());

                m_attrModel->item(row, 0)->setBackground(palette().dark().color().lighter(120));
                m_attrModel->item(row, 0)->setForeground(palette().brightText());
                m_attrModel->item(row, 1)->setBackground(palette().dark().color().lighter(120));
                m_attrModel->item(row, 1)->setForeground(palette().brightText());

                ui->attrTableView->setSpan(row, 1 , 1, 5);

                row++;
            }

            for (const auto &attr : cluster->attributes())
            {
                if (attr.attributeSet() != attrSet.id())
                {
                    continue;
                }

                if (attr.attributeSetManufacturerCode() == attrSet.manufacturerCode() ||
                    (attr.manufacturerCode_t() == 0x115f_mfcode && mfcode == 0x1037_mfcode))
                {
                    if (discoveredOnly && !attr.isAvailable())
                    {
                        continue;
                    }

                    if (mfcode == 0x1037_mfcode && attr.manufacturerCode_t() == 0x115f_mfcode)
                    {
                        // Xiaomi uses both
                    }
                    else if (attr.manufacturerCode() > 0 && mfcode != attr.manufacturerCode_t())
                    {
                        continue;
                    }

                    const deCONZ::ZclDataType &dataType = deCONZ::zclDataBase()->dataType(attr.dataType());
                    QString aid = "0x" + QString("%1").arg(attr.id(), 4, 16, QLatin1Char('0')).toUpper();

                    if (!m_init)
                    {
                        m_attrModel->setRowCount(m_attrModel->rowCount() + 1);
                    }

                    m_attrModel->setData(m_attrModel->index(row, 0), aid);
                    m_attrModel->item(row, 0)->setData((uint)attr.id());

                    m_attrModel->setData(m_attrModel->index(row, 1), attr.name());
                    m_attrModel->setData(m_attrModel->index(row, 2), dataType.shortname());
                    m_attrModel->setData(m_attrModel->index(row, 3), attr.isReadonly() ? "r" : "rw");

                    data = attr.toString(dataType, deCONZ::ZclAttribute::Prefix);
                    m_attrModel->setData(m_attrModel->index(row, 4), data);
                    
                    QString mfc = "0x" + QString("%1").arg(attr.manufacturerCode(), 4, 16, QLatin1Char('0')).toUpper();
                    m_attrModel->setData(m_attrModel->index(row, 5), mfc);

                    // visual difference if a attribute is available
                    for (int j = 0; j < m_attrModel->columnCount(); j++)
                    {
                        m_attrModel->item(row, j)->setEnabled(attr.isAvailable());
                    }

                    row++;
                }
            }
        }
    }
    else
    {
        for (const auto &attr : cluster->attributes())
        {
            if (discoveredOnly && !attr.isAvailable())
            {
                continue;
            }

            if (mfcode == 0x1037_mfcode && attr.manufacturerCode_t() == 0x115f_mfcode)
            {
                // Xiaomi uses both
            }
            else if (attr.manufacturerCode() > 0 && mfcode != attr.manufacturerCode_t())
            {
                continue;
            }

            const deCONZ::ZclDataType &dataType = deCONZ::zclDataBase()->dataType(attr.dataType());
            QString aid = "0x" + QString("%1").arg(attr.id(), 4, 16, QLatin1Char('0')).toUpper();

            if (!m_init)
            {
                m_attrModel->setRowCount(m_attrModel->rowCount() + 1);
            }

            m_attrModel->setData(m_attrModel->index(row, 0), aid);
            m_attrModel->item(row, 0)->setData((uint)attr.id());

            m_attrModel->setData(m_attrModel->index(row, 1), attr.name());
            m_attrModel->setData(m_attrModel->index(row, 2), dataType.shortname());
            m_attrModel->setData(m_attrModel->index(row, 3), attr.isReadonly() ? "r" : "rw");

            data = attr.toString(dataType, deCONZ::ZclAttribute::Prefix);
            m_attrModel->setData(m_attrModel->index(row, 4), data);

            QString mfc = "0x" + QString("%1").arg(attr.manufacturerCode(), 4, 16, QLatin1Char('0')).toUpper();
            m_attrModel->setData(m_attrModel->index(row, 5), mfc);

            // visual difference if a attribute is available
            for (int j = 0; j < m_attrModel->columnCount(); j++)
            {
                m_attrModel->item(row, j)->setEnabled(attr.isAvailable());
            }

            row++;
        }
    }

    if (!m_init)
    {
        //ui->attrTableView->resizeColumnsToContents();
        ui->attrTableView->resizeColumnToContents(0);
        ui->attrTableView->resizeColumnToContents(2);
        ui->attrTableView->resizeColumnToContents(3);
        ui->attrTableView->resizeColumnToContents(4);
        ui->attrTableView->setColumnHidden(5, true);
        ui->attrTableView->resizeRowsToContents();
        ui->attrTableView->horizontalHeader()->setStretchLastSection(true);
        m_init = true;
    }

    if (m_attrModel->rowCount() == 0)
    {
        ui->attributeGroupBox->hide();
    }
    else
    {
        ui->attributeGroupBox->show();
    }
}

void zmClusterInfo::proceedReadAttributes()
{
    if (!m_node || m_endpoint == InvalidEndpoint || m_clusterId == InvalidClusterId)
    {
        m_zclReadAttributeReqId = 0;
        return;
    }

    const uint MaxReadAttributes = 4;
    SimpleDescriptor *sd = m_node->getSimpleDescriptor(m_endpoint);

    auto *cluster = getCluster();

    if (!sd || !cluster)
    {
        m_zclReadAttributeReqId = 0;
        return;
    }

    // split into multible commands
    while (m_attrIndex < cluster->attributes().size())
    {
        deCONZ::ZclCommand command;
        command.setId(deCONZ::ZclReadAttributesId);
        command.setResponseId(deCONZ::ZclReadAttributesResponseId);
        command.setIsProfileWide(true);
        command.setDisableDefaultResponse(true);

        uint i = 0;
        for (i = 0; m_attrIndex < cluster->attributes().size(); )
        {
            if (i >= MaxReadAttributes)
            {
                break;
            }

            const deCONZ::ZclAttribute &attr = cluster->attributes()[m_attrIndex];

            if (attr.isManufacturerSpecific() && i > 0)
            {
                break; // put manufacturer specific attributes in separate commands
            }

            if (attr.isAvailable())
            {
                DBG_Printf(DBG_ZCL, "ZCL read cluster: 0x%04X, attribute: 0x%04X\n", m_clusterId, attr.id());
                deCONZ::ZclAttribute readAttr;
                readAttr.setDataType(deCONZ::ZclAttributeId);
                readAttr.setValue((quint64)attr.id());
                command.parameters().push_back(readAttr);
                i++;
            }

            m_attrIndex++;

            if (attr.isManufacturerSpecific())
            {
                command.setManufacturerId(attr.manufacturerCode());
                break;
            }

            // only allow one complex attribute per request
            if (attr.dataType() == deCONZ::ZclCharacterString ||
                attr.dataType() == deCONZ::ZclArray)
            {
                break;
            }
        }

        // done
        if (i == 0)
        {
            break;
        }

        int ret = deCONZ::controller()->zclCommandRequest(m_node->address(), deCONZ::ApsNwkAddress, *sd, *cluster, command);
        if (ret > 0)
        {
            m_readAttrTimeRef = deCONZ::steadyTimeRef();
            m_zclReadAttributeReqId = ret;
        }
        break;
    }
}

bool zmClusterInfo::dragSelectedAttribute()
{
    auto *cluster = getCluster();
    if (!cluster)
        return false;

    auto indexes = ui->attrTableView->selectionModel()->selectedRows();
    if (indexes.isEmpty())
        return false;

    const QModelIndex &index = indexes.first();
    if (!index.isValid())
        return false;

    QVariant data = m_attrModel->item(index.row(), 0)->data();
    if (data.type() != QVariant::UInt)
        return false;

    uint attrId = data.toUInt();

    const auto attr = std::find_if(cluster->attributes().cbegin(), cluster->attributes().cend(), [&](const deCONZ::ZclAttribute &i)
    {
        return i.id() == attrId;
    });

    if (attr == cluster->attributes().cend())
        return false;

    deCONZ::ZclDataType type = deCONZ::zclDataBase()->dataType(attr->dataType());

    if (!type.isValid())
        return false;

     QUrl url;
     url.setScheme("zclattr");
     url.setPath("attr");

     QUrlQuery urlQuery;
     urlQuery.addQueryItem("ep", "0x" + QString::number(m_endpoint, 16));
     urlQuery.addQueryItem("cid", "0x" + QString::number(cluster->id(), 16));
     urlQuery.addQueryItem("a", "0x" + QString::number(attr->id(), 16));
     urlQuery.addQueryItem("dt", "0x" + QString::number(attr->dataType(), 16));
     urlQuery.addQueryItem("cs", (m_clusterSide == deCONZ::ClientCluster ? "c" : "s"));
     urlQuery.addQueryItem("mf", "0x" + QString::number(attr->manufacturerCode(), 16));
     urlQuery.addQueryItem("val", attr->toString());
     if (type.isAnalog())
     {
         urlQuery.addQueryItem("t", "A");
         urlQuery.addQueryItem("rmin", QString::number(attr->minReportInterval()));
         urlQuery.addQueryItem("rmin", QString::number(attr->maxReportInterval()));
         urlQuery.addQueryItem("rchange", QString::number(attr->reportableChange().u64));
     }
     else
     {
         urlQuery.addQueryItem("t", "D");
     }

     url.setQuery(urlQuery.toString());

     QDrag *drag = new QDrag(this);
     QMimeData *mimeData = new QMimeData;

     QString attrIdString = QString("0x%1").arg(attr->id(), 4, 16, QLatin1Char('0'));

     auto fm = fontMetrics();
     int w = fm.boundingRect(attr->name() + attrIdString).width() + fm.xHeight() * 2;
     int h = fm.height() + 8;
     QPixmap pm(w, h);
     pm.fill(Qt::transparent);

     QPainter p(&pm);

     p.setRenderHint(QPainter::Antialiasing, true);
     p.setBrush(Qt::white);
     p.setPen(QColor(64,64,64));
     p.drawRoundedRect(QRect(0, 0, w, h), 4, 4);

     const QColor colorAttr(171, 64, 18);
     p.setPen(colorAttr);
     p.drawText(QRect(4, 0, w, h), Qt::AlignLeft | Qt::AlignVCenter, attrIdString);

     p.setPen(Qt::black);
     p.drawText(QRect(0, 0, w - fm.xHeight(), h), Qt::AlignRight | Qt::AlignVCenter, attr->name());
     drag->setPixmap(pm);


     mimeData->setUrls({url});
     drag->setMimeData(mimeData);

     drag->exec(Qt::CopyAction);
     return true;
}
