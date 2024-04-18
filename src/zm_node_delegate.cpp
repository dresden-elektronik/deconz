#include <QPainter>
#include <QFontMetrics>
#include <QVariant>
#include "deconz/types.h"
#include "zm_gnode.h"
#include "zm_node.h"
#include "zm_node_delegate.h"

namespace deCONZ {
const int DevColorWidth = 8;
const int RowSpace = 1;

NodeDelegate::NodeDelegate(QWidget *parent) :
    QStyledItemDelegate(parent)
{
}

void NodeDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
    if (index.data().canConvert<NodeInfo>()) {
#else
    if (qVariantCanConvert<NodeInfo>(index.data())) {
#endif
        painter->save();
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
        NodeInfo node = index.data().value<NodeInfo>();
#else
        NodeInfo node = qVariantValue<NodeInfo>(index.data());
#endif
        if (option.state & QStyle::State_Selected)
        {
            painter->fillRect(option.rect.adjusted(0, RowSpace, 0, -RowSpace), option.palette.highlightedText());
        }
        else
        {
            painter->fillRect(option.rect.adjusted(0, RowSpace, 0, -RowSpace), option.palette.base());
        }

        if (node.data && node.g)
        {
            QRect devRect(option.rect.adjusted(0, RowSpace, 0, -RowSpace));
            devRect.setWidth(DevColorWidth);
            painter->fillRect(devRect, node.g->color());

            QFont fn(option.font);
            QRect textRect = option.rect.adjusted(DevColorWidth + option.fontMetrics.averageCharWidth() * 4, 2, 0, -2);

            fn.setBold(true);
            painter->setFont(fn);
            QString str = "0x" + QString("%1 ").arg(node.data->address().nwk(), 4, 16, QLatin1Char('0')).toUpper();
            painter->drawText(textRect, Qt::AlignTop | Qt::AlignLeft, str);

            fn = option.font;
            painter->setFont(fn);
#if (QT_VERSION < QT_VERSION_CHECK(5, 11, 0))
            painter->drawText(textRect.adjusted(option.fontMetrics.width("0x0000 - "), 0, 0, 0),
                              Qt::AlignTop | Qt::AlignLeft, node.data->userDescriptor());
#else
            painter->drawText(textRect.adjusted(option.fontMetrics.horizontalAdvance("0x0000 - "), 0, 0, 0),
                              Qt::AlignTop | Qt::AlignLeft, node.data->userDescriptor());
#endif
            fn = option.font;
            fn.setPointSize(fn.pointSize() * 0.9);
            painter->setFont(fn);

            switch (node.data->state())
            {
            case deCONZ::BusyState:
                if (node.data->isInWaitState())
                {
                    switch (node.data->getLastError())
                    {
                    case deCONZ::ApsSuccessStatus:
                        str = tr("BUSY");
                        break;

                    default:
                        str = QString("0x%1 %2").arg(quint8(node.data->getLastError()), int(2), int(16), QChar('0'))
                                                .arg(node.data->getLastErrorString());
                        break;
                    }
                }
                break;

            case deCONZ::IdleState:
            case deCONZ::FailureState:
                str = QString("0x%1 %2").arg(quint8(node.data->getLastError()), int(2), int(16), QChar('0'))
                                        .arg(node.data->getLastErrorString());
                break;

            default:
                str = "";
                break;
            }

            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, str);

            str = QString("0x%1").arg(node.data->address().ext(), int(16), int(16), QChar('0'));
            painter->drawText(textRect, Qt::AlignBottom, str);
        }
        painter->restore();
    }
    else
    {
        QStyledItemDelegate::paint(painter, option, index);
    }

}


QSize NodeDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(index);

    QSize size;
    QFontMetrics fm(option.font);

    size.setWidth(fm.averageCharWidth() * 20);
    size.setHeight(fm.height() * 3.25);

    return size;
}


bool NodeDelegate::editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index)
{
    if (event->type() == QEvent::MouseButtonRelease)
    {
        QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent*>(event);
        if (mouseEvent && (mouseEvent->button() == Qt::MiddleButton))
        {
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
            if (index.data().canConvert<NodeInfo>())
#else
            if (qVariantCanConvert<NodeInfo>(index.data()))
#endif
            {
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
                NodeInfo node = index.data().value<NodeInfo>();
#else
                NodeInfo node = qVariantValue<NodeInfo>(index.data());
#endif

                if (node.data && node.g)
                {
                    emit displayNode(node.g);
                }
            }
        }
    }

    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

} // namespace deCONZ
