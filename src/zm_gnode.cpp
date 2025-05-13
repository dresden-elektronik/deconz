/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QWindow>
#include <QStyleOption>
#include <QStylePainter>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsScene>

#include "actor/plugin.h"
#include <deconz/am_gui.h>
#include <deconz/dbg_trace.h>
#include <deconz/timeref.h>
#include "gui/gnode_link_group.h"
#include "zm_app.h"
#include "zm_node.h"
#include "zm_gcluster.h"
#include "zm_gendpointbox.h"
#include "zm_gnode.h"
#include "zm_glink.h"
#include "zm_gsocket.h"

#define NODE_COLOR         239, 239, 239
#define NODE_COLOR_DARK    180, 180, 180
#define NODE_COLOR_BRIGHT  240, 240, 240

extern void NV_AddNodeIndicator(void *user, int runs); // defined in zm_graphicsview.cpp

static const int NamePad = 64;
static const int NamePointSize = 10;
static const int MacPointSize = 8;
static int SelectionOrderCounter = 0;
static const int TogglePad = 10;
static const int ToggleSize = 20;
static const int IndGeneralInterval = 400;
static const int IndGeneralCount = 5;
static const int IndDataUpdateInterval = 400;
static const int IndDataUpdateCount = 5;
static am_actor am_actor_gui_node;
static uint16_t msgNotifyTag;

extern deCONZ::SteadyTimeRef m_steadyTimeRef; // in zmController TODO proper interface

struct IndicationDef
{
    quint16 interval;
    quint8 count;
    quint8 resetColor;
    QColor colorHi;
    QColor colorLo;
};

static int GuiNode_MessageCallback(struct am_message *msg)
{
    DBG_Printf(DBG_INFO, "gui/node: msg from: %u\n", msg->src);
    return AM_CB_STATUS_UNSUPPORTED;
}

void GUI_InitNodeActor()
{
    am_api_functions *am = GUI_GetActorModelApi();

    AM_INIT_ACTOR(&am_actor_gui_node, AM_ACTOR_ID_GUI_NODE, GuiNode_MessageCallback);

    am->register_actor(&am_actor_gui_node);
}

void SendNotifyMessage1(am_msg_id msgid, am_u64 extaddr)
{
    am_api_functions *am = GUI_GetActorModelApi();

    am_message *m = am->msg_alloc();
    if (m)
    {
        m->id = msgid;
        m->src = AM_ACTOR_ID_GUI_NODE;
        m->dst = AM_ACTOR_ID_SUBSCRIBERS;
        am->msg_put_u16(m, msgNotifyTag++);
        am->msg_put_u64(m, extaddr);

        am->send_message(m);
    }
}

void SendNotifyMessageMoved(am_u64 extaddr, const QPointF &pos)
{
    am_api_functions *am = GUI_GetActorModelApi();

    am_message *m = am->msg_alloc();
    if (m)
    {
        m->id = M_ID_GUI_NODE_MOVED;
        m->src = AM_ACTOR_ID_GUI_NODE;
        m->dst = AM_ACTOR_ID_SUBSCRIBERS;
        am->msg_put_u16(m, msgNotifyTag++);
        am->msg_put_u64(m, extaddr);
        am->msg_put_s32(m, (am_s32)(pos.x() * 1000.0));
        am->msg_put_s32(m, (am_s32)(pos.y() * 1000.0));

        am->send_message(m);
    }
}

void SendNotifyMessageKeyPressed(am_u64 extaddr, am_s32 key)
{
    am_api_functions *am = GUI_GetActorModelApi();

    am_message *m = am->msg_alloc();
    if (m)
    {
        m->id = M_ID_GUI_NODE_KEY_PRESSED;
        m->src = AM_ACTOR_ID_GUI_NODE;
        m->dst = AM_ACTOR_ID_SUBSCRIBERS;
        am->msg_put_u16(m, msgNotifyTag++);
        am->msg_put_u64(m, extaddr);
        am->msg_put_s32(m, key);

        am->send_message(m);
    }
}

zmgNode::zmgNode(deCONZ::zmNode *data, QGraphicsItem *parent) :
    QGraphicsObject(parent),
    m_data(data),
    m_epDropDownVisible(false),
    m_configVisible(false),
    m_needSaveToDatabase(false)
{
    m_height = 32;
    m_width = 160;

    setCursor(Qt::ArrowCursor);

    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsFocusable, true);
    //setFlag(QGraphicsItem::ItemClipsChildrenToShape, true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);

    m_sockets[NeighborSocket] = new NodeSocket(NodeSocket::LookLeft, this);
    m_sockets[DataSocket] = new NodeSocket(NodeSocket::LookLeft, this);

    m_epBox = new zmgEndpointBox(this);
    m_epBox->moveBy(0, m_height + 2);
    m_epBox->setVisible(m_epDropDownVisible);

    m_indRect = QRectF(20, 8, 10, 10);
    m_indCount = -1;

    setZValue(0.1);

    m_indicator = new QGraphicsEllipseItem(m_indRect, this);
    m_indicator->setPen(QColor(NODE_COLOR_BRIGHT));
    m_indicator->setBrush(QColor(NODE_COLOR_DARK));
}

zmgNode::~zmgNode()
{
}

QRectF zmgNode::boundingRect() const
{
    qreal ol = 1.0; // outline
    return QRectF(-ol, -ol, m_width + ol, m_height + ol);
}

void zmgNode::toggleEndpointDropdown()
{
    if (m_epBox->endpointSize() > 0)
    {
        m_epDropDownVisible = !m_epDropDownVisible;
        m_epBox->setVisible(m_epDropDownVisible);

        for (int i = 0; i < linkCount(); i++)
        {
            NodeLink *lnk = link(i);
            if (lnk && lnk->linkType() == NodeLink::LinkBinding)
            {
                if (lnk->src() && lnk->src()->isVisible() && lnk->dst() && lnk->dst()->isVisible())
                {
                    lnk->setVisible(true);
                    lnk->updatePosition();
                }
                else
                {
                    lnk->hide();
                }
            }
       }
    }
    else
    {
        m_epDropDownVisible = false;
        m_epBox->setVisible(m_epDropDownVisible);

        for (int i = 0; i < linkCount(); i++)
        {
            NodeLink *lnk = link(i);
            if (lnk && lnk->linkType() == NodeLink::LinkBinding)
            {
                if (lnk->src()->isVisible() && lnk->dst()->isVisible())
                {
                    lnk->hide();
                }
            }
       }
    }

    m_dirty = true;
    checkVisible();
}

// TODO(mpi): remove this
void zmgNode::toggleConfigDropdown()
{
    m_configVisible = !m_configVisible;
    checkVisible();
}

void zmgNode::updated(deCONZ::RequestId id)
{
    switch (id)
    {
    case deCONZ::ReqSimpleDescriptor:
    {
        bool wasVisible = m_epDropDownVisible;

        if (m_epDropDownVisible)
        {
            toggleEndpointDropdown();
        }

        m_epBox->updateEndpoints(this);

        if (wasVisible)
        {
            if (!data()->simpleDescriptors().empty())
            {
                toggleEndpointDropdown();
            }
        }

        m_dirty = true;
        requestUpdate();
    }
        break;

    case deCONZ::ReqUserDescriptor:
    default:
        requestUpdate();
        break;
    }
}

void zmgNode::indicate(deCONZ::Indication type)
{
    if (gHeadlessVersion)
    {
        return;
    }

    QWindow *win = QGuiApplication::focusWindow();

    if (!win || !win->isExposed() || !win->isActive())
    {
        return;
    }

    Q_ASSERT(type < 6);

    static const IndicationDef indicationDef[] = {
         {0, 0, 1, QColor(NODE_COLOR_DARK), QColor(NODE_COLOR_DARK) }, // None
         {IndGeneralInterval, IndGeneralCount, 1, QColor(Qt::green), QColor(NODE_COLOR_DARK) }, // Receive
         {IndGeneralInterval, IndGeneralCount, 0, QColor(Qt::yellow), QColor(Qt::yellow) }, // Send
         {IndDataUpdateInterval, IndDataUpdateCount, 1, QColor(30, 60, 200), QColor(NODE_COLOR_DARK) }, // Send Done
         {IndDataUpdateInterval, IndDataUpdateCount, 1, QColor(30, 60, 200), QColor(NODE_COLOR_DARK) }, // Data update
         {IndGeneralInterval, IndGeneralCount, 1, QColor(Qt::red), QColor(NODE_COLOR_DARK) }, // Error
    };

    m_indDef = &indicationDef[type];
    m_indCount = m_indDef->count;

    NV_AddNodeIndicator(this, m_indCount);
}

void zmgNode::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    if (gHeadlessVersion)
    {
        return;
    }

    Q_UNUSED(widget)

    static const QColor nodeColor(NODE_COLOR);
    static const QColor nodeColorSelected = nodeColor.lighter(104);
    static const QColor nodeColorNeutral(160, 160, 160);
    static const QColor colorCoordinator(0, 132, 209);
    static const QColor colorRouterDead(240, 190, 15);
    static const QColor colorRouter(255, 211, 32);
    static const QColor colorOtau(120, 250, 100);
    static const QColor nodeShadowColor(165, 165, 165);
    static const QColor colorToggleBackground(240, 240, 240);
    static const QColor colorInset(140, 140, 140);
    static const QColor colorInsetDark(100, 100, 100);

    QPainter &p = *painter;

    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor *m_color = &nodeColorNeutral;

    const int rheight = option->rect.height();
    const int tooOld = 60 * 1000 * 30; // 30 minutes
    qint64 ageSeconds = tooOld;

    if (m_data)
    {
        ageSeconds = (m_steadyTimeRef.ref - m_lastSeen) / 1000;

        if (isValid(m_otauActiveTime))
        {
            const deCONZ::TimeMs dt = m_steadyTimeRef - m_otauActiveTime;
            if (dt < deCONZ::TimeMs{5000})
            {
                m_color = &colorOtau;
            }
            else
            {
                m_otauActiveTime = {};
            }
        }
        else if (m_deviceType == deCONZ::Coordinator)
        {
            m_color = &colorCoordinator;
        }
        else if (m_deviceType == deCONZ::Router)
        {
            if (m_isZombie || m_data->state() == deCONZ::FailureState || (ageSeconds >= tooOld))
            {
                m_color = &colorRouterDead;
            }
            else
            {
                m_color = &colorRouter;
            }
        }
        else if (m_deviceType == deCONZ::EndDevice)
        {
            m_color = &nodeColorNeutral;
        }
    }

    // fake shadow
    qreal roundBorder = 2;
    qreal shadowY = -1;

    p.setBrush(nodeShadowColor);
    p.setPen(QPen(nodeShadowColor, 1.8));
    p.drawRoundedRect(QRectF(option->rect).adjusted(1.5, 1.5, -1, -1), roundBorder, roundBorder);

    // surface
    qreal inset = 1;
    if (option->state & QStyle::State_Selected)
    {
        p.setBrush(nodeColorSelected);
        //inset = 0;
        p.setPen(QPen(QColor(0, 80, 250), 2));
    }
    else
    {
        p.setBrush(nodeColor);
        p.setPen(Qt::NoPen);
    }

    p.drawRoundedRect(option->rect.adjusted(inset, inset, -inset, -inset), roundBorder, roundBorder);

    p.setClipRect(0, 0, 16, 100);
    p.setBrush(*m_color);
    p.drawRoundedRect(option->rect.adjusted(inset, inset, -inset, -inset), roundBorder, roundBorder);
    p.setClipping(false);

    if ((option->state & QStyle::State_Selected) == 0)
    {
        QRect rect = option->rect;

        QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
        gradient.setColorAt(0, QColor(255, 255, 255, 96));
        gradient.setColorAt(1, QColor(130, 130, 130, 64));

        p.setPen(QPen(gradient, 0.75));
        p.setBrush(Qt::NoBrush);

        p.drawRoundedRect(rect.adjusted(inset + 1.0, inset, -inset, -inset), roundBorder, roundBorder);
    }

    // endpoint checkbox subcontrol
    p.setPen(Qt::NoPen);
    if (!data()->simpleDescriptors().empty())
    {
//        p.setPen(QPen(colorInset, 1.0));
//        p.setBrush(colorToggleBackground);
//        p.drawRoundedRect(m_endpointToggle, 3.0, 3.0);

        QRectF r = m_endpointToggle;
        qreal pad = 3.0;
        qreal subt = (r.height() / 2.0) - 1.4;

        const qreal round = 1.0;

        p.setPen(Qt::NoPen);
        // shade
        p.setBrush(colorInsetDark);
        p.drawRoundedRect(r.adjusted(pad, subt, -pad, -subt), round, round);

        if (!m_epDropDownVisible)
        {
            p.drawRoundedRect(r.adjusted(subt, pad, -subt, -pad), round, round);
        }

        // inner
        pad = 4.0;
        subt += 1.0;
        p.setBrush(colorInset);
        p.drawRect(r.adjusted(pad, subt, -pad, -subt));

        if (!m_epDropDownVisible)
        {
            p.drawRect(r.adjusted(subt, pad, -subt, -pad));
        }
    }

    QFont fn;
    fn.setPointSize(NamePointSize);
    fn.setWeight(QFont::Bold);
    p.setFont(fn);

    QFontMetrics fm(fn);

    // NWK address | Userdescriptor
    static const QColor textColorDark(20, 20, 20);
    static const QColor textColorDim(80, 80, 80);

    p.setPen(QPen(textColorDark, 2));

    if (ageSeconds >= tooOld)
    {
        p.setPen(QPen(textColorDim, 2));
    }
    else
    {
        p.setPen(QPen(textColorDark, 2));
    }

    QRect rectName = option->rect.adjusted(NamePad, fm.capHeight() * 3 / 8, -2 * ToggleSize, 0);
    rectName.setHeight(fm.capHeight() * 2);

    // p.fillRect(rectName, Qt::cyan);
    p.drawText(rectName, Qt::AlignVCenter, m_name);

    if (m_hasDDF != 0)
    {
        fn.setPointSize(8);
        fn.setBold(false);
        p.setFont(fn);
        p.setPen(QPen(QColor(50, 50, 50), 2));
        if (m_hasDDF == 1)
        {
            p.drawText(rectName, Qt::AlignVCenter | Qt::AlignRight, QLatin1String("DDF"));
        }
        else if (m_hasDDF == 2)
        {
            p.drawText(rectName, Qt::AlignVCenter | Qt::AlignRight, QLatin1String("DDB"));
        }
    }

    // IEEE address
    fn.setFamily(QLatin1String("monospace"));
    fn.setBold(false);
    fn.setPointSize(MacPointSize);
    p.setFont(fn);
    fm = QFontMetrics(fn);
    p.setPen(QPen(QColor(50, 50, 50), 2));
    if (m_extAddress.isEmpty())
    {
        m_extAddress = QString(QLatin1String("%1")).arg(m_extAddressCache, 16, 16, QLatin1Char('0')).toUpper();
    }

    QRect macRect = option->rect.adjusted(m_indRect.x() + m_indRect.width() + 4, option->rect.height() / 2, 0, 0);
    // p.fillRect(macRect, Qt::yellow);

    p.drawText(macRect, Qt::AlignVCenter, m_extAddress);

    if (!m_pm.isNull())
    {
        qreal x = m_indRect.x() + m_indRect.width();
        p.drawPixmap(x + 4, m_indRect.y() - m_indRect.height() + 3, m_pm);
    }
}

NodeSocket *zmgNode::socket(quint8 endpoint, quint16 cluster, deCONZ::ZclClusterSide side)
{
    if (m_epBox)
    {
        zmgCluster *cl = m_epBox->getCluster(endpoint, cluster, side);
        if (cl)
        {
            return cl->socket();
        }
    }

    return 0;
}

QVariant zmgNode::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == QGraphicsItem::ItemPositionChange)
    {
        m_needSaveToDatabase = true;
        if (m_moveWatcher == 0)
        {

            NodeLinkGroup::setRenderQuality(NodeLinkGroup::RenderQualityFast);
            m_moveWatcher = 2;
        }

        // TODO(mpi): The moved signaled is now only used by UI source routes.
        // The UI source routes and NodeLink need to be brought into GUI layer.
        emit moved();
        updateLinks();
    }
    else if (change == QGraphicsItem::ItemSelectedHasChanged)
    {
        if (value.toBool())
        {
            m_selectionCounter = ++SelectionOrderCounter;
            SendNotifyMessage1(M_ID_GUI_NODE_SELECTED, m_extAddressCache);
        }
        else
        {
            SendNotifyMessage1(M_ID_GUI_NODE_DESELECTED, m_extAddressCache);
            m_selectionCounter = -1;
        }
    }

    return QGraphicsObject::itemChange(change, value);
}

void zmgNode::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    checkVisible();
    requestUpdate();
    QGraphicsObject::mousePressEvent(event);

    if (event->button() == Qt::RightButton)
    {
        if (!isSelected())
        {
            setSelected(true); // does also emit selected message in itemChange()
        }

        SendNotifyMessage1(M_ID_GUI_NODE_CONTEXT_MENU, m_extAddressCache);
    }

    m_moveWatcher = 0;
}

void zmgNode::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);

    if (m_moveWatcher > 0)
    {
        m_moveWatcher = -1;
        NodeLinkGroup::setRenderQuality(NodeLinkGroup::RenderQualityHigh);

        SendNotifyMessageMoved(m_extAddressCache, pos());
    }

    if (m_endpointToggle.contains(event->pos()))
    {
        if (m_configVisible)
        {
            toggleConfigDropdown();
        }
        toggleEndpointDropdown();
    }
    else
    {
        checkVisible();
    }

    requestUpdate();

    QGraphicsObject::mouseReleaseEvent(event);
}

void zmgNode::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    Q_UNUSED(event);

    toggleEndpointDropdown();
}

void zmgNode::keyPressEvent(QKeyEvent *event)
{
    switch (event->key())
    {
    case Qt::Key_Refresh:
    case Qt::Key_Delete:
        break;

    default:
        {
            SendNotifyMessageKeyPressed(m_extAddressCache, (am_s32)event->key());
        }
        break;
    }
}

// update links -- curves
void zmgNode::updateLinks()
{
    for (NodeLink *link : m_links)
    {
        updateLink(link);
    }

    // TODO only lines bounding rect?
    if (scene())
        scene()->invalidate(sceneBoundingRect(), QGraphicsScene::BackgroundLayer);
}

void zmgNode::updateLink(NodeLink *link)
{
    Q_ASSERT(link);
    if (link && link->isVisible())
    {
        link->updatePosition();
    }
}

// TODO wip zmgNode should not know anything about deCONZ::zmNode
void zmgNode::updateParameters()
{
    if (m_data) // TODO set from outside
    {
        m_isZombie = m_data->isZombie();
    }

    if (!m_name.isEmpty())
    {
        QFont fn;
        fn.setPointSize(NamePointSize);
        fn.setWeight(QFont::Bold);
        QFontMetrics fm(fn);

        QString placeHolder = m_name + "DDF_M";

        QRect bb = fm.boundingRect(placeHolder);
        int w = bb.width();
        w += NamePad + ToggleSize;
        int h = bb.height() * 2.4;

        if (w < 220)
            w = 220;

        if (h < 42)
            h = 42;

        if (m_width != w || m_height != h)
        {
            prepareGeometryChange();
            m_width = w;
            m_height = h;
            m_epBox->setPos(0, m_height + 2);
        }
    }

    int y = (boundingRect().height() - ToggleSize) / 2 + 1;

    m_endpointToggle = QRectF(boundingRect().width() - ToggleSize - TogglePad, y,
                      ToggleSize, ToggleSize);
}

bool zmgNode::needSaveToDatabase() const
{
    return m_needSaveToDatabase;
}

void zmgNode::setBattery(int battery)
{
    if (battery != m_battery)
    {
        if (battery >= 0 && battery <= 100)
        {
            m_battery = battery;
            if (battery <= 20)
            {
                m_pm = QPixmap(QLatin1String(":/icons/faenza/gpm-primary-020.png"));
            }
            else if (battery <= 40)
            {
                m_pm = QPixmap(QLatin1String(":/icons/faenza/gpm-primary-040.png"));
            }
            else if (battery <= 60)
            {
                m_pm = QPixmap(QLatin1String(":/icons/faenza/gpm-primary-060.png"));
            }
            else
            {
                m_pm = QPixmap(QLatin1String(":/icons/faenza/gpm-primary-100.png"));
            }

            m_pm = m_pm.scaledToHeight(26);
        }
        else
        {
            m_battery = -1;
        }

        m_dirty = true;
    }
}

void zmgNode::setNeedSaveToDatabase(bool needSave)
{
    m_needSaveToDatabase = needSave;
}

void zmgNode::addLink(NodeLink *link)
{
    const auto i = std::find(m_links.cbegin(), m_links.cend(), link);
    if (i == m_links.cend())
    {
        m_links.push_back(link);

        if (link->linkType() == NodeLink::LinkBinding)
        {
            if (!m_epDropDownVisible && link->isVisible())
            {
                link->setVisible(false);
            }
        }
    }
}

void NV_IndicatorCallback(void *user)
{
    zmgNode *node = static_cast<zmgNode*>(user);

    if (node)
    {
        node->indicationTick();
    }
}

void zmgNode::indicationTick()
{
    m_indCount--;
    if (m_indDef)
    {
#if 1 /* TEMP */
        if (m_indCount >= 0)
        {
            if (m_indCount & 1)
            {
                m_indicator->setBrush(m_indDef->colorHi);
            }
            else
            {
                m_indicator->setBrush(m_indDef->colorLo);
            }
        }
        else
        {
            if (m_indDef->resetColor)
            {
                m_indicator->setBrush(QColor(NODE_COLOR_DARK));
            }
            else
            {
                m_indicator->setBrush(m_indDef->colorLo);
            }
        }
#endif
    }
    if (m_indCount <= 0)
    {
        m_indCount = -1;
    }
}

void zmgNode::remLink(NodeLink *link)
{
    auto i = std::find(m_links.begin(), m_links.end(), link);
    if (i != m_links.end())
    {
        m_links.erase(i);
    }
}

bool zmgNode::hasLink(NodeLink *link)
{
    return std::find(m_links.cbegin(), m_links.cend(), link) != m_links.cend();
}

bool zmgNode::ownsSocket(NodeSocket *socket) const
{
    const auto i = std::find(m_sockets.cbegin(), m_sockets.cend(), socket);

    if (i == m_sockets.cend())
        return false;

    return true;
}

void zmgNode::checkVisible()
{
    qreal maxz = zValue();
    auto items = collidingItems();

    if (m_epDropDownVisible)
    {
        items.append(m_epBox->collidingItems());
    }

    for (QGraphicsItem *item : items)
    {
        maxz = qMax(maxz, item->zValue());
    }

    setZValue(maxz + 0.1);
}

NodeLink *zmgNode::link(int i)
{
    if (m_links.size() < size_t(i))
    {
        return m_links[i];
    }

    return nullptr;
}

void zmgNode::requestUpdate()
{
    if (m_dirty)
    {
        updateParameters();
        m_dirty = false;
    }
    update();
}

void zmgNode::setName(const QString &name)
{
    if (!name.isEmpty())
    {
        if (m_name != name)
        {
            m_name = name;
            m_dirty = true;
        }
    }
    else
    {
        m_name = "0x" + QString("%1").arg(m_nwkAddressCache, 4, 16, QLatin1Char('0')).toUpper();
        m_dirty = true;
    }
}

void zmgNode::setAddress(quint16 nwk, quint64 mac)
{
    if (nwk != m_nwkAddressCache)
    {
        m_nwkAddressCache = nwk;

        if (m_name.isEmpty() || (m_name.size() == 6 && m_name.startsWith("0x")))
        {
            setName(QString()); // ugly, but force refresh NWK address as name
        }
        m_dirty = true;
    }

    if (mac != m_extAddressCache)
    {
        m_extAddress.clear();
        m_extAddressCache = mac;
        m_dirty = true;
    }
}

void zmgNode::setDeviceType(deCONZ::DeviceType type)
{
    if (m_deviceType != type)
    {
        m_deviceType = type;
        m_dirty = true;
    }
}

void zmgNode::setLastSeen(qint64 lastSeen)
{
    if (m_lastSeen != lastSeen)
    {
        m_lastSeen = lastSeen;
    }
}

void zmgNode::setHasDDF(int hasDDF)
{
    if (m_hasDDF != hasDDF)
    {
        m_hasDDF = hasDDF;
        update();
    }
}
