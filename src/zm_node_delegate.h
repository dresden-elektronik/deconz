#ifndef ZM_NODE_DELEGATE_H
#define ZM_NODE_DELEGATE_H

#include <QStyledItemDelegate>

class zmgNode;
namespace deCONZ {

class NodeDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit NodeDelegate(QWidget *parent = 0);
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const;
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index);

signals:
    void displayNode(zmgNode *);

public slots:

private:
};

} // namespace deCONZ

#endif // ZM_NODE_DELEGATE_H
