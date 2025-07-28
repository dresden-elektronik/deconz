/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QApplication>
#include <QDebug>
#include <QFontMetrics>
#include <QGraphicsSceneDragDropEvent>
#include <QPainter>
#include <QWidget>
#include <QStyleOptionButton>
#include <QStylePainter>
#include <QDrag>
#include <QMimeData>
#include <QUrlQuery>

#include "deconz/types.h"
#include "gui/theme.h"
#include "zm_controller.h"
#include "zm_gcluster.h"
#include "zm_gendpointbox.h"
#include "deconz/zcl.h"
#include "zm_gsocket.h"

zmgCluster::zmgCluster(zmgEndpointBox *box, QGraphicsItem *parent) :
    QGraphicsItem(parent),
    m_rect(0, 0, 200, 24),
    m_text(QObject::tr("unknown cluster")),
    m_isServer(false),
    m_socket(new NodeSocket(NodeSocket::LookLeft, this)),
    m_attributeCount(0),
    m_box(box)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
}

QRectF zmgCluster::boundingRect() const
{
    return m_rect;
}

void zmgCluster::setUrl(const QUrl &url)
{
    if (url.scheme() != CL_URL_SCHEME)
    {
        m_url = QUrl();
        m_id = 0;
        m_extAddr = 0;
        m_endpoint = 0;
        m_isServer = false;
        m_text.clear();
        m_textId.clear();
        setToolTip("");
        return;
    }

    bool ok = false;
    m_url = url;

    if (urlEndpoint(url, &m_endpoint))
    {
        if (urlExtAddress(url, &m_extAddr))
        {
            if (urlClusterId(url, &m_id))
            {
                m_textId = QString("%1").arg(m_id, 4, 16, QLatin1Char('0')).toUpper();
                QUrlQuery urlq(url);

                if (urlq.hasQueryItem(CL_ITEM_NAME) &&
                    urlq.hasQueryItem(CL_ITEM_CLUSTER_SIDE) )
                {
                    QString side = urlq.queryItemValue(CL_ITEM_CLUSTER_SIDE);

                    if (!side.isEmpty()) {
                        m_isServer = (side[0] == 's');
                    }

                    m_text = urlq.queryItemValue(CL_ITEM_NAME);
                    ok = true;
                }
            }
        }
    }

    if (!ok)
    {
        m_id = 0;
        m_extAddr = 0;
        m_endpoint = 0;
        m_isServer = false;
        m_text.clear();
        m_textId.clear();
    }

    setToolTip(url.toString());
    updateGeometry();
    update();
}

void zmgCluster::setIcon(const QIcon &icon)
{
    m_icon = icon;
}

void zmgCluster::dragEnterEvent(QGraphicsSceneDragDropEvent *event)
{

    if (event->mimeData()->hasUrls())
    {
//        QUrl url = event->mimeData()->urls().first();

//        if (url.queryItemValue(CL_ITEM_EXT_ADDR).toULongLong(0, 16) != m_extAddr)
//        {
//        }
    }

    event->setAccepted(false);
}

void zmgCluster::dragMoveEvent(QGraphicsSceneDragDropEvent *event)
{
    event->accept();
}

void zmgCluster::dropEvent(QGraphicsSceneDragDropEvent *event)
{
    deCONZ::BindReq req;
    const auto urls = event->mimeData()->urls();

    for (const QUrl &url : urls)
    {
        if (urlExtAddress(url, &req.srcAddr))
        {
            if (urlEndpoint(url, &req.srcEndpoint))
            {
                req.unbind = false;
                req.dstExtAddr = m_extAddr;
                req.dstEndpoint = m_endpoint;
                req.clusterId = m_id;
                req.dstAddrMode = 0x03; // ext addressing
                deCONZ::controller()->bindReq(&req);
            }
        }
    }
}

void zmgCluster::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    Q_UNUSED(event)
}

void zmgCluster::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (QLineF(event->screenPos(), event->buttonDownScreenPos(Qt::LeftButton))
            .length() < QApplication::startDragDistance())
    {
        return;
    }

    QDrag *drag = new QDrag(event->widget());
    QMimeData *mime = new QMimeData;
    mime->setUrls({m_url});

    QFontMetrics fm(m_font);

    int w = fm.boundingRect(m_text + m_textId).width() + fm.xHeight() * 2;
    int h = fm.height() + 8;
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);

    QPainter p(&pm);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::white);
    p.setPen(QColor(64,64,64));
    p.drawRoundedRect(QRect(0, 0, w, h), 4, 4);

    const QColor colorServer(18, 64, 171);
    p.setPen(colorServer);
    p.drawText(QRect(4, 0, w, h), Qt::AlignLeft | Qt::AlignVCenter, m_textId);

    p.setPen(Qt::black);
    p.drawText(QRect(0, 0, w - fm.xHeight(), h), Qt::AlignRight | Qt::AlignVCenter, m_text);
    drag->setPixmap(pm);

    /*
    Mime-format: application/vnd.wireshark.displayfilter
    Mime-data: {
      "description": "Source",
      "filter": "zbee_nwk.src == 0x23ff",
      "name": "zbee_nwk.src"
    }

    Mime-format: text/plain
    Mime-data: Source: 0x23ff
*/

    //zbee_aps.cluster == 0x0000
    if (m_box && m_box->node())
    {
        auto *node = m_box->node()->data();
        if (node && node->address().hasNwk())
        {
            QString wsData = QString("{\"filter\":\"zbee_aps.cluster == %1 && (zbee_nwk.src == %2 || zbee_nwk.dst == %2)\", \"name\": \"deCONZ cluster\"}")
            .arg("0x" + QString::number(m_id, 16))
            .arg("0x" + QString::number(node->address().nwk(), 16));
            mime->setData("application/vnd.wireshark.displayfilter", wsData.toUtf8());
        }
    }

    drag->setMimeData(mime);
    drag->exec(Qt::CopyAction);
}

void zmgCluster::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    Q_UNUSED(event)

    m_box->clusterClicked(m_endpoint, m_id, m_isServer ? deCONZ::ServerCluster : deCONZ::ClientCluster);
}

void zmgCluster::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    bool scrollAttr = false;
    const auto pos = mapToItem(this, event->pos());
    const auto geo = boundingRect();

    if (geo.contains(pos) && pos.x() > geo.width() - 32)
    {
        scrollAttr = true;
    }
    m_box->clusterDoubleClicked(scrollAttr);
}

void zmgCluster::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(widget)
    const int averageCharWidth = painter->fontMetrics().averageCharWidth();
    const int textY = static_cast<int>(m_rect.y() + averageCharWidth + painter->fontMetrics().ascent());

    QColor colorDim(102, 102, 102);
    QColor colorLightGray(221, 221, 221);
    QColor colorWhite(255, 255, 255);
    QColor colorServer(18, 64, 171);

    QPalette pal = widget->palette();

    colorServer = Theme_Color(ColorServerCluster);
    colorWhite = pal.color(QPalette::Base);
    colorLightGray = pal.color(QPalette::AlternateBase);

    QColor color = option->state & QStyle::State_MouseOver ?
                colorLightGray :
                colorWhite;

    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawRect(boundingRect().adjusted(2, 0, -2, 0));

    // cluster id
    color = m_isServer ? colorServer :
                         colorDim;
    painter->setPen(QPen(color));
    QFont f = m_font;
    f.setBold(true);
    painter->setFont(f);
    painter->drawText(static_cast<int>(m_rect.x() + averageCharWidth), textY, m_textId);


    // cluster name
    color = pal.color(QPalette::WindowText);

    painter->setPen(QPen(color));
    painter->setFont(m_font);
#if (QT_VERSION < QT_VERSION_CHECK(5, 11, 0))
    painter->drawText(m_rect.x() + averageCharWidth + painter->fontMetrics().width("AAAA BB"),
                      textY, m_text);
#else
    painter->drawText(static_cast<int>(m_rect.x() + averageCharWidth + qreal(painter->fontMetrics().horizontalAdvance("AAAA BB"))),
                      textY, m_text);
#endif

    // cluster attribute count
    color = colorDim;

    painter->setPen(QPen(color));
    painter->setFont(m_font);
#if (QT_VERSION < QT_VERSION_CHECK(5, 11, 0))
    painter->drawText(m_rect.x() + m_rect.width() - painter->fontMetrics().width(" (00) "),
                      textY, QString(QLatin1String("(%1)")).arg(m_attributeCount));
#else
    painter->drawText(static_cast<int>(m_rect.x() + m_rect.width() - qreal(painter->fontMetrics().horizontalAdvance(" (00) "))),
                      textY, QString(QLatin1String("(%1)")).arg(m_attributeCount));
#endif
}

QSizeF zmgCluster::sizeHint(Qt::SizeHint which, const QSizeF &) const
{
    qreal width = 0.0;
    QSizeF size;
    QFontMetrics fm(m_font);

    switch (which)
    {
//    case Qt::MinimumSize:
    case Qt::PreferredSize:
#if (QT_VERSION < QT_VERSION_CHECK(5, 11, 0))
        width += fm.width(m_text + QLatin1String("AAAA BB CC")) + 2 * fm.averageCharWidth(); // text
#else
        width += fm.horizontalAdvance(m_text + QLatin1String("AAAA BB CC")) + 2 * fm.averageCharWidth(); // text
#endif
        size.setWidth(width);
        size.setHeight(fm.height() + 2 * fm.averageCharWidth());
        break;

    default:
        break;
    }

    return size;
}

void zmgCluster::setGeometry(const QRectF &rect)
{
    m_rect = rect;
    m_socket->setPos(rect.x(), rect.y());
    update();
}

bool zmgCluster::urlClusterId(const QUrl &url, quint16 *id)
{
    Q_ASSERT(id);
    QUrlQuery urlq(url);

    if (urlq.hasQueryItem(CL_ITEM_CLUSTER_ID))
    {
        bool ok;
        *id = urlq.queryItemValue(CL_ITEM_CLUSTER_ID).toUShort(&ok, 16);
        if (ok)
            return true;
    }

    return false;
}

bool zmgCluster::urlExtAddress(const QUrl &url, uint64_t *addr)
{
    Q_ASSERT(addr);
    QUrlQuery urlq(url);

    if (urlq.hasQueryItem(CL_ITEM_EXT_ADDR))
    {
        bool ok;
        *addr = urlq.queryItemValue(CL_ITEM_EXT_ADDR).toULongLong(&ok, 16);
        if (ok)
            return true;
    }

    return false;
}

bool zmgCluster::urlEndpoint(const QUrl &url, quint8 *ep)
{
    Q_ASSERT(ep);
    QUrlQuery urlq(url);

    if (urlq.hasQueryItem(CL_ITEM_ENDPOINT))
    {
        bool ok;
        *ep = static_cast<quint8>(urlq.queryItemValue(CL_ITEM_ENDPOINT).toUInt(&ok, 16));
        if (ok)
            return true;
    }

    return false;
}
