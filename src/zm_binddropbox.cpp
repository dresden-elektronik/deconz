/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QButtonGroup>
#include <QDragEnterEvent>
#include <QHeaderView>
#include <QMimeData>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include "zm_binddropbox.h"
#include "zm_controller.h"
#include "ui_zm_binddropbox.h"
#include "zcl_private.h"

zmBindDropbox::zmBindDropbox(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::zmBindDropbox),
    m_hasSrcData(false),
    m_srcAddr(0),
    m_dstAddr(0),
    m_dstGroupAddr(0),
    m_binderAddr(0),
    m_srcEndpoint(0),
    m_dstEndpoint(0),
    m_cluster(0),
    m_bindingTableInfo(nullptr),
    m_bindingTableView(nullptr),
    m_selectedNodeAddr(0)
{
    ui->setupUi(this);
    setAcceptDrops(true);

    m_bindingTableInfo = new QLabel(this);
    m_bindingTableInfo->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->bindingTableLayout->addWidget(m_bindingTableInfo);

    m_bindingTableView = new QTableWidget(this);
    m_bindingTableView->setColumnCount(7);
    m_bindingTableView->setHorizontalHeaderLabels({
        tr("Src"),
        tr("Src EP"),
        tr("Cluster"),
        tr("Address Mode"),
        tr("Dst"),
        tr("Dst EP"),
        tr("Cluster Name")
    });
    m_bindingTableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_bindingTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_bindingTableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_bindingTableView->setAlternatingRowColors(true);
    m_bindingTableView->verticalHeader()->setVisible(false);
    m_bindingTableView->horizontalHeader()->setStretchLastSection(true);
    m_bindingTableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_bindingTableView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_bindingTableView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_bindingTableView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_bindingTableView->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_bindingTableView->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_bindingTableView->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_bindingTableView->setMinimumHeight(140);
    ui->bindingTableLayout->addWidget(m_bindingTableView);

    clear();

    connect(ui->bindButton, SIGNAL(clicked()),
            this, SLOT(bind()));

    connect(ui->unbindButton, SIGNAL(clicked()),
            this, SLOT(unbind()));

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(20000);

    connect(m_timer, SIGNAL(timeout()),
            this, SLOT(bindTimeout()));

    QButtonGroup *bgroup = new QButtonGroup(this);
    bgroup->addButton(ui->ieeeRadioButton);
    bgroup->addButton(ui->groupRadioButton);
    bgroup->setExclusive(true);

    ui->ieeeRadioButton->setChecked(true);
    dstRadioButtonClicked(ui->ieeeRadioButton);

    connect(bgroup, SIGNAL(buttonClicked(QAbstractButton*)),
            this, SLOT(dstRadioButtonClicked(QAbstractButton*)));

    connect(ui->groupAddressLineEdit, SIGNAL(textChanged(QString)),
            this,SLOT(dstGroupTextChanged(QString)));
}

zmBindDropbox::~zmBindDropbox()
{
    delete ui;
}

void zmBindDropbox::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        QUrl url = event->mimeData()->urls().first();
        if (url.scheme() == CL_URL_SCHEME ||
            url.scheme() == EP_URL_SCHEME)
        {
            event->acceptProposedAction();
        }
    }
}

void zmBindDropbox::dragMoveEvent(QDragMoveEvent *event)
{
    Q_UNUSED(event);
}

void zmBindDropbox::dropEvent(QDropEvent *event)
{
    bool ok;
    uint16_t id;
    deCONZ::ZclDevice dev;
    deCONZ::ZclProfile prof;
    QWidget *child = childAt(event->pos());
    QUrl url = event->mimeData()->urls().first();
    QUrlQuery urlq(event->mimeData()->urls().first());

    if (!child)
    {
        return;
    }

    // profile
    if (urlq.hasQueryItem(CL_ITEM_PROFILE_ID))
    {
        if (setU16(0, &id, urlq.queryItemValue(CL_ITEM_PROFILE_ID)))
        {
            prof = deCONZ::zclDataBase()->profile(id);
        }
    }

    // device
    if (urlq.hasQueryItem(CL_ITEM_DEVICE_ID))
    {
        if (setU16(0, &id, urlq.queryItemValue(CL_ITEM_DEVICE_ID)))
        {
            dev = deCONZ::zclDataBase()->device(prof.id(), id);
        }
    }

    if (child == ui->srcDropTarget)
    {
        if (url.scheme() == CL_URL_SCHEME)
        {
            ok = false;

            if (urlq.hasQueryItem(CL_ITEM_EXT_ADDR) &&
                urlq.hasQueryItem(CL_ITEM_CLUSTER_ID) &&
                urlq.hasQueryItem(CL_ITEM_ENDPOINT))
            {
                ui->profile->setText(prof.name());
                ui->srcDeviceType->setText(dev.name());
                        ok = setU64(ui->srcExtAddress, &m_srcAddr, urlq.queryItemValue(CL_ITEM_EXT_ADDR));
                if (ok) ok = setU64(ui->binderExtAddress, &m_binderAddr, urlq.queryItemValue(CL_ITEM_EXT_ADDR));
                if (ok) ok = setU16(ui->srcCluster, &m_cluster, urlq.queryItemValue(CL_ITEM_CLUSTER_ID));
                if (ok) ok = setU8(ui->srcEndpoint, &m_srcEndpoint, urlq.queryItemValue(CL_ITEM_ENDPOINT));

            }

            m_hasSrcData = ok;
            if (!ok)
            {
                ui->profile->clear();
                ui->srcDeviceType->clear();
                ui->srcExtAddress->clear();
                ui->binderExtAddress->clear();
                ui->srcCluster->clear();
                ui->srcEndpoint->clear();
            }
        }
    }
    else if (child == ui->dstDropTarget)
    {
        ok = false;

        if (urlq.hasQueryItem(CL_ITEM_EXT_ADDR) &&
            urlq.hasQueryItem(CL_ITEM_ENDPOINT))
        {
            ui->dstDeviceType->setText(dev.name());
                    ok = setU64(ui->dstExtAddress, &m_dstAddr, urlq.queryItemValue(CL_ITEM_EXT_ADDR));
            if (ok) ok = setU8(ui->dstEndpoint, &m_dstEndpoint, urlq.queryItemValue(CL_ITEM_ENDPOINT));
            if (ok)
            {
                ui->ieeeRadioButton->setChecked(true);
                dstRadioButtonClicked(ui->ieeeRadioButton);
            }
        }

        if (!ok)
        {
            ui->dstDeviceType->clear();
            ui->dstExtAddress->clear();
            ui->dstEndpoint->clear();
        }
    }
    else if (child == ui->binderExtAddress)
    {
        if (urlq.hasQueryItem(CL_ITEM_EXT_ADDR))
        {
            setU64(ui->binderExtAddress, &m_binderAddr, urlq.queryItemValue(CL_ITEM_EXT_ADDR));
        }
    }

    ui->statusLabel->clear();
    checkButtons();
}

bool zmBindDropbox::setU8(QLabel *label, quint8 *value, const QString &source)
{
    bool ok;

    *value = (quint8)source.toUShort(&ok, 16);

    if (!ok || source.isEmpty())
    {
        if (label)
        {
            label->clear();
        }
        *value = 0;
        return false;
    }

    if (label)
    {
        label->setText(source);
    }

    return true;
}

bool zmBindDropbox::setU16(QLabel *label, quint16 *value, const QString &source)
{
    bool ok;

    *value = source.toUShort(&ok, 16);

    if (!ok || source.isEmpty())
    {
        if (label)
        {
            label->clear();
        }
        *value = 0;
        return false;
    }

    if (label)
    {
        label->setText(source);
    }

    return true;
}

bool zmBindDropbox::setU64(QLabel *label, quint64 *value, const QString &source)
{
    bool ok;

    *value = source.toULongLong(&ok, 16);

    if (!ok || source.isEmpty())
    {
        if (label)
        {
            label->clear();
        }
        *value = 0;
        return false;
    }

    if (label)
    {
        label->setText(source);
    }

    return true;
}

void zmBindDropbox::clear()
{
    ui->profile->clear();
    ui->srcDeviceType->clear();
    ui->srcCluster->clear();
    ui->srcEndpoint->clear();
    ui->srcExtAddress->clear();
    ui->dstDeviceType->clear();
    ui->dstEndpoint->clear();
    ui->dstExtAddress->clear();
    ui->binderExtAddress->clear();
    ui->bindButton->setEnabled(false);
    ui->unbindButton->setEnabled(false);
    if (m_bindingTableInfo)
    {
        m_bindingTableInfo->setText(tr("Binding table: no data"));
    }
    if (m_bindingTableView)
    {
        m_bindingTableView->setRowCount(0);
    }
    m_hasSrcData = false;
}

void zmBindDropbox::bind()
{
    if (m_hasSrcData && hasDstData())
    {
        deCONZ::BindReq req;

        req.unbind = false;
        req.srcAddr = m_srcAddr;
        req.srcEndpoint = m_srcEndpoint;
        req.clusterId = m_cluster;
        if (ui->ieeeRadioButton->isChecked())
        {
            req.dstAddrMode = 0x03; // ext addressing
            req.dstExtAddr = m_dstAddr;
            req.dstEndpoint = m_dstEndpoint;
        }
        else if (ui->groupRadioButton->isChecked())
        {
            req.dstAddrMode = 0x01; // group addressing
            req.dstGroupAddr = m_dstGroupAddr;
        }
        req.binderAddr = m_binderAddr;

        deCONZ::controller()->bindReq(&req);
        ui->statusLabel->setText(tr("binding ..."));
        m_timer->start();

        ui->srcDropTarget->setEnabled(false);
        ui->dstDropTarget->setEnabled(false);
        ui->bindButton->setEnabled(false);
        ui->unbindButton->setEnabled(false);
    }
}

void zmBindDropbox::unbind()
{
    if (m_hasSrcData && hasDstData())
    {
        deCONZ::BindReq req;

        req.unbind = true;
        req.srcAddr = m_srcAddr;
        req.srcEndpoint = m_srcEndpoint;
        req.clusterId = m_cluster;
        if (ui->ieeeRadioButton->isChecked())
        {
            req.dstAddrMode = 0x03; // ext addressing
            req.dstExtAddr = m_dstAddr;
            req.dstEndpoint = m_dstEndpoint;
        }
        else if (ui->groupRadioButton->isChecked())
        {
            req.dstAddrMode = 0x01; // group addressing
            req.dstGroupAddr = m_dstGroupAddr;
        }
        req.binderAddr = m_binderAddr;

        deCONZ::controller()->bindReq(&req);
        ui->statusLabel->setText(tr("unbinding ..."));
        m_timer->start();

        ui->srcDropTarget->setEnabled(false);
        ui->dstDropTarget->setEnabled(false);
        ui->bindButton->setEnabled(false);
        ui->unbindButton->setEnabled(false);
    }
}

void zmBindDropbox::bindIndCallback(const deCONZ::ApsDataIndication &ind)
{
    if (!m_timer->isActive())
    {
        return;
    }

    if (ind.asdu().size() < 2)
    {
        return;
    }

    if (ind.srcAddress().ext() != m_srcAddr)
    {
        return;
    }

    quint8 status = ind.asdu().at(1);

    if (m_hasSrcData && hasDstData())
    {
        m_timer->stop();
        QString text;

        if (status == deCONZ::ZdpSuccess)
        {
            text = tr("success");
        }
        else if (status == deCONZ::ZdpTableFull)
        {
            text = tr("failed: table full");
        }
        else if (status == deCONZ::ZdpNotSupported)
        {
            text = tr("failed: not supported");
        }
        else if (status == deCONZ::ZdpInvalidEndpoint)
        {
            text = tr("failed: invalid endpoint");
        }
        else if (status == deCONZ::ZdpNotAuthorized)
        {
            text = tr("failed: not authorized");
        }
        else if (status == deCONZ::ZdpNoEntry)
        {
            text = tr("failed: no entry");
        }
        else
        {
            text = tr("failed: unknown error");
        }

        ui->statusLabel->setText(text);
    }

    ui->srcDropTarget->setEnabled(true);
    ui->dstDropTarget->setEnabled(true);
    checkButtons();
}

void zmBindDropbox::bindTimeout()
{
    ui->statusLabel->setText("failed: timeout");
    ui->srcDropTarget->setEnabled(true);
    ui->dstDropTarget->setEnabled(true);
    checkButtons();
}

void zmBindDropbox::mgmtBindRspCallback(quint64 srcAddr, quint8 status, quint8 entries, quint8 startIndex, quint8 listCount, const deCONZ::BindingTable &table)
{
    if (status == deCONZ::ZdpSuccess)
    {
        if (startIndex == 0)
        {
            m_bindingCache.remove(srcAddr);
        }

        updateBindingTableView(srcAddr, table);
    }

    if (m_selectedNodeAddr == 0 || srcAddr != m_selectedNodeAddr)
    {
        return;
    }

    QString statusText;

    if (status == deCONZ::ZdpSuccess)
    {
        statusText = tr("success");
    }
    else if (status == deCONZ::ZdpNotSupported)
    {
        statusText = tr("failed: not supported");
    }
    else
    {
        statusText = tr("failed: 0x%1").arg(status, 2, 16, QLatin1Char('0')).toUpper();
    }

    if (m_bindingTableInfo)
    {
        m_bindingTableInfo->setText(tr("Binding table from %1 | status: %2 | entries: %3 | start: %4 | count: %5")
        .arg(formatAddress64(srcAddr), statusText, QString::number(entries), QString::number(startIndex), QString::number(listCount)));
    }
}

void zmBindDropbox::updateBindingTableView(quint64 srcAddr, const deCONZ::BindingTable &table)
{
    auto &cache = m_bindingCache[srcAddr];

    // Add new entries to the node cache (duplicates are ignored by QSet).
    for (auto i = table.cbegin(); i != table.cend(); ++i)
    {
        const auto &binding = *i;
        BindingEntry entry;
        entry.srcAddr = srcAddr;
        entry.srcEndpoint = binding.srcEndpoint();
        entry.clusterId = binding.clusterId();
        entry.dstAddrMode = static_cast<quint8>(binding.dstAddressMode());
        entry.dstExtAddr = binding.dstAddress().ext();
        entry.dstGroupAddr = binding.dstAddress().group();
        entry.dstEndpoint = binding.dstEndpoint();
        cache.insert(entry);
    }

    if (srcAddr == m_selectedNodeAddr)
    {
        rebuildBindingTableView();
    }
}

void zmBindDropbox::rebuildBindingTableView()
{
    if (!m_bindingTableView)
    {
        return;
    }

    const auto cacheIt = m_bindingCache.constFind(m_selectedNodeAddr);
    const int cacheSize = (cacheIt != m_bindingCache.cend()) ? cacheIt.value().size() : 0;
    m_bindingTableView->setRowCount(cacheSize);

    if (cacheIt == m_bindingCache.cend())
    {
        return;
    }

    int row = 0;
    for (const auto &entry : cacheIt.value())
    {
        const QString src = formatAddress64(entry.srcAddr);
        const QString srcEp = formatHex8(entry.srcEndpoint);
        const QString cluster = formatHex16(entry.clusterId);
        QString addrMode = tr("Unknown (%1)").arg(formatHex8(entry.dstAddrMode));
        QString dstAddr;
        QString dstEp = QLatin1String("-");

        if (entry.dstAddrMode == deCONZ::ApsExtAddress)
        {
            addrMode = tr("IEEE");
            dstAddr = formatAddress64(entry.dstExtAddr);
            dstEp = formatHex8(entry.dstEndpoint);
        }
        else if (entry.dstAddrMode == deCONZ::ApsGroupAddress)
        {
            addrMode = tr("Group");
            dstAddr = formatHex16(entry.dstGroupAddr);
        }

        const QString clName = clusterName(entry.srcAddr, entry.srcEndpoint, entry.clusterId);

        m_bindingTableView->setItem(row, 0, new QTableWidgetItem(src));
        m_bindingTableView->setItem(row, 1, new QTableWidgetItem(srcEp));
        m_bindingTableView->setItem(row, 2, new QTableWidgetItem(cluster));
        m_bindingTableView->setItem(row, 3, new QTableWidgetItem(addrMode));
        m_bindingTableView->setItem(row, 4, new QTableWidgetItem(dstAddr));
        m_bindingTableView->setItem(row, 5, new QTableWidgetItem(dstEp));
        m_bindingTableView->setItem(row, 6, new QTableWidgetItem(clName));

        row++;
    }
}

QString zmBindDropbox::formatAddress64(quint64 value) const
{
    return QString(QLatin1String("0x%1")).arg(value, 16, 16, QLatin1Char('0')).toUpper();
}

QString zmBindDropbox::formatHex16(quint16 value) const
{
    return QString(QLatin1String("0x%1")).arg(value, 4, 16, QLatin1Char('0')).toUpper();
}

QString zmBindDropbox::formatHex8(quint8 value) const
{
    return QString(QLatin1String("0x%1")).arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

QString zmBindDropbox::clusterName(quint64 srcAddr, quint8 srcEndpoint, quint16 clusterId) const
{
    const NodeInfo node = deCONZ::controller()->nodeWithMac(srcAddr);

    if (!node.data)
    {
        return tr("Unknown");
    }

    const auto *sd = node.data->getSimpleDescriptor(srcEndpoint);
    if (!sd)
    {
        return tr("Unknown");
    }

    for (const auto &cl : sd->inClusters())
    {
        if (cl.id() == clusterId)
        {
            return cl.name();
        }
    }

    for (const auto &cl : sd->outClusters())
    {
        if (cl.id() == clusterId)
        {
            return cl.name();
        }
    }

    return tr("Unknown");
}

void zmBindDropbox::dstRadioButtonClicked(QAbstractButton *button)
{
    Q_UNUSED(button);

    if (ui->ieeeRadioButton->isChecked())
    {
        ui->dstExtAddress->setEnabled(true);
        ui->dstEndpoint->setEnabled(true);

        ui->groupAddressLineEdit->setEnabled(false);
    }
    else if (ui->groupRadioButton->isChecked())
    {
        ui->dstExtAddress->setEnabled(false);
        ui->dstEndpoint->setEnabled(false);

        ui->groupAddressLineEdit->setEnabled(true);
    }
}

bool zmBindDropbox::hasDstData()
{
    if (ui->ieeeRadioButton->isChecked())
    {
        if (!ui->dstExtAddress->text().isEmpty())
        {
            if (!ui->dstEndpoint->text().isEmpty())
            {
                return true;
            }
        }
    }
    else if (ui->groupRadioButton->isChecked())
    {
        bool ok;

        if (!ui->groupAddressLineEdit->text().isEmpty())
        {
            quint16 addr = ui->groupAddressLineEdit->text().toUShort(&ok, 0);
            if (ok)
            {
                m_dstGroupAddr = addr;
                return true;
            }
        }
    }

    return false;
}

void zmBindDropbox::checkButtons()
{
    if (m_hasSrcData && hasDstData())
    {
        ui->bindButton->setEnabled(true);
        ui->unbindButton->setEnabled(true);
    }
    else
    {
        ui->bindButton->setEnabled(false);
        ui->unbindButton->setEnabled(false);
    }
}

void zmBindDropbox::dstGroupTextChanged(const QString &text)
{
    Q_UNUSED(text);
    checkButtons();
}

void zmBindDropbox::setSelectedNode(quint64 nodeAddr)
{
    if (m_selectedNodeAddr != nodeAddr)
    {
        m_selectedNodeAddr = nodeAddr;

        if (nodeAddr != 0)
        {
            const NodeInfo node = deCONZ::controller()->nodeWithMac(nodeAddr);

            if (node.data)
            {
                updateBindingTableView(nodeAddr, node.data->bindingTable());
            }
        }

        rebuildBindingTableView();

        if (m_bindingTableInfo)
        {
            if (nodeAddr != 0)
            {
                if (m_bindingCache.value(nodeAddr).isEmpty())
                {
                    m_bindingTableInfo->setText(tr("Binding table: waiting for data from %1").arg(formatAddress64(nodeAddr)));
                }
                else
                {
                    m_bindingTableInfo->setText(tr("Binding table: cached data for %1").arg(formatAddress64(nodeAddr)));
                }
            }
            else
            {
                m_bindingTableInfo->setText(tr("Binding table: no node selected"));
            }
        }
    }
}
