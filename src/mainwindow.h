/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QElapsedTimer>

#include "deconz/types.h"
#include "deconz/aps_controller.h"
#include "deconz/device_enumerator.h"

namespace Ui {
    class MainWindow;
}

namespace deCONZ {
    class Node;
    class NodeInterface;
    class ZclDataBase;
    class DeviceEnumerator;
}

class ActorVfsModel;
class DebugView;
class zmBindDropbox;
class zmController;
class zmMaster;
class zmNetEvent;
class zmNetSetup;
class zmNode;
class zmNodeInfo;
class zmNeighbor;
class QAction;
class QStandardItemModel;
struct QextPortInfo;
class QLabel;
class QPushButton;
class QTableView;
class NodeInterface;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();
    void notifyUser(const QString &text);
    void setDeviceState(deCONZ::State state);
    void loadPlugIns();

public Q_SLOTS:
    void setAutoFetching();
    void initAutoConnectManager();

private Q_SLOTS:
    void loadPluginsStage2();
    void onNetStartFailed(uint8_t zdoStatus);
    void onControllerEvent(const zmNetEvent &event);
    void onDeviceConnected();
    void onDeviceDisconnected(int reason);
    void onDeviceState();
    void onDeviceActivity();
    void onDeviceStateTimeout();
    void onSelectionChanged();
    void onNodeEvent(const deCONZ::NodeEvent&event);
    void getComPorts();
    void devConnectClicked();
    void devDisconnectClicked();
    void devUpdateClicked();
    void showUserManual();
    void showAboutDialog();
    void showActorView();
    void showPreferencesDialog();
    void showDevicePage();
    void showNetworkSettings();
    void showSendToDialog();
    void showNodeViewPage();
    void setNodesOnline();
    void resetNodesActionTriggered();
    void readNodeDescriptorActionTriggered();
    void readActiveEndpointsActionTriggered();
    void readSimpleDescriptorsActionTriggered();
    void editDDFActionTriggered();
    void deleteNodesActionTriggered();
    void addSourceRouteActionTriggered();
    void removeSourceRouteActionTriggered();
    void updateNetworkControls();
    void appAboutToQuit();
    void openWebApp();
    void openPhosconApp();

protected:
    void timerEvent(QTimerEvent *event);
    void createMainToolbar();
    void createFileMenu();
    void createEditMenu();
    void createHelpMenu();
    void createFetchMenu(bool enableRFD, bool enableFFD);

private:
    enum State {
        StateInit,
        StateIdle,
        StateConnecting,
        StateConnected,
        StateFirmwareNeedUpdate,
        StateFirmwareUpdateRunning
    };

    struct FtdiDevice
    {
        QString manufacturer;
        QString description;
        int vendor;
        int product;
        QByteArray serial;
    };
    void setState(State state, int line);

    State m_state;

    // main toolbar items
    QAction *m_actionDeviceDisconnect;
    QAction *m_leaveAction;
    QAction *m_joinAction;
    QLabel *m_netStateLabel;
    QAction *m_netConfigAction;
    QPushButton *m_autoPushButton;
    QPushButton *m_openPhosconAppButton;
    QAction *m_sendToAction;
    QLabel *m_nodesOnlineLabel;
    QList<QAction*> m_showPanelActions; //!< toggle actions for show panels if connected

    size_t m_autoConnIdx; // iterate over devices one per auto connect attemp
    deCONZ::DeviceEntry m_devEntry; // current connected device
    deCONZ::State m_connState;
    int m_fetchTimer;
    int m_waitReconnectCount;
    int m_connTimeout;
    QElapsedTimer m_firmwareUpdateTime;
    QString m_reconnectDevPath;
    bool m_reconnectAfterFirmwareUpdate;
    deCONZ::ZclDataBase *m_zclDataBase;
    zmMaster *m_master;
    QString m_remoteIP;
    int m_remotePort;
    ActorVfsModel *m_vfsModel;
    zmController *m_controller;
    Ui::MainWindow *ui;
    QDockWidget *m_dockNodeInfo;
    zmNodeInfo *m_nodeInfo;
    QTableView *m_nodeTableView;
    zmBindDropbox *m_bindDropbox;
    bool m_devUpdateCanditate;
    QObject *m_restPlugin = nullptr;
    std::vector<deCONZ::NodeInterface*> m_plugins;
    std::vector<deCONZ::DeviceEntry> m_devs;
    deCONZ::DeviceEnumerator *m_devEnum;
    QMenu *m_menuPanels;
    QMenu *m_menuPlugins;
    QMenu *m_editMenu = nullptr;
    DebugView *m_debugView = nullptr;
};

#endif // MAINWINDOW_H
