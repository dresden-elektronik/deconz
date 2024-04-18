/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_CLUSTER_INFO_H
#define ZM_CLUSTER_INFO_H

#include <QWidget>

#include "deconz/aps.h"
#include "deconz/zcl.h"

namespace Ui {
    class zmClusterInfo;
}

class zmAttributeInfo;
class zmClusterInfo;
class QSignalMapper;

namespace deCONZ {
    zmClusterInfo *clusterInfo();
    class ZclCluster;
    class ZclDataBase;
}

namespace deCONZ
{
class ApsDataConfirm;
class ApsDataIndication;
}

class zmNode;
class QButtonGroup;
class QStandardItemModel;
class QModelIndex;

class zmClusterInfo : public QWidget
{
    Q_OBJECT

public:
    explicit zmClusterInfo(QWidget *parent = 0);
    ~zmClusterInfo();
    void setEndpoint(deCONZ::zmNode *node, int endpoint);
    void showCluster(quint16 id, deCONZ::ZclClusterSide clusterSide);
    void zclCommandResponse(const deCONZ::ApsDataIndication &ind, const deCONZ::ZclFrame &zclFrame);
    void zclDiscoverAttributesRequest(quint16 startIndex);
    void clear();
    void refresh();
    void refreshNodeCommands(deCONZ::zmNode *node, deCONZ::ZclCluster *cluster);
    void refreshNodeAttributes(deCONZ::zmNode *node, quint8 endpoint, deCONZ::ZclCluster *cluster);
    quint16 clusterId() const { return m_clusterId; }
    quint8 endpoint() const { return m_endpoint; }
    deCONZ::ZclClusterSide clusterSide() const { return m_clusterSide; }
    deCONZ::ZclCluster *getCluster();
    bool eventFilter(QObject *object, QEvent *event);
    void scrollToAttributes();

public Q_SLOTS:
    bool zclCommandRequest(const deCONZ::ZclCluster &cluster, deCONZ::ZclClusterSide side, const deCONZ::ZclCommand &command);
    void apsDataConfirm(const deCONZ::ApsDataConfirm &conf);

private Q_SLOTS:
    void readAttributesButtonClicked();
    void discoverAttributesButtonClicked();
    void attributeDoubleClicked(const QModelIndex &index);
    void zclWriteReportConfiguration(const deCONZ::ZclAttribute &attribute, quint8 direction);
    void zclReadReportConfiguration(const deCONZ::ZclAttribute &attribute);
    void zclWriteAttribute(const deCONZ::ZclAttribute &attribute);
    void zclReadAttribute(const deCONZ::ZclAttribute &attribute);
    void attributeDialogeFinished(int result);
    void showAttributes();
    void proceedReadAttributes();
    bool dragSelectedAttribute();

private:
    enum Constants {
        InvalidEndpoint = 255,
        InvalidClusterId = 0x1FFFF
    };

    Ui::zmClusterInfo *ui;
    deCONZ::zmNode *m_node = nullptr;
    deCONZ::ZclClusterSide m_clusterSide;
    quint8 m_endpoint = InvalidEndpoint;
    quint32 m_clusterId = InvalidClusterId;
    QStandardItemModel *m_attrModel = nullptr;
    QSignalMapper *m_mapperServer = nullptr;
    QSignalMapper *m_mapperClient = nullptr;
    zmAttributeInfo *m_attributeDialog = nullptr;
    std::vector<uint8_t> m_apsReqIds;
    uint8_t m_zclReadAttributeReqId = 0;
    deCONZ::SteadyTimeRef m_readAttrTimeRef{};
    uint m_attrIndex = 0;
    bool m_init;
    QPoint m_startDragPos;
};

#endif // ZM_CLUSTER_INFO_H
