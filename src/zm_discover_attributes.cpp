/*
 * Copyright (c) 2013-2026 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <algorithm>

#include <QApplication>
#include <QClipboard>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "deconz/dbg_trace.h"
#include "deconz/zcl.h"
#include "zcl_private.h"
#include "zm_cluster_info.h"
#include "zm_controller.h"
#include "zm_discover_attributes.h"

namespace
{
const char *InvalidSelectionText = "No node selected";
}

zmDiscoverAttributes::zmDiscoverAttributes(QWidget *parent) :
    QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);

    auto *targetGroup = new QGroupBox(tr("Target"), this);
    auto *targetLayout = new QFormLayout(targetGroup);

    m_targetNodeValue = new QLabel(InvalidSelectionText, targetGroup);

    m_endpointLineEdit = new QLineEdit(targetGroup);
    m_endpointLineEdit->setPlaceholderText("1");
    m_endpointLineEdit->setText("1");

    m_clusterLineEdit = new QLineEdit(targetGroup);
    m_clusterLineEdit->setPlaceholderText("0x0000");

    m_manufacturerLineEdit = new QLineEdit(targetGroup);
    m_manufacturerLineEdit->setPlaceholderText("0x0000");
    m_manufacturerLineEdit->setText("0x0000");

    m_startLineEdit = new QLineEdit(targetGroup);
    m_startLineEdit->setPlaceholderText("0x0000");
    m_startLineEdit->setText("0x0000");

    m_endLineEdit = new QLineEdit(targetGroup);
    m_endLineEdit->setPlaceholderText("0xFFFF");
    m_endLineEdit->setText("0xFFFF");

    targetLayout->addRow(tr("Node"), m_targetNodeValue);
    targetLayout->addRow(tr("Endpoint"), m_endpointLineEdit);
    targetLayout->addRow(tr("Cluster ID"), m_clusterLineEdit);
    targetLayout->addRow(tr("Manufacturer Code"), m_manufacturerLineEdit);
    targetLayout->addRow(tr("Start Attribute"), m_startLineEdit);
    targetLayout->addRow(tr("End Attribute"), m_endLineEdit);

    mainLayout->addWidget(targetGroup);

    auto *buttonRow = new QHBoxLayout();
    m_startButton = new QPushButton(tr("Discover Attributes"), this);
    auto *copyXmlButton = new QPushButton(tr("Copy XML"), this);

    buttonRow->addWidget(m_startButton);
    buttonRow->addWidget(copyXmlButton);
    mainLayout->addLayout(buttonRow);

    mainLayout->addSpacing(12);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    auto *discoveredGroup = new QGroupBox(tr("Discovered Attributes"), this);
    auto *discoveredLayout = new QVBoxLayout(discoveredGroup);

    m_discoveredText = new QPlainTextEdit(discoveredGroup);
    m_discoveredText->setReadOnly(true);
    discoveredLayout->addWidget(m_discoveredText);

    mainLayout->addWidget(discoveredGroup, 1);

    auto *xmlGroup = new QGroupBox(tr("XML Additions (general.xml)"), this);
    auto *xmlLayout = new QVBoxLayout(xmlGroup);

    m_xmlText = new QPlainTextEdit(xmlGroup);
    m_xmlText->setReadOnly(true);
    xmlLayout->addWidget(m_xmlText);

    mainLayout->addWidget(xmlGroup, 2);

    connect(m_startButton, &QPushButton::clicked,
            this, &zmDiscoverAttributes::startDiscovery);
    connect(copyXmlButton, &QPushButton::clicked,
            this, &zmDiscoverAttributes::copyXmlToClipboard);

    connect(deCONZ::controller(), &zmController::discoverAttributesStarted,
            this, &zmDiscoverAttributes::onDiscoverStarted);
    connect(deCONZ::controller(), &zmController::discoverAttributesAttributeDiscovered,
            this, &zmDiscoverAttributes::onAttributeDiscovered);
    connect(deCONZ::controller(), &zmController::discoverAttributesFinished,
            this, &zmDiscoverAttributes::onDiscoverFinished);

    m_startButton->setEnabled(false);
    setStatus(tr("Select exactly one node to enable discovery."));

    updateXmlPreview();
}

void zmDiscoverAttributes::setTargetNode(quint64 extAddress)
{
    m_targetExtAddress = extAddress;

    if (extAddress == 0)
    {
        m_targetNodeValue->setText(InvalidSelectionText);
        m_startButton->setEnabled(false);
        setStatus(tr("Select exactly one node to enable discovery."));
        return;
    }

    m_targetNodeValue->setText(formatHex64(extAddress));
    m_startButton->setEnabled(true);
}

void zmDiscoverAttributes::startDiscovery()
{
    quint16 clusterId = 0;
    quint16 startAttribute = 0;
    quint16 endAttribute = 0;

    if (m_targetExtAddress == 0)
    {
        setStatus(tr("Select exactly one node before starting discovery."));
        return;
    }

    if (!parseU16(m_clusterLineEdit->text(), &clusterId))
    {
        setStatus(tr("Invalid Cluster ID. Use decimal or 0x-prefixed hex."));
        return;
    }

    // Parse endpoint as a uint8 value
    quint16 endpoint = 0;
    if (!parseU16(m_endpointLineEdit->text(), &endpoint) || endpoint > 255)
    {
        setStatus(tr("Invalid Endpoint. Use decimal value between 1 and 255."));
        return;
    }

    if (!parseU16(m_startLineEdit->text(), &startAttribute))
    {
        setStatus(tr("Invalid Start Attribute. Use decimal or 0x-prefixed hex."));
        return;
    }

    if (!parseU16(m_endLineEdit->text(), &endAttribute))
    {
        setStatus(tr("Invalid End Attribute. Use decimal or 0x-prefixed hex."));
        return;
    }

    if (endAttribute < startAttribute)
    {
        setStatus(tr("End Attribute must be greater than or equal to Start Attribute."));
        return;
    }

    const QString manufacturerCodeText = m_manufacturerLineEdit->text().trimmed();
    quint16 manufacturerCode = 0;
    bool manufacturerSpecific = false;

    if (!parseU16(manufacturerCodeText, &manufacturerCode))
    {
        setStatus(tr("Invalid Manufacturer Code. Use decimal or 0x-prefixed hex."));
        return;
    }

    manufacturerSpecific = (manufacturerCode != 0);

    resetDiscoveryData();

    const bool ok = deCONZ::controller()->startDiscoverAttributesRange(
        m_targetExtAddress,
        static_cast<quint8>(endpoint),
        clusterId,
        startAttribute,
        endAttribute,
        manufacturerSpecific,
        manufacturerCode);

    if (!ok)
    {
        setStatus(tr("Failed to start discovery request."));
    }
}

void zmDiscoverAttributes::copyXmlToClipboard()
{
    QApplication::clipboard()->setText(m_xmlText->toPlainText());
    setStatus(tr("XML copied to clipboard."));
}

void zmDiscoverAttributes::onDiscoverStarted(quint64 extAddress, quint8 endpoint, quint16 clusterId,
                                             quint16 startAttribute, quint16 endAttribute,
                                             bool manufacturerSpecific, quint16 manufacturerCode)
{
    if (extAddress != m_targetExtAddress)
    {
        return;
    }

    m_discoveryActive = true;
    m_activeEndpoint = endpoint;
    m_activeClusterId = clusterId;
    m_activeStartAttribute = startAttribute;
    m_activeEndAttribute = endAttribute;
    m_activeManufacturerSpecific = manufacturerSpecific;
    m_activeManufacturerCode = manufacturerCode;

    setStatus(tr("Discovery started for cluster %1, endpoint %2.")
              .arg(formatHex16(clusterId))
              .arg(endpoint));
}

void zmDiscoverAttributes::onAttributeDiscovered(quint64 extAddress, quint8 endpoint, quint16 clusterId,
                                                  quint16 attributeId, quint8 dataType, quint16 manufacturerCode)
{
    if (!m_discoveryActive ||
        extAddress != m_targetExtAddress ||
        endpoint != m_activeEndpoint ||
        clusterId != m_activeClusterId)
    {
        return;
    }

    auto alreadyKnown = std::find_if(m_discoveredAttributes.cbegin(), m_discoveredAttributes.cend(),
                                     [attributeId, manufacturerCode](const DiscoveredAttribute &item) {
        return item.id == attributeId && item.manufacturerCode == manufacturerCode;
    });

    if (alreadyKnown != m_discoveredAttributes.cend())
    {
        return;
    }

    DiscoveredAttribute item;
    item.id = attributeId;
    item.dataType = dataType;
    item.manufacturerCode = manufacturerCode;

    m_discoveredAttributes.push_back(item);

    appendLogLine(QString("attribute=%1 type=%2 mfcode=%3")
                  .arg(formatHex16(attributeId))
                  .arg(formatHex8(dataType))
                  .arg(formatHex16(manufacturerCode)));

    updateXmlPreview();
}

void zmDiscoverAttributes::onDiscoverFinished(quint64 extAddress, quint8 endpoint, quint16 clusterId,
                                               bool success, const QString &reason)
{
    if (extAddress != m_targetExtAddress ||
        endpoint != m_activeEndpoint ||
        clusterId != m_activeClusterId)
    {
        return;
    }

    m_discoveryActive = false;

    if (success)
    {
        setStatus(tr("Discovery finished. %1 attributes in selected range.")
                  .arg(static_cast<int>(m_discoveredAttributes.size())));
    }
    else
    {
        setStatus(tr("Discovery failed: %1").arg(reason));
    }
}

bool zmDiscoverAttributes::parseU16(const QString &text, quint16 *value)
{
    if (!value)
    {
        return false;
    }

    const QString t = text.trimmed();
    if (t.isEmpty())
    {
        return false;
    }

    bool ok = false;
    uint parsed = t.toUInt(&ok, 0);

    if (!ok || parsed > 0xFFFF)
    {
        return false;
    }

    *value = static_cast<quint16>(parsed);
    return true;
}

QString zmDiscoverAttributes::formatHex8(quint8 value)
{
    return QString("0x%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

QString zmDiscoverAttributes::formatHex16(quint16 value)
{
    return QString("0x%1").arg(value, 4, 16, QLatin1Char('0')).toUpper();
}

QString zmDiscoverAttributes::formatHex64(quint64 value)
{
    return QString("0x%1").arg(value, 16, 16, QLatin1Char('0')).toUpper();
}

void zmDiscoverAttributes::setStatus(const QString &status)
{
    m_statusLabel->setText(status);
}

void zmDiscoverAttributes::resetDiscoveryData()
{
    m_discoveryActive = false;
    m_discoveredAttributes.clear();
    m_discoveredText->clear();
    updateXmlPreview();
}

void zmDiscoverAttributes::appendLogLine(const QString &line)
{
    m_discoveredText->appendPlainText(line);
}

void zmDiscoverAttributes::updateXmlPreview()
{
    quint16 clusterId = 0;

    if (!parseU16(m_clusterLineEdit->text(), &clusterId))
    {
        m_xmlText->clear();
        return;
    }

    // Leave XML field empty if no attributes discovered
    if (m_discoveredAttributes.empty())
    {
        m_xmlText->clear();
        return;
    }

    std::sort(m_discoveredAttributes.begin(), m_discoveredAttributes.end(),
              [](const DiscoveredAttribute &a, const DiscoveredAttribute &b) {
        if (a.id != b.id)
        {
            return a.id < b.id;
        }

        return a.manufacturerCode < b.manufacturerCode;
    });

    QString xml;
    xml += QString("<cluster id=\"%1\" name=\"\" description=\"\">\n").arg(formatHex16(clusterId));

    for (const auto &item : m_discoveredAttributes)
    {
        QString typeShortName;
        const deCONZ::ZclDataType type = deCONZ::zclDataBase()->dataType(item.dataType);
        if (type.id() == item.dataType)
        {
            typeShortName = type.shortname().trimmed();
            if (typeShortName.compare("unk", Qt::CaseInsensitive) == 0 ||
                typeShortName.compare("unknown", Qt::CaseInsensitive) == 0)
            {
                typeShortName.clear();
            }
        }

        xml += QString("  <attribute id=\"%1\" name=\"Unknown\" type=\"%2\" access=\"rw\" required=\"o\"")
               .arg(formatHex16(item.id))
               .arg(typeShortName);

        if (item.manufacturerCode != 0)
        {
            xml += QString(" mfcode=\"%1\"").arg(formatHex16(item.manufacturerCode));
        }

        xml += "></attribute>\n";
    }

    xml += "</cluster>\n";

    m_xmlText->setPlainText(xml);
}
