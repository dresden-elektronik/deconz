/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_ATTRIBUTE_INFO_H
#define ZM_ATTRIBUTE_INFO_H

#include <QDialog>

#include "deconz/zcl.h"

namespace Ui {
    class zmAttributeInfo;
}

class QTimer;

class zmAttributeInfo : public QDialog
{
    Q_OBJECT

public:
    enum AttributeInfoState
    {
        Idle,
        ReadData,
        WriteData,
        ReadConfig,
        WriteConfig,
        Timeout
    };

    explicit zmAttributeInfo(QWidget *parent = 0);
    void setAttribute(quint8 endpoint, quint16 clusterId, deCONZ::ZclClusterSide clusterSide, const deCONZ::ZclAttribute &attr);
    void zclWriteAttributeResponse(bool ok);
    void zclCommandResponse(const deCONZ::ZclFrame &zclFrame);
    ~zmAttributeInfo();

    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);

Q_SIGNALS:
    void zclWriteAttribute(const deCONZ::ZclAttribute &attr);
    void zclReadAttribute(const deCONZ::ZclAttribute &attr);
    void zclReadReportConfiguration(const deCONZ::ZclAttribute &attr);
    void zclWriteReportConfiguration(const deCONZ::ZclAttribute &attr, quint8 direction);

private Q_SLOTS:
    void stateCheck();
    void write();
    void read();
    void writeAttributeResponse(const deCONZ::ZclFrame &zclFrame);
    void readAttributeResponse(const deCONZ::ZclFrame &zclFrame);
    void timeout();
    void failedWithDefaultResponse(const deCONZ::ZclFrame &zclFrame);
    void readReportConfiguration();
    void readReportConfigurationResponse(const deCONZ::ZclFrame &zclFrame);
    void writeReportConfiguration();
    void writeReportConfigurationResponse(const deCONZ::ZclFrame &zclFrame);
    void updateEdit();

private:
    void buildBooleanInput();
    bool getBooleanInput();
    bool setBooleanInput();
    void buildNumericInput();
    bool getNumericInput();
    bool setNumericInput();
    void buildBitmapInput();
    bool getBitmapInput();
    bool setBitmapInput();
    void buildEnumInput();
    bool getEnumInput();
    bool setEnumInput();

    Ui::zmAttributeInfo *ui;
    AttributeInfoState m_state;
    bool m_signed;
    QList<QWidget*> m_edits;
    deCONZ::ZclAttribute m_attribute;
    QVariant m_origValue;
    QTimer *m_timer;
    QPoint m_startDragPos;
    quint8 m_endpoint;
    quint16 m_clusterId;
    deCONZ::ZclClusterSide m_clusterSide;
};

#endif // ZM_ATTRIBUTE_INFO_H
