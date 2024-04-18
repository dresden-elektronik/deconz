/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_COMMAND_INFO_H
#define ZM_COMMAND_INFO_H

#include <QGroupBox>

#include "deconz/zcl.h"

namespace Ui {
    class zmCommandInfo;
}

namespace deCONZ
{
class ApsDataIndication;
}

class QPushButton;
class QLabel;
class QVBoxLayout;
class QSignalMapper;

class zmCommandInfo : public QWidget
{
    Q_OBJECT

public:
    explicit zmCommandInfo(QWidget *parent = 0);
    ~zmCommandInfo();
    void setCluster(quint16 profileId, const deCONZ::ZclCluster &cluster, deCONZ::ZclClusterSide side);

public Q_SLOTS:
    void onExec(int commandId);
    void zclCommandResponse(const deCONZ::ApsDataIndication &ind, const deCONZ::ZclFrame &zclFrame);
    void zclCommandTimeout();
    void zclCommandRequestError();
    void zclAllRequestsConfirmed();

Q_SIGNALS:
    void zclCommandRequest(const deCONZ::ZclCluster &cluster, deCONZ::ZclClusterSide side, const deCONZ::ZclCommand &command);

private:
    void setCommandState(int commandId, deCONZ::CommonState state, const  QString &info, const deCONZ::ZclFrame *response = 0);

    struct CommandDescriptor
    {
        CommandDescriptor() :
            widget(0),
            execButton(0),
            statusLabel(0),
            responseWidget(0)
        {
        }
        quint16 profileId;
        quint16 clusterId;
        deCONZ::ZclClusterSide side;
        deCONZ::ZclCommand command;
        QWidget *widget;
        QPushButton *execButton;
        QLabel *statusLabel;
        /*!
            Each parameter with a user input field has a widget like
            QLineEdit or QComboBox.
         */
        std::vector<std::vector<QWidget*> > parameterAttributes;

        // response stuff (if there is any)
        deCONZ::ZclCommand responseCommand;
        QWidget *responseWidget;
        std::vector<std::vector<QWidget*> > responseParameterAttributes;
    };

    void createCommandWidget(CommandDescriptor &descriptor, bool response);
    void showCommandParameters(CommandDescriptor &descriptor, bool response);
    bool getParameter(deCONZ::ZclAttribute &attr, std::vector<QWidget *> &widgets);
    bool setParameter(deCONZ::ZclAttribute &attr, std::vector<QWidget*> &widgets, bool response);
    void clearData(CommandDescriptor &descriptor);
    void updateDescriptor();

    typedef QList<CommandDescriptor>::iterator CommandCacheIterator;
    Ui::zmCommandInfo *ui;
    QTimer *m_timer;
    int m_commandTimeout;
    int m_commandId; //!< of the command currently running
    QVBoxLayout *m_vbox;
    quint16 m_profileId = 0xffff;
    deCONZ::ZclClusterSide m_side;
    deCONZ::ZclCluster m_cluster;
    deCONZ::ZclCluster m_clusterOpposite;
    QList<CommandDescriptor> m_cache;
    QSignalMapper *m_execMapper;
};

#endif // ZM_COMMAND_INFO_H
