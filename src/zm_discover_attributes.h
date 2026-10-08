/*
 * Copyright (c) 2013-2026 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_DISCOVER_ATTRIBUTES_H
#define ZM_DISCOVER_ATTRIBUTES_H

#include <QWidget>
#include <vector>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class zmDiscoverAttributes : public QWidget
{
    Q_OBJECT

public:
    explicit zmDiscoverAttributes(QWidget *parent = nullptr);

public Q_SLOTS:
    void setTargetNode(quint64 extAddress);

private Q_SLOTS:
    void startDiscovery();
    void copyXmlToClipboard();

    void onDiscoverStarted(quint64 extAddress, quint8 endpoint, quint16 clusterId,
                           quint16 startAttribute, quint16 endAttribute,
                           bool manufacturerSpecific, quint16 manufacturerCode);
    void onAttributeDiscovered(quint64 extAddress, quint8 endpoint, quint16 clusterId,
                               quint16 attributeId, quint8 dataType, quint16 manufacturerCode);
    void onDiscoverFinished(quint64 extAddress, quint8 endpoint, quint16 clusterId,
                            bool success, const QString &reason);

private:
    struct DiscoveredAttribute
    {
        quint16 id = 0;
        quint8 dataType = 0;
        quint16 manufacturerCode = 0;
    };

    static bool parseU16(const QString &text, quint16 *value);
    static QString formatHex8(quint8 value);
    static QString formatHex16(quint16 value);
    static QString formatHex64(quint64 value);

    void setStatus(const QString &status);
    void resetDiscoveryData();
    void appendLogLine(const QString &line);
    void updateXmlPreview();

    bool m_discoveryActive = false;
    quint64 m_targetExtAddress = 0;
    quint8 m_activeEndpoint = 0;
    quint16 m_activeClusterId = 0;
    quint16 m_activeStartAttribute = 0;
    quint16 m_activeEndAttribute = 0;
    bool m_activeManufacturerSpecific = false;
    quint16 m_activeManufacturerCode = 0;

    QLabel *m_targetNodeValue = nullptr;
    QLineEdit *m_endpointLineEdit = nullptr;
    QLineEdit *m_clusterLineEdit = nullptr;
    QLineEdit *m_manufacturerLineEdit = nullptr;
    QLineEdit *m_startLineEdit = nullptr;
    QLineEdit *m_endLineEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPlainTextEdit *m_discoveredText = nullptr;
    QPlainTextEdit *m_xmlText = nullptr;
    QPushButton *m_startButton = nullptr;

    std::vector<DiscoveredAttribute> m_discoveredAttributes;
};

#endif // ZM_DISCOVER_ATTRIBUTES_H
