/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QDebug>
#include <QDateTime>
#include <QInputDialog>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QHeaderView>
#include <QMetaProperty>
#include <QDockWidget>
#include <QSettings>
#include <QHostAddress>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QTableView>
#include <QStandardItemModel>
#include <QStyleFactory>
#include <QToolButton>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QUrl>
#include <QPixmapCache>
#include <QPluginLoader>
#include <QScreen>
#include <QSortFilterProxyModel>
#include <QWindow>
#include <cerrno>
#ifdef USE_ACTOR_MODEL
#include <actor/plugin_loader.h>
#include "actor/service.h"
#endif
#include "gui/actor_vfs_view.h"
#include "gui/theme.h"
#include "actor_vfs_model.h"
#include "mainwindow.h"
#include "source_route_info.h"
#include "zm_app.h"
#include "zm_global.h"
#include "ui_mainwindow.h"
#include "zm_about_dialog.h"
#include "zm_binddropbox.h"
#include "zm_node_info.h"
#include "common/protocol.h"
#include "send_to_dialog.h"
#include "zm_gnode.h"
#include "zm_controller.h"
#include "zm_cluster_info.h"
#include "zm_master.h"
#include "zm_netedit.h"
#include "zm_node.h"
#include "zm_node_model.h"
#include "zm_netdescriptor_model.h"
#include "zm_settings_dialog.h"
#include "debug_view.h"
#include "deconz/dbg_trace.h"
#include "deconz/http_client_handler.h"
#include "deconz/util.h"
#include "deconz/util_private.h"
#include "deconz/u_assert.h"
#include "deconz/node_event.h"
#include "deconz/node_interface.h"
#include "zcl_private.h"

#define APP_USER_MANUAL_PDF "deCONZ-BHB-en.pdf"
#define FW_UPDATE_TIME_MS 75000
#define FW_UPDATE_TIME_BACKOFF_MS 2000

namespace
{
    enum DestinationMode
    {
        ShortAddressDestination           = 0,
        BroadcastAllDestination           = 1,
        BroadcastRxOnWhenIdleDestination  = 2,
        BroadcastRoutersDestination       = 3,
        MultipleDestination               = 4
    };
}


QAction *readBindingTableAction = nullptr;
QAction *readNodeDescriptorAction = nullptr;
QAction *readActiveEndpointsAction = nullptr;
QAction *readSimpleDescriptorsAction = nullptr;
QAction *deleteNodeAction = nullptr;
QAction *resetNodeAction = nullptr;
QAction *addSourceRouteAction = nullptr;
QAction *removeSourceRouteAction = nullptr;
QAction *editDDFAction = nullptr;

static const int MainTickMs = 1000;
static const int WaitReconnectDuration = 15; // seconds
static const int WaitReconnectDuration2 = 5; // seconds
static const int MaxConnectionTimeout = 12;
static const int MaxConnectionTimeoutBootloaderOnly = 60;

    // provide global access
static SourceRouteInfo *_sourceRouteInfo = nullptr;
static zmClusterInfo *_clusterInfo = nullptr;
static zmNodeInfo *_nodeInfo = nullptr;
static deCONZ::NodeModel *_nodeModel = nullptr;
static zmBindDropbox *_bindDropBox = nullptr;
static zmNetEdit *_netEdit = nullptr;
static SendToDialog *_sendToDialog = nullptr;
static MainWindow *_mainWindow = nullptr;

namespace deCONZ
{

void notifyUser(const QString &text)
{
    if (_mainWindow)
    {
        _mainWindow->notifyUser(text);
    }
}

void notifyHandler(UtilEvent event, void *data)
{
    Q_UNUSED(data);

    switch (event)
    {
    case UE_DestinationAddressChanged:
        if (_sendToDialog)
        {
            _sendToDialog->reloadAddress();
        }
        break;

    default:
        DBG_Printf(DBG_INFO, "notifyHandler() unknown event %d\n", event);
        break;
    }
}

zmClusterInfo *clusterInfo()
{
    Q_ASSERT(_clusterInfo);
    return _clusterInfo;
}

zmNodeInfo *nodeInfo()
{
    Q_ASSERT(_nodeInfo);
    return _nodeInfo;
}

NodeModel *nodeModel()
{
    return _nodeModel;
}

zmBindDropbox *bindDropBox()
{
    Q_ASSERT(_bindDropBox);
    return _bindDropBox;
}

zmNetEdit *netEdit()
{
    return _netEdit;
}

void setDeviceState(State state)
{
    if (_mainWindow)
    {
        _mainWindow->setDeviceState(state);
    }

    deCONZ::controller()->setDeviceState(state);

    if (_netEdit)
    {
        _netEdit->setDeviceState(state);
    }

    deCONZ::nodeModel()->setDeviceState(state);
}

} // namespace deCONZ

am_api_functions *GUI_GetActorModelApi(void)
{
    return AM_ApiFunctions();
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    QString configPath = deCONZ::getStorageLocation(deCONZ::ConfigLocation);
    QSettings config(configPath, QSettings::IniFormat);

    Theme_Init();
    // TODO query from OS?
    // gsettings get org.gnome.desktop.interface color-scheme

    QString theme("light");

    if (config.contains("window/theme"))
    {
        theme = config.value("window/theme", "default").toString();
    }

    if (theme == "dark")
    {
        Theme_Activate("dark");
        QStyle *fusion = QStyleFactory::create("fusion");
        qApp->setStyle(new AStyle("dark", fusion));
        qApp->setPalette(qApp->style()->standardPalette());
    }
    else
    {
        Theme_Activate("light");
        QStyle *fusion = QStyleFactory::create("fusion");
        qApp->setStyle(fusion);

        QPalette pal = qApp->style()->standardPalette();
        int bri = (pal.windowText().color().lightness() + pal.button().color().lightness()) / 2;
        pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(bri, bri, bri));
        pal.setColor(QPalette::Disabled, QPalette::Text, QColor(bri, bri, bri));
        qApp->setPalette(pal);
    }

    ui->setupUi(this);
    ui->stackedView->setCurrentWidget(ui->pageOffline);
    updateLogo();

    m_state = StateInit;
    m_restPlugin = nullptr;
    m_devUpdateCanditate = false;
    m_devEnum = new deCONZ::DeviceEnumerator(this);
    m_autoConnIdx = 0;

    m_vfsModel = new ActorVfsModel(this);

    _nodeModel = new deCONZ::NodeModel(this);

    if (!gHeadlessVersion)
    {
        m_debugView = new DebugView(this);
        m_debugView->hide();
    }

    GUI_InitNodeActor();

    setCentralWidget(ui->stackedView);

    setWindowTitle(qApp->applicationName());
#ifdef Q_OS_OSX
    setWindowIcon(QIcon(":/icons/de_logo.icns"));
#else
    setWindowIcon(QIcon(":/icons/de_logo_48px.png"));
#endif
    auto *versionLabel = new QLabel;
    versionLabel->setText(qApp->applicationVersion());
    statusBar()->addPermanentWidget(versionLabel);

    const QString vers = QString("Version %1\n\nCopyright © %2 dresden elektronik ingenieurtechnik gmbh. All rights reserved.")
    .arg(qApp->applicationVersion()).arg(QDate::currentDate().year());

    ui->page0AppVersionLabel->setText(vers);

    QScrollArea *scrollArea;
    QGraphicsScene *scene = new QGraphicsScene(ui->graphicsView);
    ui->graphicsView->setScene(scene);

    connect(scene, SIGNAL(selectionChanged()),
            this, SLOT(onSelectionChanged()));

    connect(qApp, SIGNAL(aboutToQuit()),
            this, SLOT(appAboutToQuit()));

    protocol_init();

    zmNetDescriptorModel *networkModel = new zmNetDescriptorModel(this);

    m_master = new zmMaster(this);
    m_controller = new zmController(m_master, networkModel, scene, ui->graphicsView, this);

    connect(m_master, SIGNAL(netStateChanged()),
            this, SLOT(updateNetworkControls()));

    m_connTimeout = -1;
    m_waitReconnectCount = 0;
    m_connState = deCONZ::UnknownState;
    m_reconnectAfterFirmwareUpdate = false;

    createMainToolbar();

    _clusterInfo = new zmClusterInfo(this);
    m_nodeInfo = new zmNodeInfo(this);
    _nodeInfo = m_nodeInfo;
    _nodeInfo->hide();

    // network settings
    _netEdit = new zmNetEdit(this);
    _netEdit->hide();
    _netEdit->init();
    _netEdit->setNetDescriptorModel(networkModel);

    // send to dialog
    _sendToDialog = new SendToDialog(this);
    _sendToDialog->hide();

    m_bindDropbox = new zmBindDropbox(this);
    _bindDropBox = m_bindDropbox;
    _bindDropBox->hide();
    m_nodeTableView = new QTableView(this);

    QSortFilterProxyModel *proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(deCONZ::nodeModel());

    m_nodeTableView->setModel(proxyModel);
    m_nodeTableView->setSortingEnabled(true);
    m_nodeTableView->sortByColumn(deCONZ::NodeModel::ModelIdColumn, Qt::AscendingOrder);
    m_nodeTableView->horizontalHeader()->setStretchLastSection(true);
//    deCONZ::NodeDelegate *nodeDelegate = new deCONZ::NodeDelegate(this);
//    m_nodeTableView->setItemDelegate(nodeDelegate);
//    m_nodeTableView->hide();

//    connect(nodeDelegate, SIGNAL(displayNode(zmgNode*)),
//            ui->graphicsView, SLOT(displayNode(zmgNode*)));

    createFileMenu();
    createEditMenu();
    createViewMenu();

    setDockOptions(ForceTabbedDocks | AllowTabbedDocks);

    QMenuBar *menuBar = this->menuBar();

    // panel menu
    m_menuPanels = menuBar->addMenu(tr("Panels"));

    // plugin menu
    m_menuPlugins = menuBar->addMenu(tr("Plugins"));
    m_menuPlugins->setEnabled(false);

    createHelpMenu();

    // Dock NodeInfo
    m_dockNodeInfo = new QDockWidget(tr("Node Info"), this);
    m_dockNodeInfo->setObjectName("NodeInfoDock");
    m_dockNodeInfo->setTitleBarWidget(new QWidget()); // don't show title bar
    m_dockNodeInfo->setWidget(m_nodeInfo);
    m_dockNodeInfo->setStyleSheet("::title { position: relative; padding-left: 7px; }");
    addDockWidget(Qt::LeftDockWidgetArea, m_dockNodeInfo);
    m_menuPanels->addAction(m_dockNodeInfo->toggleViewAction());

    // Dock ClusterInfo
    auto *dockClusterInfo = new QDockWidget(tr("Cluster Info"), this);
    dockClusterInfo->setObjectName("ClusterInfoDock");
    dockClusterInfo->setTitleBarWidget(new QWidget()); // don't show title bar
    scrollArea = new QScrollArea(this);
    //scrollArea->setAutoFillBackground(true);

    _clusterInfo->setAutoFillBackground(true);
    scrollArea->setWidget(deCONZ::clusterInfo());
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setFrameStyle(QFrame::NoFrame | QFrame::Plain);
    // // fix background color due QScrollArea
    // scrollArea->viewport()->setAutoFillBackground(false);
    // scrollArea->widget()->setAutoFillBackground(false);

    dockClusterInfo->setWidget(scrollArea);
    addDockWidget(Qt::LeftDockWidgetArea, dockClusterInfo);
    m_menuPanels->addAction(dockClusterInfo->toggleViewAction());

    // Dock BindDropbox
    auto *dockBinding = new QDockWidget(tr("Bind Dropbox"), this);
    dockBinding->setTitleBarWidget(new QWidget()); // don't show title bar
    dockBinding->setObjectName("BindDropbox");
    dockBinding->setWidget(m_bindDropbox);
    addDockWidget(Qt::LeftDockWidgetArea, dockBinding);
    m_menuPanels->addAction(dockBinding->toggleViewAction());

    // Dock NodeListView
    QDockWidget *dockNodeList;
    dockNodeList = new QDockWidget(tr("Node List"), this);
    dockNodeList->setTitleBarWidget(new QWidget()); // don't show title bar
    dockNodeList->setObjectName("NodeListView");
    dockNodeList->setWidget(m_nodeTableView);
    addDockWidget(Qt::RightDockWidgetArea, dockNodeList);
    m_menuPanels->addAction(dockNodeList->toggleViewAction());
    dockNodeList->hide();

    // Dock Source Routing
#ifdef APP_FEATURE_SOURCE_ROUTING
    _sourceRouteInfo = new SourceRouteInfo(this);
    auto * dockSourceRouting = new QDockWidget(tr("Source Routing"), this);
    dockSourceRouting->setTitleBarWidget(new QWidget()); // don't show title bar
    dockSourceRouting->setObjectName("SourceRoutingDock");
    dockSourceRouting->setWidget(_sourceRouteInfo);
    dockSourceRouting->hide();
    addDockWidget(Qt::LeftDockWidgetArea, dockSourceRouting);
    m_menuPanels->addAction(dockSourceRouting->toggleViewAction());
#endif // APP_FEATURE_SOURCE_ROUTING

    if (config.contains("window/state")) {
        QByteArray arr = config.value("window/state").toByteArray();
        restoreState(arr);
    }
    else
    {
        dockBinding->hide();
    }

    tabifyDockWidget(dockSourceRouting, dockNodeList);
    tabifyDockWidget(dockNodeList, dockBinding);
    tabifyDockWidget(dockBinding, m_dockNodeInfo);
    tabifyDockWidget(m_dockNodeInfo, dockClusterInfo);

    if (config.contains("window/geometry")) {
        QByteArray arr = config.value("window/geometry").toByteArray();
        restoreGeometry(arr);
    }

    if (!qApp->screens().isEmpty())
    { // setup minimum width (mostly initial setup
        auto geo = geometry();
        const int preferredWidth = 1280;
        const int preferredHeight = 1024;
        auto *screen = qApp->screens().first();

        if (screen && screen->availableGeometry().width() > preferredWidth && geo.width() < preferredWidth)
        {
            geo.setLeft((screen->availableGeometry().width() - preferredWidth) / 2);
            geo.setWidth(preferredWidth);
        }
        if (screen && screen->availableGeometry().height() > preferredHeight && geo.height() < preferredHeight)
        {
            geo.setTop((screen->availableGeometry().height() - preferredHeight) / 2);
            geo.setHeight(preferredHeight);
        }

        setGeometry(geo);
    }

    auto docks = std::array<QDockWidget*, 5> { dockSourceRouting, dockNodeList, dockBinding, m_dockNodeInfo, dockClusterInfo };

    for (auto *dock : docks)
    {
        dock->hide();
    }

    if (config.contains("nodelist/geometry")) {
        QByteArray arr = config.value("nodelist/geometry").toByteArray();
        m_nodeTableView->horizontalHeader()->restoreGeometry(arr);
    }

    if (config.contains("nodelist/state")) {
        QByteArray arr = config.value("nodelist/state").toByteArray();
        m_nodeTableView->horizontalHeader()->restoreState(arr);
    }

    if (config.contains("nodeview/sceneRect")) {
        QRectF r = config.value("nodeview/sceneRect").toRectF();
        ui->graphicsView->setSceneRect(r);
    }

    bool enableRFD = true;
    bool enableFFD = true;

    if (config.contains("controller/autoFetchFFD"))
    {
        enableFFD = config.value("controller/autoFetchFFD").toBool();
    }

    if (config.contains("controller/autoFetchRFD"))
    {
        enableRFD = config.value("controller/autoFetchRFD").toBool();
    }

    createFetchMenu(enableRFD, enableFFD);

    if (config.contains("controller/apsAcksEnabled"))
    {
        bool apsAcksEnabled = config.value("controller/apsAcksEnabled").toBool();
        deCONZ::netEdit()->setApsAcksEnabled(apsAcksEnabled);
    }
    else
    {
        deCONZ::netEdit()->setApsAcksEnabled(false);
    }

    m_remoteIP = "127.0.0.1";
    if (config.contains("remote/default/ip"))
    {
        QString ip = config.value("remote/default/ip").toString();
        QHostAddress addr;
        if (addr.setAddress(ip))
        {
            m_remoteIP = ip;
        }
    }

    m_remotePort = 8080;
    if (config.contains("remote/default/port"))
    {
        bool ok;
        int port = config.value("remote/default/IP").toInt(&ok);

        if (ok)
        {
            m_remotePort = port;
        }
    }

    if (config.contains("discovery/zdp/nwkAddrInterval"))
    {
        int interval = config.value("discovery/zdp/nwkAddrInterval").toInt();
        deCONZ::setFetchInterval(deCONZ::ReqNwkAddr, interval);
    }

    if (config.contains("discovery/zdp/mgmtLqiInterval"))
    {
        int interval = config.value("discovery/zdp/mgmtLqiInterval").toInt();
        deCONZ::setFetchInterval(deCONZ::ReqMgmtLqi, interval);
    }

    connect(m_controller, SIGNAL(notify(zmNetEvent)),
            this, SLOT(onControllerEvent(zmNetEvent)));

    // device connection
    connect(ui->devConnectButton, SIGNAL(clicked()),
            this, SLOT(devConnectClicked()));

    connect(ui->devUpdateButton, SIGNAL(clicked()),
            this, SLOT(devUpdateClicked()));

    connect(ui->refreshComButton, SIGNAL(clicked()),
            this, SLOT(getComPorts()));

    // device monitor
    connect(deCONZ::master(), SIGNAL(deviceConnected()),
            this, SLOT(onDeviceConnected()));

    connect(deCONZ::master(), SIGNAL(deviceDisconnected(int)),
            this, SLOT(onDeviceDisconnected(int)));

    connect(deCONZ::master(), SIGNAL(deviceState()),
            this, SLOT(onDeviceState()));

    connect(deCONZ::master(), SIGNAL(deviceActivity()),
            this, SLOT(onDeviceActivity()));

    connect(deCONZ::master(), SIGNAL(deviceStateTimeOut()),
            this, SLOT(onDeviceStateTimeout()));

    getComPorts();

    _mainWindow = this;

    m_fetchTimer = -1;

    m_nodesOnlineLabel->clear();

    QString zclFile = getStorageLocation(deCONZ::ZcldbLocation);
    deCONZ::zclDataBase()->initDbFile(zclFile);
    deCONZ::zclDataBase()->reloadAll(zclFile);

    loadPlugIns();

    deCONZ::controller()->loadNodesFromDb();
    deCONZ::controller()->restoreNodesState();

    m_dockNodeInfo->raise();

    m_showPanelActions.append(m_dockNodeInfo->toggleViewAction());
    m_showPanelActions.append(dockClusterInfo->toggleViewAction());

    dockClusterInfo->hide();
    m_dockNodeInfo->hide();

    deCONZ::utilSetNotifyHandler(deCONZ::notifyHandler);

    // TODO(mpi): Remove when events come from AM_ACTOR_ID_GUI_NODE
    connect(deCONZ::controller(), &zmController::nodeEvent, this, &MainWindow::onNodeEvent);

    setState(StateIdle, __LINE__);

    QTimer::singleShot(1000, this, SLOT(initAutoConnectManager()));

    ui->devConnectButton->setFocus();

    QTimer::singleShot(10, this, &MainWindow::loadPluginsStage2);
}

MainWindow::~MainWindow()
{
    m_master = nullptr;
    protocol_exit();

    _clusterInfo = nullptr;
    _nodeInfo = nullptr;
    _bindDropBox = nullptr;
    _netEdit = nullptr;
    _nodeModel = nullptr;
    _mainWindow = nullptr;
    delete ui;
    ui = nullptr;
    Theme_Destroy();
}

void MainWindow::onControllerEvent(const zmNetEvent &event)
{
    switch (event.type())
    {
    case deCONZ::NodeDataChanged:
        m_nodeInfo->dataChanged(event.node());
        setNodesOnline();
        m_connTimeout = MaxConnectionTimeout;
        break;

    default: break;
    }
}

void MainWindow::onDeviceConnected()
{
    m_connTimeout = 0;
    setState(StateConnected, __LINE__);
    deCONZ::controller()->setParameter(deCONZ::ParamDeviceName, m_devEntry.friendlyName);

    if (!m_devEntry.serialNumber.isEmpty())
    {
        m_reconnectDevPath = m_devEntry.serialNumber;
    }
    else
    {
        m_reconnectDevPath = m_devEntry.path;
    }

    updateNetworkControls();
    setNodesOnline();
}

void MainWindow::onDeviceDisconnected(int reason)
{
    m_waitReconnectCount = WaitReconnectDuration;
    m_master->comExit();
    // mimic user disconnect
    devDisconnectClicked();

    DBG_Printf(DBG_INFO_L2, "device disconnected reason: %d, device index: %u\n", reason, (unsigned)m_autoConnIdx);
    if (m_reconnectDevPath.isEmpty())
    {
        m_autoConnIdx++;
    }
}

void MainWindow::onDeviceState()
{
    // reset timeout
    m_connTimeout = 0;

    if (deCONZ::master()->connected())
    {
        if (ui->stackedView->currentWidget() == ui->pageOffline)
        {
            // initial proof that we have a real device
            // proceed
            // deCONZ::master()->readParameters();
            showNodeViewPage();

            statusBar()->showMessage(tr("Connected successful to device"), 10000);
        }
    }
}

void MainWindow::onDeviceActivity()
{
    m_connTimeout = 0; // reset
}

void MainWindow::onDeviceStateTimeout()
{
    m_connTimeout++;

    if (deCONZ::master()->deviceFirmwareVersion() == FW_ONLY_AVR_BOOTLOADER)
    {
        if (m_connTimeout < MaxConnectionTimeoutBootloaderOnly)
        {
            return; // try longer, wait fw update
        }
    }

    if (m_connTimeout >= MaxConnectionTimeout)
    {
        DBG_Printf(DBG_INFO, "device state timeout (handled)\n");
        m_connTimeout = 0;

        if (deCONZ::appArgumentNumeric("--auto-connect", 1) == 1)
        {
            if (m_devs.size() > 0)
            {
                if (m_autoConnIdx < m_devs.size())
                {
                    deCONZ::DeviceEntry &dev = m_devs[m_autoConnIdx];
                    dev.failedConnects++;

#if 0
// TODO causes problems on RaspBee II (and ConBee II?)
// FIXME reimplement with proper checks and state machine
                    // tried all devices
                    if (m_state == StateConnecting)
                    {
                        if ((m_devs.size() - 1) == m_autoConnIdx)
                        {
                            if (m_restPlugin && deCONZ::controller()->getParameter(deCONZ::ParamFirmwareUpdateActive) == deCONZ::FirmwareUpdateReadyToStart)
                            {
                                m_devUpdateCanditate = true;
                            }

                            if (m_devUpdateCanditate)
                            {
                                setState(StateFirmwareNeedUpdate, __LINE__);
                            }
                        }
                    }
#endif
                }
            }
        }

        // mimic user disconnect
        devDisconnectClicked();
    }
    else
    {
        DBG_Printf(DBG_INFO_L2, "device state timeout ignored in state %d\n", m_state);
    }
}

void MainWindow::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_fetchTimer)
    {
        if (!m_master || !m_controller)
            return;

        if (m_waitReconnectCount > 0)
        {
            DBG_Printf(DBG_INFO_L2, "wait reconnect %d seconds\n", m_waitReconnectCount);
            m_waitReconnectCount--;
        }

        // TODO if controller in fw update state, disconnect
        // and only reconnect when no more in fw update state
        if (!m_master->isOpen())
        {
            if (m_connState != deCONZ::NotInNetwork)
            {
                m_nodesOnlineLabel->clear();
                ui->refreshComButton->setEnabled(true);
                getComPorts();

                m_connState = deCONZ::NotInNetwork;
            }

            if (m_state == StateFirmwareUpdateRunning)
            {
                double ms = m_firmwareUpdateTime.elapsed();
                double totalMs = FW_UPDATE_TIME_MS;

                double val = (ms / totalMs) * 100;
                if (val > 100)
                    val = 100;

                ui->fwProgressBar->setValue(static_cast<int>(val));
                statusBar()->showMessage(tr("Firmware update running , please wait"));

            }

            initAutoConnectManager();
        }
        else //  connected
        {
            if (m_controller->getParameter(deCONZ::ParamFirmwareUpdateActive) == deCONZ::FirmwareUpdateRunning)
            {
                setState(StateFirmwareUpdateRunning, __LINE__);
                m_reconnectAfterFirmwareUpdate = true;
                // we need to disconnect in order to update firmware
                devDisconnectClicked();
            }
        }
    }
}

void MainWindow::onSelectionChanged()
{
    QGraphicsScene *scene = ui->graphicsView->scene();

    QList<QGraphicsItem *>items = scene->selectedItems();
    QList<zmgNode*> nodes;

    {
        QList<QGraphicsItem *>::iterator i = items.begin();
        QList<QGraphicsItem *>::iterator end = items.end();

        for (; i!= end; ++i)
        {
            zmgNode *g = qgraphicsitem_cast<zmgNode*>(*i);
            if (g)
            {
                nodes.push_back(g);
            }
        }
    }

    if (nodes.isEmpty() || (nodes.size() > 1)) // only display one
    {
        m_nodeInfo->setNode(nullptr);
    }
    else
    {
        m_nodeInfo->setNode(m_vfsModel, nodes.first()->data()->address().ext());
        m_nodeInfo->setNode(nodes.first()->data());
    }
}

void MainWindow::onNodeEvent(const deCONZ::NodeEvent &event)
{
    // TODO(mpi): Currently these events come from the controller. However the mainwindow
    // shall subscribe to AM_ACTOR_ID_GUI_NODE to receive these events within the GUI.

    if (event.node() && event.event() == deCONZ::NodeEvent::NodeContextMenu)
    {
        QMenu menu;
        menu.addAction(readNodeDescriptorAction);
        menu.addAction(readActiveEndpointsAction);
        menu.addAction(readSimpleDescriptorsAction);
        if (readBindingTableAction)
        {
            menu.addAction(readBindingTableAction);
        }
        if (ui->graphicsView->scene()->selectedItems().size() > 2)
        {
            menu.addAction(addSourceRouteAction);
        }
        if (ui->graphicsView->scene()->selectedItems().size() == 1)
        {
            zmgNode *g = qgraphicsitem_cast<zmgNode*>(ui->graphicsView->scene()->selectedItems().front());
            if (g && g->data() && !g->data()->sourceRoutes().empty())
            {
                menu.addAction(removeSourceRouteAction);
            }
        }

        if (ui->graphicsView->scene()->selectedItems().size() == 1 &&
                event.node()->address().nwk() != 0x0000)
        {
            menu.addAction(editDDFAction);
            menu.addSeparator();
            menu.addAction(deleteNodeAction);
        }

        menu.exec(QCursor::pos());
    }
    else if (event.node() && event.event() == deCONZ::NodeEvent::NodeSelected)
    {
        readNodeDescriptorAction->setEnabled(true);
        readActiveEndpointsAction->setEnabled(true);
        readSimpleDescriptorsAction->setEnabled(true);
        resetNodeAction->setEnabled(true);
        deleteNodeAction->setEnabled(true);
    }
    else if (event.event() == deCONZ::NodeEvent::NodeDeselected)
    {
        readNodeDescriptorAction->setEnabled(false);
        readActiveEndpointsAction->setEnabled(false);
        readSimpleDescriptorsAction->setEnabled(false);
        resetNodeAction->setEnabled(false);
        deleteNodeAction->setEnabled(false);
    }
}

void MainWindow::getComPorts()
{
    std::vector<deCONZ::DeviceEntry> devs;

    {
        const QString comPort = deCONZ::appArgumentString(QLatin1String("--dev"), QString());
        if (!comPort.isEmpty())
        {
            deCONZ::DeviceEntry dev;
            dev.path = deCONZ::DEV_StableDevicePath(comPort);

            if (comPort.contains(QLatin1String("ttyUSB")))
            {
                dev.friendlyName = QLatin1String("ConBee");
            }
            else if (comPort.contains(QLatin1String("ttyACM")) || comPort.contains(QLatin1String("ConBee_II")))
            {
                dev.friendlyName = QLatin1String("ConBee II");
            }
            else
            {
                dev.friendlyName = QLatin1String("RaspBee");
            }

            if (m_devEnum->listSerialPorts())
            {
                devs = m_devEnum->getList();
            }

            // try to find more detailed descriptor
            bool available = false;
            for (const auto &d : devs)
            {
                if (d.path == dev.path)
                {
                    available = true;
                    dev = d;
                    DBG_Printf(DBG_INFO, "COM: %s / serialno: %s, %s\n", qPrintable(dev.path), qPrintable(dev.serialNumber), qPrintable(dev.friendlyName));
                    break;
                }
            }

            devs.clear();
            if (available)
            {
                devs.push_back(dev);
            }
            deCONZ::controller()->setParameter(deCONZ::ParamDeviceName, dev.friendlyName);
        }
    }

    if (devs.empty() && m_devEnum->listSerialPorts())
    {
        devs = m_devEnum->getList();
    }

    if (!m_devUpdateCanditate)
    {
        if (deCONZ::controller()->getParameter(deCONZ::ParamFirmwareUpdateActive) == deCONZ::FirmwareUpdateReadyToStart)
        {
            m_devUpdateCanditate = true;
        }
    }

    if (m_devs == devs)
    {
        // nothing changed
        return;
    }

    m_devUpdateCanditate = false;
    m_devs.clear();
    ui->usbComboBox->clear();
    int pos = 0;
    int devId = 0;

    std::vector<deCONZ::DeviceEntry>::iterator i = devs.begin();
    std::vector<deCONZ::DeviceEntry>::iterator end = devs.end();

    for (; i != end; ++i)
    {
        ui->usbComboBox->insertItem(pos++, QString("%1  %2").arg(i->friendlyName, i->serialNumber), devId++);
        m_devs.push_back(*i);
    }
}

void MainWindow::devConnectClicked()
{
    if (deCONZ::master()->isOpen())
    {
        DBG_Printf(DBG_INFO, "%s connect clicked while connected\n", Q_FUNC_INFO);
        return;
    }

    int index = ui->usbComboBox->currentIndex();

    if (index != -1)
    {
        bool ok;
        int port = ui->usbComboBox->itemData(index).toInt(&ok);

        if (ok && (port >= 0) && (port < static_cast<int>(m_devs.size())))
        {
            const deCONZ::DeviceEntry &dev = m_devs[port];
            QString devPath = deCONZ::DEV_ResolvedDevicePath(dev.path);
            if (devPath.isEmpty())
            {
                devPath = dev.path;
            }
            if (deCONZ::master()->openSerial(devPath, dev.baudrate) == 0)
            {
                DBG_Printf(DBG_INFO, "%s choose com %s\n", Q_FUNC_INFO, qPrintable(dev.path));
                m_devEntry = dev;

                setState(StateConnecting, __LINE__);
            }
            else
            {
                DBG_Printf(DBG_INFO, "%s master open serial error: %s\n", Q_FUNC_INFO, qPrintable(dev.path));
            }
        }
        else
        {
            DBG_Printf(DBG_INFO, "%s no valid com port id: %d\n", Q_FUNC_INFO, port);
        }

        m_connState = deCONZ::UnknownState;
    }
    else
    {
        DBG_Printf(DBG_INFO, "%s no valid combobox idx: %d\n", Q_FUNC_INFO, index);
    }
}

void MainWindow::devDisconnectClicked()
{
    if (m_master->isOpen())
    {
        m_master->comExit();
        m_connState = deCONZ::UnknownState;
        deCONZ::setDeviceState(deCONZ::NotInNetwork);
    }

    if (!deCONZ::master()->connected())
    {
        showDevicePage();
    }

    if (m_state == StateConnecting || m_state == StateConnected)
    {
        if (m_controller->getParameter(deCONZ::ParamFirmwareUpdateActive) == deCONZ::FirmwareUpdateRunning)
        {
            setState(StateFirmwareUpdateRunning, __LINE__);
        }
        else
        {
            setState(StateIdle, __LINE__);
        }
    }
    updateNetworkControls();
    setNodesOnline();
}

void MainWindow::devUpdateClicked()
{
    if (m_restPlugin)
    {
        statusBar()->showMessage(tr("Start firmware update, please wait"));
        QMetaObject::invokeMethod(m_restPlugin, "startUpdateFirmware");
    }
}

void MainWindow::notifyUser(const QString &text)
{
    statusBar()->showMessage(text, 7 * 1000);
}

void MainWindow::onNetStartFailed(uint8_t zdoStatus)
{
    switch (zdoStatus)
    {
    // TODO:
    default:
        break;
    }
}

void MainWindow::setDeviceState(deCONZ::State state)
{
    if (m_connState != state)
    {
        m_connState = state;
        updateNetworkControls();
    }
}

void ListPluginFilesRecursive(const QString &path, QStringList *out, int depth)
{
    if (depth > 4)
        return;

    QDir dir(path);

    const auto entryList = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);

    for (const auto &entry : entryList)
    {
        const QString absPath = entry.absoluteFilePath();

        if (entry.isDir())
        {
            ListPluginFilesRecursive(absPath, out, depth + 1);
        }
        else if (
            absPath.endsWith(QLatin1String("plugin.so"))
#ifdef _WIN32
            || absPath.endsWith(QLatin1String("plugin.dll"))
#endif
#ifdef __APPLE__
            || absPath.endsWith(QLatin1String("plugin.dylib"))
#endif
            )
        {
            out->append(absPath);
        }
    }
}

void MainWindow::loadPlugIns()
{
    QDir dir(qApp->applicationDirPath());

    QString plugins;
#if defined(__linux__)
    if (dir.absolutePath().endsWith("bin"))
    {
        plugins = "../share/deCONZ/plugins";
    }
    else
    {
        plugins = "plugins";
    }
#endif
#ifdef __APPLE__
    dir.cdUp();
    dir.cd("PlugIns");
    plugins = dir.path();
#endif
#ifdef _WIN32
    plugins = "plugins"; // windows
#endif

    if (!dir.cd(plugins))
    {
        DBG_Printf(DBG_INFO, "%s/%s no plugin directory found\n", qPrintable(qApp->applicationDirPath()), qPrintable(plugins));
        return;
    }

    QStringList fileList;
    ListPluginFilesRecursive(dir.absolutePath(), &fileList, 0);
    const QStringList &fileListConst = fileList; // prevent detach warning

    for (const QString &fileName: fileListConst)
    {
        const QString absFilePath = dir.absoluteFilePath(fileName);

#ifdef USE_ACTOR_MODEL
        /* actor model plugin interface */
        AM_LoadPlugin(qPrintable(absFilePath));
#endif

        QPluginLoader *pluginLoader =  new QPluginLoader(absFilePath, this);

        deCONZ::NodeInterface *ifaceNode = nullptr;
        QObject *plugin = pluginLoader->instance();

        if (plugin)
        {
            ifaceNode = qobject_cast<deCONZ::NodeInterface *>(plugin);
            Q_ASSERT(ifaceNode && ifaceNode->name());
            if (ifaceNode && ifaceNode->name() == nullptr)
            {
                continue;
            }
        }

        if (!plugin || !ifaceNode || !ifaceNode->name())
        {
            DBG_Printf(DBG_ERROR, "error loading plugin: %s\n", qPrintable(pluginLoader->errorString()));
            continue;
        }

        connect(this, &QObject::destroyed, this, [pluginLoader]() {
            if (pluginLoader->isLoaded())
                pluginLoader->unload();

            pluginLoader->deleteLater();
        });

        if (strstr(ifaceNode->name(), "REST"))
        {
            m_restPlugin = plugin;
        }

        DBG_Printf(DBG_INFO, "found node plugin: %s - %s\n", qPrintable(fileName), ifaceNode->name());

        deCONZ::controller()->addNodePlugin(ifaceNode);
        m_plugins.push_back(ifaceNode);
    }
}

void MainWindow::loadPluginsStage2()
{
    QList<QDockWidget*> dockList = tabifiedDockWidgets(m_dockNodeInfo);
    QDockWidget *tabDock = nullptr;

    if (!dockList.isEmpty())
    {
        tabDock = dockList.last();
    }
    else
    {
        tabDock = m_dockNodeInfo;
    }

    for (auto ifaceNode : m_plugins)
    {
        if (ifaceNode->name() == nullptr)
        {
            continue;
        }

        QString name(ifaceNode->name());

        // provides a Widget?
        if (ifaceNode->hasFeature(deCONZ::NodeInterface::WidgetFeature))
        {
            QWidget *w = ifaceNode->createWidget();
            if (w)
            {
                QString dockName = name;
                if (!w->windowTitle().isEmpty() && w->windowTitle() != QLatin1String("Form"))
                {
                    dockName = w->windowTitle();
                }
                if (w->layout())
                {
                    w->layout()->setContentsMargins(0,0,0,0);
                }
                QDockWidget *dock = new QDockWidget(dockName, this);
                dock->setObjectName(name.trimmed().replace(" ", ""));
                dock->setTitleBarWidget(new QWidget()); // don't show title bar
                dock->setWidget(w);
                dock->hide();
                //dock->setBackgroundRole(QPalette::Highlight);
                //dock->setAutoFillBackground(true);
                if (!restoreDockWidget(dock))
                {
                    addDockWidget(Qt::LeftDockWidgetArea, dock);
                    tabifyDockWidget(tabDock, dock);
                }
                else if (dock->isVisible())
                {
                    m_showPanelActions.append(dock->toggleViewAction());
                }

                dock->hide();

                tabDock = dock;

                if (strstr(ifaceNode->name(), "OTA"))
                {
                    m_menuPlugins->addAction(dock->toggleViewAction());
                }

                m_menuPanels->addAction(dock->toggleViewAction());
            }
        }

        // provides a Dialog?
        if (ifaceNode->hasFeature(deCONZ::NodeInterface::DialogFeature))
        {
            QDialog *dlg = ifaceNode->createDialog();
            Q_ASSERT(dlg);
            dlg->setParent(this, Qt::Dialog);
            dlg->hide();

            const auto actions = dlg->actions();
            for (auto *action : actions)
            {
                DBG_Printf(DBG_INFO, "dlg action: %s\n", qPrintable(action->text()));
                if (action->property("type") == QLatin1String("node-action"))
                {
                    addAction(action);

                    if (action->property("actionid") == QLatin1String("read-binding-table"))
                    {
                        readBindingTableAction = action;
                        Q_ASSERT(m_editMenu);
                        m_editMenu->insertAction(readNodeDescriptorAction, action);
                    }
                }
            }

            m_menuPlugins->addAction(QString(ifaceNode->name()), dlg, SLOT(show()));
        }

        // has a HTTP Request Handler?
        if (ifaceNode->hasFeature(deCONZ::NodeInterface::HttpClientHandlerFeature))
        {
            deCONZ::HttpClientHandler *handler = dynamic_cast<deCONZ::HttpClientHandler*>(ifaceNode);
            if (handler)
            {
                deCONZ::registerHttpClientHandler(handler);
            }
        }
    }

    if (!m_menuPlugins->isEnabled())
    {
        m_menuPlugins->setEnabled(true);
    }
}

/*! Init the auto connection manager.

    - must be activated via commandline switch --auto-connect=1
 */
void MainWindow::initAutoConnectManager()
{
    if (m_fetchTimer == -1)
    {
        m_fetchTimer = startTimer(MainTickMs);
    }

    quint8 updateState = m_controller->getParameter(deCONZ::ParamFirmwareUpdateActive);

    if (updateState == deCONZ::FirmwareUpdateRunning)
    {
        setState(StateFirmwareUpdateRunning, __LINE__);
        if (!m_reconnectAfterFirmwareUpdate)
        { //always try to recconect after firmware update
            m_reconnectAfterFirmwareUpdate = true;
        }

        // don't connect while updating firmware
        return;
    }
    else if (m_state == StateFirmwareUpdateRunning)
    {
        // finished update process
        if (updateState == deCONZ::FirmwareUpdateIdle || updateState == deCONZ::FirmwareUpdateReadyToStart)
        {
            if (m_firmwareUpdateTime.elapsed() < FW_UPDATE_TIME_MS + FW_UPDATE_TIME_BACKOFF_MS)
            {
                DBG_Printf(DBG_INFO, "Wait reconnect after firmware update\n");
                return;
            }
            m_waitReconnectCount = WaitReconnectDuration;
            setState(StateIdle, __LINE__);
        }
    }

    if (m_state == StateFirmwareNeedUpdate || m_state == StateFirmwareUpdateRunning)
    {
        return;
    }

    if (m_waitReconnectCount > 0)
    {
        DBG_Assert(m_waitReconnectCount <= WaitReconnectDuration);
        return;
    }

    if (deCONZ::appArgumentNumeric("--auto-connect", 1) != 1)
    {
        if (!m_reconnectAfterFirmwareUpdate)
        {
            // not activated
            return;
        }
    }

    if (!m_master || !m_controller)
    {
        return;
    }

    if (deCONZ::master()->isOpen())
    {
        return;
    }

    if (m_state == StateFirmwareNeedUpdate || m_state == StateFirmwareUpdateRunning)
    {
        return;
    }

    getComPorts();

    if (!m_reconnectDevPath.isEmpty()) // try to reconnect to same device
    {
        m_autoConnIdx = 0;
        for (const deCONZ::DeviceEntry &e : m_devs)
        {
            if (e.serialNumber == m_reconnectDevPath)
            {
                break;
            }
            else if (e.path == m_reconnectDevPath)
            {
                break;
            }
            m_autoConnIdx++;
        }
        m_reconnectDevPath.clear();
    }

    if (m_devs.size() > 0)
    {
        // use a iterator so if connecting to m_devs[0] fails the next attemp will be with m_devs[1]
        if (m_autoConnIdx >= m_devs.size())
        {
            m_autoConnIdx = 0;
        }

        const deCONZ::DeviceEntry &dev = m_devs[m_autoConnIdx];

        m_connTimeout = 0;
        setState(StateConnecting, __LINE__);

        QString devPath = deCONZ::DEV_ResolvedDevicePath(dev.path);
        if (devPath.isEmpty())
        {
            devPath = dev.path;
        }

        int ret = deCONZ::master()->openSerial(devPath, dev.baudrate);
        if (ret == 0)
        {
            DBG_Printf(DBG_INFO_L2, "auto connect com %s\n", qPrintable(devPath));
            m_devEntry = dev;

            ui->devConnectButton->setEnabled(false);
            m_actionDeviceDisconnect->setEnabled(false);
        }
        else
        {
            DBG_Printf(DBG_INFO_L2, "failed open com status: (%d), path: %s\n", ret, qPrintable(dev.path));
            m_waitReconnectCount = WaitReconnectDuration2;
            if (m_reconnectDevPath.isEmpty())
            {
                m_autoConnIdx++;
            }
        }

        m_connState = deCONZ::UnknownState;
    }
}



void MainWindow::createMainToolbar()
{
    QWidget *w = new QWidget;
    w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui->mainToolBar->addWidget(w);

    m_leaveAction = ui->mainToolBar->addAction(tr("Leave"), deCONZ::master(), SLOT(leaveNetwork()));
    m_leaveAction->setEnabled(false);
    m_leaveAction->setToolTip(tr("Leave the network"));
    connect(m_leaveAction, &QAction::triggered, [](){
        emit deCONZ::controller()->networkStateChangeRequest(false);
    });

    m_joinAction = ui->mainToolBar->addAction(tr("Join"), deCONZ::master(), SLOT(joinNetwork()));
    m_joinAction->setEnabled(false);
    m_joinAction->setToolTip(tr("Joins or starts a network"));
    connect(m_joinAction, &QAction::triggered, [](){
        emit deCONZ::controller()->networkStateChangeRequest(true);
    });

    m_netStateLabel = new QLabel;

    ui->mainToolBar->addWidget(m_netStateLabel);

    m_netConfigAction = ui->mainToolBar->addAction(QIcon(":/icons/faenza/preferences-desktop.png"),
                                                   tr("Network Preferences"),
                                                   this, SLOT(showNetworkSettings()));

    // auto fetching control button
    m_autoPushButton = new QPushButton;
    m_autoPushButton->setIcon(QIcon(":/icons/auto-off.png"));
    m_autoPushButton->setIconSize(QSize(24, 24));
    m_autoPushButton->setToolTip(tr("Control ZDP auto fetching"));
    m_autoPushButton->setMaximumWidth(32);
    m_autoPushButton->setFlat(true);
    ui->mainToolBar->addWidget(m_autoPushButton);

    // add a spacer
    w = new QWidget;
    w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    w->setFixedWidth(8);
    ui->mainToolBar->addWidget(w);

    auto *lqiButton = new QPushButton(tr("LQI"));
    lqiButton->setCheckable(true);
    lqiButton->setChecked(false);
    lqiButton->setToolTip(tr("Toggle show Link Quality Indicator (LQI) values in links between nodes"));
    connect(lqiButton, &QPushButton::toggled, m_controller, &zmController::toggleLqiView);
    ui->mainToolBar->addWidget(lqiButton);

    auto *linksButton = new QPushButton(tr("Neighbor Links"));
    linksButton->setCheckable(true);
    linksButton->setChecked(true);
    linksButton->setToolTip(tr("Toggle show neighbor table links between nodes"));
    connect(linksButton, &QPushButton::toggled, m_controller, &zmController::toggleNeighborLinks);
    ui->mainToolBar->addWidget(linksButton);

    // add a spacer to center the buttons
    w = new QWidget;
    w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui->mainToolBar->addWidget(w);

    m_nodesOnlineLabel = new QLabel;
    m_nodesOnlineLabel->setContentsMargins(12, 0, 12, 0);
    ui->mainToolBar->addWidget(m_nodesOnlineLabel);

    m_openPhosconAppButton = new QPushButton(tr("Phoscon App"));
    m_openPhosconAppButton->setToolTip(tr("Opens the Phoscon App in your browser."));
    connect(m_openPhosconAppButton, SIGNAL(clicked(bool)), this, SLOT(openPhosconApp()));
    ui->mainToolBar->addWidget(m_openPhosconAppButton);

#if (QT_VERSION >= QT_VERSION_CHECK(5, 11, 0))
    // ensure minimum horizontal padding of buttons
    int pad = 32;
    linksButton->setMinimumWidth(fontMetrics().horizontalAdvance(linksButton->text()) + pad);
    m_openPhosconAppButton->setMinimumWidth(fontMetrics().horizontalAdvance(m_openPhosconAppButton->text()) + pad);
#endif
}

void MainWindow::createHelpMenu()
{
    QMenuBar *menuBar = this->menuBar();
    QMenu *menu = menuBar->addMenu(tr("Help"));
    QAction *userManual = menu->addAction(tr("User Manual"));
    connect(userManual, SIGNAL(triggered()),
            this, SLOT(showUserManual()));

    if (!gHeadlessVersion)
    {
        auto *dbgView = menu->addAction(tr("Debug view"));
        connect(dbgView, &QAction::triggered, m_debugView, &DebugView::show);

        auto *actorView = menu->addAction(tr("Data view"));
        actorView->setShortcut(QKeySequence::fromString("F8"));
        connect(actorView, &QAction::triggered, this, &MainWindow::showActorView);
    }

    QAction *webApp2016 =  menu->addAction(QString(tr("Open old WebApp (2016)")));
    webApp2016->setToolTip(tr("Open the old 2016 WebApp in browser."));
    connect(webApp2016, SIGNAL(triggered()),
            this, SLOT(openWebApp()));

    menu->addSeparator();

    QAction *about =  menu->addAction(QString(tr("About ")) + qApp->applicationName());
    connect(about, SIGNAL(triggered()),
            this, SLOT(showAboutDialog()));
}

void MainWindow::createFetchMenu(bool enableRFD, bool enableFFD)
{
    // auto fetching buttons
    QMenu *autoFetchMenu = new QMenu(m_autoPushButton);
    QAction *autoFFD = autoFetchMenu->addAction(tr("Routers and Coordinator"));
    connect(autoFFD, SIGNAL(toggled(bool)),
            deCONZ::controller(), SLOT(setAutoFetchingFFD(bool)));
    autoFFD->setCheckable(true);
    autoFFD->setChecked(enableFFD);
    QAction *autoRFD = autoFetchMenu->addAction(tr("End-devices"));
    connect(autoRFD, SIGNAL(toggled(bool)),
            deCONZ::controller(), SLOT(setAutoFetchingRFD(bool)));
    autoRFD->setCheckable(true);
    autoRFD->setChecked(enableRFD);
    m_autoPushButton->setMenu(autoFetchMenu);
}

void MainWindow::setState(MainWindow::State state, int line)
{
    if (state != m_state)
    {
        Q_UNUSED(line);

        bool dockVisible = false;
        m_state = state;
        if (state == StateIdle)
        {
            ui->stateStackedWidget->setCurrentWidget(ui->connectPage);
            ui->devConnectButton->setEnabled(true);
            m_actionDeviceDisconnect->setEnabled(false);
            statusBar()->clearMessage();
        }
        else if (state == StateConnecting)
        {
            ui->stateStackedWidget->setCurrentWidget(ui->connectPage);
            ui->devConnectButton->setEnabled(false);
            m_actionDeviceDisconnect->setEnabled(false);
            statusBar()->showMessage(tr("Connecting to device"));
        }
        else if (state == StateConnected)
        {
            ui->stateStackedWidget->setCurrentWidget(ui->connectPage);
            ui->devConnectButton->setEnabled(false);
            m_actionDeviceDisconnect->setEnabled(true);
            statusBar()->clearMessage();
            dockVisible = true;
        }
        else if (state == StateFirmwareNeedUpdate)
        {
            ui->stateStackedWidget->setCurrentWidget(ui->updateFirmwarePage);
            Q_ASSERT(m_devUpdateCanditate);
            m_actionDeviceDisconnect->setEnabled(false);
            statusBar()->showMessage(tr("Firmware update needed, please press the Update Firmware button"));
        }
        else if (state == StateFirmwareUpdateRunning)
        {
            ui->stateStackedWidget->setCurrentWidget(ui->updateRunningPage);
            ui->fwProgressBar->setValue(0);
            ui->fwProgressBar->setMaximum(100);
            m_firmwareUpdateTime.start();
            m_actionDeviceDisconnect->setEnabled(false);
            if (!m_devEntry.serialNumber.isEmpty())
            {
                m_reconnectDevPath = m_devEntry.serialNumber;
            }
            else
            {
                m_reconnectDevPath = m_devEntry.path;
            }

            statusBar()->showMessage(tr("Firmware update running, please wait"));
        }
        else
        {
            DBG_Printf(DBG_ERROR, "MainWindow::setState unhandled state %d\n", state);
            Q_ASSERT(0);
        }

        const auto docks = tabifiedDockWidgets(m_dockNodeInfo);
        for (auto *dock : docks)
        {
            dock->setVisible(dockVisible);
        }
    }
}

void MainWindow::updateLogo()
{
    // draw logo in current theme style (blending logo mask against theme color)
    {
        QImage mask(":/img/deconz_mask.png");
        mask = mask.scaledToWidth(320, Qt::SmoothTransformation);
        QImage img(mask.width(), mask.height(), QImage::Format_ARGB32);
        QColor fg = palette().color(QPalette::WindowText);

        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                int a = mask.pixelColor(x, y).red();
                QColor c = fg;
                c.setAlpha(a);
                img.setPixelColor(x, y, c);
            }
        }
        ui->labelLogo->setPixmap(QPixmap::fromImage(img));
    }
}

void MainWindow::createFileMenu()
{
    QMenu *menu = menuBar()->addMenu(tr("&File"));
    QAction *quit = menu->addAction(tr("Quit"));
    quit->setShortcuts(QKeySequence::Quit);
    connect(quit, SIGNAL(triggered()),
            qApp, SLOT(quit()));
}

void MainWindow::createEditMenu()
{
    Q_ASSERT(!m_editMenu);
    m_editMenu = menuBar()->addMenu(tr("&Edit"));
    resetNodeAction = m_editMenu->addAction(tr("Reset node"));
    resetNodeAction->setShortcuts(QKeySequence::Refresh);
    connect(resetNodeAction, SIGNAL(triggered()),
            this, SLOT(resetNodesActionTriggered()));

    deleteNodeAction = m_editMenu->addAction(tr("Delete node"));
    deleteNodeAction->setShortcuts(QKeySequence::Delete);
    connect(deleteNodeAction, SIGNAL(triggered()),
            this, SLOT(deleteNodesActionTriggered()));

    m_editMenu->addSeparator();

    readNodeDescriptorAction = m_editMenu->addAction(tr("Read node descriptor"));
    readNodeDescriptorAction->setShortcut(deCONZ::NodeKeyRequestNodeDescriptor);
    connect(readNodeDescriptorAction, &QAction::triggered, this, &MainWindow::readNodeDescriptorActionTriggered);

    readActiveEndpointsAction = m_editMenu->addAction(tr("Read active endpoints"));
    readActiveEndpointsAction->setShortcut(deCONZ::NodeKeyRequestActiveEndpoints);
    connect(readActiveEndpointsAction, &QAction::triggered, this, &MainWindow::readActiveEndpointsActionTriggered);

    readSimpleDescriptorsAction = m_editMenu->addAction(tr("Read simple descriptors"));
    readSimpleDescriptorsAction->setShortcut(deCONZ::NodeKeyRequestSimpleDescriptors);
    connect(readSimpleDescriptorsAction, &QAction::triggered, this, &MainWindow::readSimpleDescriptorsActionTriggered);

    editDDFAction = m_editMenu->addAction(tr("Edit DDF"));
    editDDFAction->setShortcut(tr("Ctrl+E"));
    connect(editDDFAction, &QAction::triggered, this, &MainWindow::editDDFActionTriggered);

    addSourceRouteAction = new QAction(tr("Add source route"), this);
    connect(addSourceRouteAction, &QAction::triggered, this, &MainWindow::addSourceRouteActionTriggered);

    removeSourceRouteAction = new QAction(tr("Remove source route"), this);
    connect(removeSourceRouteAction, &QAction::triggered, this, &MainWindow::removeSourceRouteActionTriggered);

    m_editMenu->addSeparator();

    QAction *preferences = m_editMenu->addAction(tr("Preferences"));
    connect(preferences, SIGNAL(triggered()),
            this, SLOT(showPreferencesDialog()));

    QAction *config = m_editMenu->addAction(tr("Network Settings"));
    config->setShortcuts(QList<QKeySequence>() << Qt::Key_F9);
    connect(config, SIGNAL(triggered()),
            this, SLOT(showNetworkSettings()));

    QAction *reboot = m_editMenu->addAction(tr("Reboot Device"));
    connect(reboot, SIGNAL(triggered()),
            m_master, SLOT(rebootDevice()));

#ifdef QT_DEBUG
    QAction *factoryReset = m_editMenu->addAction(tr("Factory Reset Device"));
    connect(factoryReset, SIGNAL(triggered()), m_master, SLOT(factoryReset()));
#endif

    m_sendToAction = m_editMenu->addAction(tr("Destination Settings"),
                                     this, SLOT(showSendToDialog()));

    m_sendToAction->setShortcuts(QList<QKeySequence>() << Qt::Key_F6);

    m_actionDeviceDisconnect = m_editMenu->addAction(tr("Disconnect"));
    connect(m_actionDeviceDisconnect, &QAction::triggered, this, &MainWindow::devDisconnectClicked);
    m_actionDeviceDisconnect->setEnabled(false);
}

void MainWindow::createViewMenu()
{
    QMenu *menu = menuBar()->addMenu(tr("&View"));

    m_lightThemeAction = menu->addAction(tr("Classic theme"));
    m_lightThemeAction->setData("light");
    connect(m_lightThemeAction, &QAction::triggered, this, &MainWindow::switchTheme);

    m_darkThemeAction = menu->addAction(tr("Dark theme"));
    m_darkThemeAction->setData("dark");
    connect(m_darkThemeAction, &QAction::triggered, this, &MainWindow::switchTheme);
}

void MainWindow::showAboutDialog()
{
    zmAboutDialog *dlg = new zmAboutDialog(this);
    dlg->show();
}

void MainWindow::showActorView()
{
    static ActorVfsView *dlg = nullptr;

    if (!dlg)
    {
        dlg = new ActorVfsView(m_vfsModel, this);
    }

    dlg->show();
}

void MainWindow::showUserManual()
{
    QString path;
    path.append(deCONZ::getStorageLocation(deCONZ::ApplicationsLocation));
#ifdef Q_OS_WIN
    path.append("\\doc\\");
#else
    if (path.startsWith("/usr"))
    {
        path.append("/share/deCONZ/doc/");
    }
    else
    {
        path.append("/doc/");
    }
#endif
    path.append(APP_USER_MANUAL_PDF);
    QUrl url;
    url.setScheme("file");
    url.setPath(path);
    QDesktopServices::openUrl(url.toString());
}

void MainWindow::showPreferencesDialog()
{
    static zmSettingsDialog *dlg = nullptr;

    if (!dlg)
    {
        dlg = new zmSettingsDialog(this);
    }

    dlg->show();
}

void MainWindow::showDevicePage()
{
    // prevent double call
    if (ui->stackedView->currentWidget() == ui->pageOffline)
    {
        return;
    }

    updateNetworkControls();
    ui->stackedView->setCurrentWidget(ui->pageOffline);

    // put all visible panels in a list and hide them until
    // we show the node view page
    m_showPanelActions.clear();

    foreach(QAction *a, m_menuPanels->actions())
    {
        if (a->isChecked())
        {
            a->trigger();
            m_showPanelActions.append(a);
        }
    }
}

void MainWindow::showNetworkSettings()
{
    if (DBG_Assert(deCONZ::master()->connected()) == false)
    {
        return;
    }

    if (DBG_Assert(deCONZ::netEdit() != nullptr) == false)
    {
        return;
    }

    deCONZ::netEdit()->checkFeatures();
    deCONZ::netEdit()->show();
}

void MainWindow::showSendToDialog()
{
    if (_sendToDialog->isHidden())
    {
        _sendToDialog->show();
    }
    else
    {
        _sendToDialog->hide();
    }
}

void MainWindow::showNodeViewPage()
{
    updateNetworkControls();
    if (deCONZ::master()->connected())
    {
        ui->stackedView->setCurrentWidget(ui->pageNodeView);

        // show panels which where hidden on device page
        foreach(QAction *a, m_showPanelActions)
        {
            if (!a->isChecked())
            {
                a->trigger();
            }
        }

        m_showPanelActions.clear();
    }
    else
    {
        showDevicePage();
    }
}

void MainWindow::setNodesOnline()
{
    int count;

    if (m_master->connected())
    {

        if (deCONZ::master()->netState() != deCONZ::InNetwork)
        {
            count = 0;
        }
        else
        {
            count = m_controller->nodeCount() - m_controller->zombieCount();
        }

        if (count > 0)
        {
            m_nodesOnlineLabel->setText(QString("%1 Nodes").arg(count));
        }
        else
        {
            m_nodesOnlineLabel->clear();
        }
    }
    else
    {
        m_nodesOnlineLabel->clear();
    }
}

void MainWindow::setAutoFetching()
{
    bool ffd = deCONZ::controller()->autoFetchFFD();
    bool rfd = deCONZ::controller()->autoFetchRFD();

    if (ffd && rfd)
    {
        m_autoPushButton->setIcon(QIcon(":/icons/auto-cre.png"));
    }
    else if (ffd && !rfd)
    {
        m_autoPushButton->setIcon(QIcon(":/icons/auto-cr.png"));
    }
    else if (!ffd && rfd)
    {
        m_autoPushButton->setIcon(QIcon(":/icons/auto-e.png"));
    }
    else
    {
        m_autoPushButton->setIcon(QIcon(":/icons/auto-off.png"));
    }
}

void MainWindow::deleteNodesActionTriggered()
{
    const auto items = ui->graphicsView->scene()->selectedItems();

    if (items.size() != 1)
    {
        return;
    }

    QGraphicsItem *item = items.first();

    {
        zmgNode *node = qgraphicsitem_cast<zmgNode*>(item);
        if (node && node->data())
        {
            const QString &nodeName = node->name();
            const QString extAddr = QString::fromLatin1(node->data()->extAddressString().c_str());

            QMessageBox dlg(QMessageBox::NoIcon,
                            tr("Delete Node"),
                            tr("Do you really want to delete  <b>%1</b>?\n\n     (%2)").arg(nodeName, extAddr),
                            QMessageBox::Yes | QMessageBox::Cancel);


            dlg.setInformativeText(tr("<b>Warning:</b> This deletes all related entries like sensors and lights from the REST API as well."));

            auto *cancelButton = dlg.button(QMessageBox::Cancel);
            cancelButton->setIcon(QIcon());

            auto *deleteButton = dlg.button(QMessageBox::Yes);
            deleteButton->setText(tr("Delete"));
            deleteButton->setIcon(QIcon());

            const auto ret = dlg.exec();

            if (ret == QMessageBox::Yes)
            {
                statusBar()->showMessage(tr("Node %1 (%2) deleted.").arg(nodeName, extAddr));
                deCONZ::controller()->nodeKeyPressed(node->data()->address().ext(), Qt::Key_Delete);
            }
        }
    }
}

void MainWindow::addSourceRouteActionTriggered()
{
    std::vector<zmgNode*> nodes;
    const auto items = ui->graphicsView->scene()->selectedItems();
    zmgNode *coordinator = nullptr;

    for (auto *item : items)
    {
        auto *g = qgraphicsitem_cast<zmgNode*>(item);
        if (g)
        {
            nodes.push_back(g);

            if (g->data()->isCoordinator())
            {
                coordinator = g;
            }
        }
    }

    // scene doesn't return items in selection oder
    std::sort(nodes.begin(), nodes.end(), [](const zmgNode *a, const zmgNode* b) -> bool
    {
        return a->selectionOrder() < b->selectionOrder();
    });

    if (!coordinator || (coordinator != nodes.front() && coordinator != nodes.back()))
    {
        DBG_Printf(DBG_INFO, "coordinator must be selected as first or last node to create a source route");
        return;
    }

    if (coordinator != nodes.front())
    {
        std::reverse(nodes.begin(), nodes.end());
        DBG_Printf(DBG_INFO, "reverse selection order\n");
    }

    for (auto *g : nodes)
    {
        DBG_Printf(DBG_INFO, "%s selection order: %d\n", qPrintable(g->name()), g->selectionOrder());
    }

    m_controller->addSourceRoute(nodes);
}

void MainWindow::removeSourceRouteActionTriggered()
{
    if (ui->graphicsView->scene()->selectedItems().size() != 1)
    {
        return;
    }

    auto *item = ui->graphicsView->scene()->selectedItems().front();
    auto *g = qgraphicsitem_cast<zmgNode*>(item);

    if (g && g->data() && !g->data()->sourceRoutes().empty())
    {
        m_controller->removeSourceRoute(g);
    }
}

void MainWindow::readNodeDescriptorActionTriggered()
{
    for (QGraphicsItem *item : ui->graphicsView->scene()->selectedItems())
    {
        zmgNode *node = qgraphicsitem_cast<zmgNode*>(item);
        if (node && node->data())
        {
            deCONZ::controller()->nodeKeyPressed(node->data()->address().ext(), deCONZ::NodeKeyRequestNodeDescriptor);
        }
    }
}

void MainWindow::readActiveEndpointsActionTriggered()
{
    for (QGraphicsItem *item : ui->graphicsView->scene()->selectedItems())
    {
        zmgNode *node = qgraphicsitem_cast<zmgNode*>(item);
        if (node && node->data())
        {
            deCONZ::controller()->nodeKeyPressed(node->data()->address().ext(), deCONZ::NodeKeyRequestActiveEndpoints);
        }
    }
}

void MainWindow::readSimpleDescriptorsActionTriggered()
{
    for (QGraphicsItem *item : ui->graphicsView->scene()->selectedItems())
    {
        zmgNode *node = qgraphicsitem_cast<zmgNode*>(item);
        if (node && node->data())
        {
            deCONZ::controller()->nodeKeyPressed(node->data()->address().ext(), deCONZ::NodeKeyRequestSimpleDescriptors);
        }
    }
}

void MainWindow::editDDFActionTriggered()
{
    const auto items = ui->graphicsView->scene()->selectedItems();
    for (QGraphicsItem *item : items)
    {
        zmgNode *node = qgraphicsitem_cast<zmgNode*>(item);
        if (node && node->data())
        {
            deCONZ::NodeEvent event(deCONZ::NodeEvent::EditDeviceDDF, node->data());
            emit deCONZ::controller()->nodeEvent(event);
        }
    }
}

void MainWindow::updateNetworkControls()
{
    if (deCONZ::master()->connected())
    {
        m_netConfigAction->setEnabled(true);
        QColor netStateColor = m_netStateLabel->palette().color(QPalette::WindowText);

        switch (deCONZ::master()->netState())
        {
        case deCONZ::NotInNetwork:
            m_netStateLabel->setText(tr("Not In Network"));
            netStateColor = Qt::red;
            m_joinAction->setEnabled(true);
            m_leaveAction->setEnabled(false);
            break;

        case deCONZ::Connecting:
            m_netStateLabel->setText(tr("Joining ..."));
            netStateColor = 0xFF204a87;
            m_joinAction->setEnabled(false);
            m_leaveAction->setEnabled(false);
            break;

        case deCONZ::InNetwork:
            m_netStateLabel->setText(tr("In Network"));
            netStateColor = 0xFF00dd00;
            m_joinAction->setEnabled(false);
            m_leaveAction->setEnabled(true);
            break;

        case deCONZ::Leaving:
            m_netStateLabel->setText(tr("Leaving ..."));
            netStateColor = 0xFF204a87;
            m_joinAction->setEnabled(false);
            m_leaveAction->setEnabled(false);
            break;

        case deCONZ::Touchlink:
            m_netStateLabel->setText(tr("Touchlink"));
            netStateColor = 0xFF204a87;
            m_joinAction->setEnabled(true);
            m_leaveAction->setEnabled(true);
            break;

        default:
            netStateColor = 0xFFff0000;
            m_netStateLabel->setText(tr("Unknown"));
            m_joinAction->setEnabled(false);
            m_leaveAction->setEnabled(false);
            break;
        }

        {
            auto pal = qApp->palette();
            pal.setColor(QPalette::WindowText, netStateColor);
            m_netStateLabel->setForegroundRole(QPalette::WindowText);
            m_netStateLabel->setPalette(pal);
            m_netStateLabel->update();
        }

        setWindowTitle(qApp->applicationName() + " - " + m_devEntry.friendlyName + " (" + m_devEntry.path + ")");
    }
    else
    {
        m_netStateLabel->setText(tr("Not Connected"));

        auto pal = qApp->palette();
        pal.setColor(QPalette::WindowText, Qt::red);
        m_netStateLabel->setForegroundRole(QPalette::WindowText);
        m_netStateLabel->setPalette(pal);
        m_netStateLabel->update();

        m_netConfigAction->setEnabled(false);
        m_leaveAction->setEnabled(false);
        m_joinAction->setEnabled(false);
        setWindowTitle(qApp->applicationName());
    }
}

/*! Handler is called before the application quits.
 */
void MainWindow::appAboutToQuit()
{
    // store configuration
    QSettings config(deCONZ::getStorageLocation(deCONZ::ConfigLocation), QSettings::IniFormat);

    config.setValue("window/state", saveState());
    config.setValue("window/geometry", saveGeometry());
    config.setValue("nodelist/geometry", m_nodeTableView->horizontalHeader()->saveGeometry());
    config.setValue("nodelist/state", m_nodeTableView->horizontalHeader()->saveState());
    config.setValue("nodeview/sceneRect", ui->graphicsView->sceneRect());
    config.setValue("controller/autoFetchFFD", deCONZ::controller()->autoFetchFFD());
    config.setValue("controller/autoFetchRFD", deCONZ::controller()->autoFetchRFD());
    config.setValue("controller/apsAcksEnabled", deCONZ::netEdit()->apsAcksEnabled());
    config.setValue("remote/default/ip", m_remoteIP);
    config.setValue("remote/default/port", m_remotePort);
    config.setValue("discovery/zdp/nwkAddrInterval", deCONZ::getFetchInterval(deCONZ::ReqNwkAddr));
    config.setValue("discovery/zdp/mgmtLqiInterval", deCONZ::getFetchInterval(deCONZ::ReqMgmtLqi));

    config.beginGroup("debug");
    for (int i = 1; i < DBG_END; i++)
    {
        char buf[32];
        const auto dbg = DBG_StringFromItem(i, buf, sizeof(buf));
        if (dbg > 0)
        {
            config.setValue(QLatin1String(buf), DBG_IsEnabled(i));
        }
    }
    config.endGroup();


    QString rundir = deCONZ::getStorageLocation(deCONZ::RuntimeLocation);
    if (!rundir.isEmpty())
    {
        QString pidFile(rundir + QLatin1String("/deconz.pid"));
        if (QFile::exists(pidFile))
        {
            QFile::remove(pidFile);
        }
    }
}

void MainWindow::openWebApp()
{
    quint16 port = deCONZ::controller()->getParameter(deCONZ::ParamHttpPort);

    if (!port)
    {
        statusBar()->showMessage(tr("HTTP server is not running"));
        return;
    }

    QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();

    QList<QNetworkInterface>::Iterator ifi = ifaces.begin();
    QList<QNetworkInterface>::Iterator ifend = ifaces.end();

    for (; ifi != ifend; ++ifi)
    {
        QString name = ifi->humanReadableName();

        // filter
        if (name.contains("vm", Qt::CaseInsensitive) ||
            name.contains("virtual", Qt::CaseInsensitive) ||
            name.contains("loop", Qt::CaseInsensitive))
        {
            continue;
        }

        QList<QNetworkAddressEntry> addr = ifi->addressEntries();

        QList<QNetworkAddressEntry>::Iterator i = addr.begin();
        QList<QNetworkAddressEntry>::Iterator end = addr.end();

        for (; i != end; ++i)
        {
            QHostAddress a = i->ip();

            if (a.protocol() == QAbstractSocket::IPv4Protocol)
            {
                if ((a.toIPv4Address() & 0xFFFF0000ul) == 0xA9FE0000ul)
                {
                    // if the network adapter had problems with DHCP it trys to auto assign IP address
                    // 169.254.0.0 - 169.254.255.255
                    // since these don't work well with WebApp and Browser we filter them out
                    continue;
                }

                quint32 prefix = a.toIPv4Address() & 0xC0000000ul;
                // prefer class B and C networks
                if (prefix == 0xC0000000ul || prefix == 0x80000000ul)
                {
                    QString url = QString("http://%1:%2/login.html").arg(a.toString()).arg(port);
                    QDesktopServices::openUrl(url);
                    return;
                }
            }
        }
    }

    // fallback: localhost
    QString url = QString("http://127.0.0.1:%2/login.html").arg(port);
    QDesktopServices::openUrl(url);
}

void MainWindow::openPhosconApp()
{
    const quint16 port = deCONZ::controller()->getParameter(deCONZ::ParamHttpPort);
    const QString httpRoot = deCONZ::controller()->getParameter(deCONZ::ParamHttpRoot);

    QString urlPath("/pwa/login2.html");
    if (!QFile::exists(httpRoot + urlPath) && QFile::exists(httpRoot + QLatin1String("/login2.html")))
    {
        urlPath = QLatin1String("/login2.html"); // development version
    }

    if (!port)
    {
        statusBar()->showMessage(tr("HTTP server is not running"));
        return;
    }

    for (const auto &ifi : QNetworkInterface::allInterfaces())
    {
        const QString name = ifi.humanReadableName();

        // filter
        if (name.contains("vm", Qt::CaseInsensitive) ||
            name.contains("virtual", Qt::CaseInsensitive) ||
            name.contains("loop", Qt::CaseInsensitive))
        {
            continue;
        }

        for (const auto &addressEntry : ifi.addressEntries())
        {
            const QHostAddress ipAddress = addressEntry.ip();

            if (ipAddress.protocol() == QAbstractSocket::IPv4Protocol)
            {
                if ((ipAddress.toIPv4Address() & 0xFFFF0000ul) == 0xA9FE0000ul)
                {
                    // if the network adapter had problems with DHCP it trys to auto assign IP address
                    // 169.254.0.0 - 169.254.255.255
                    // since these don't work well with WebApp and Browser we filter them out
                    continue;
                }

                const quint32 prefix = ipAddress.toIPv4Address() & 0xC0000000ul;
                // prefer class B and C networks
                if (prefix == 0xC0000000ul || prefix == 0x80000000ul)
                {
                    //const QString url = QString("http://%1:%2%3#host/%1:%2").arg(ipAddress.toString()).arg(port).arg(urlPath);
                    const QString url = QString("http://%1:%2%3").arg(ipAddress.toString()).arg(port).arg(urlPath);
                    QDesktopServices::openUrl(url);
                    return;
                }
            }
        }
    }

    // fallback: localhost
    const QString url = QString("http://127.0.0.1:%1%2").arg(port).arg(urlPath);
    QDesktopServices::openUrl(url);
}

void MainWindow::switchTheme()
{
    QAction *action = qobject_cast<QAction*>(sender());
    if (!action)
        return;

    QString theme = action->data().toString();
    QStyle *fusion = QStyleFactory::create("fusion");

    QString configPath = deCONZ::getStorageLocation(deCONZ::ConfigLocation);
    QSettings config(configPath, QSettings::IniFormat);

    Theme_Activate(theme);
    if (theme == "dark")
    {
        qApp->setStyle(new AStyle(theme, fusion));
        config.setValue("window/theme", theme);
    }
    else if (theme == "light")
    {
        qApp->setStyle(fusion);
        //qApp->setStyle(new AStyle(theme, fusion));
        config.setValue("window/theme", theme);
    }
    else
    {
        U_ASSERT(0 && "unsupported theme");
    }

    QStyle *s = QApplication::style();

    QPixmapCache::clear();

    QPalette pal = qApp->style()->standardPalette();
    // adjust disabled text color (fusion is too low contrast)
    int bri = (pal.windowText().color().lightness() + pal.button().color().lightness()) / 2;
    pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(bri, bri, bri));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor(bri, bri, bri));;
    QApplication::setPalette(pal);

    // Repaint all top-level widgets.
    for (auto* widget : QApplication::allWidgets())
    {
        widget->setPalette(pal);
        s->unpolish(widget);
        s->polish(widget);
        widget->update();
    }

    updateLogo();

    updateNetworkControls(); // sets text color of m_netStateLabel


    ///// hack to update nodes indicator colors
    {
        QList<QGraphicsItem *>items = ui->graphicsView->scene()->items();

        auto i = items.begin();
        auto end = items.end();

        for (; i!= end; ++i)
        {
            zmgNode *g = qgraphicsitem_cast<zmgNode*>(*i);
            if (g)
            {
                g->indicate(deCONZ::IndicateReceive);

            }
        }

    }
    /////

    ui->graphicsView->repaintAll();
}

void MainWindow::resetNodesActionTriggered()
{
    foreach (QGraphicsItem *item, ui->graphicsView->scene()->selectedItems())
    {
        zmgNode *node = qgraphicsitem_cast<zmgNode*>(item);
        if (node && node->data())
        {
            deCONZ::controller()->nodeKeyPressed(node->data()->address().ext(), Qt::Key_Refresh);
        }
    }
}
