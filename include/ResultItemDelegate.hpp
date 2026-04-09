#pragma once
#include <QStyledItemDelegate>

class ResultItemDelegate : public QStyledItemDelegate {
  Q_OBJECT

public:
  explicit ResultItemDelegate(QObject *parent = nullptr);

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override;
  QSize sizeHint(const QStyleOptionViewItem &option,
                 const QModelIndex &index) const override;
  bool editorEvent(QEvent *event, QAbstractItemModel *model,
                   const QStyleOptionViewItem &option,
                   const QModelIndex &index) override;
  bool helpEvent(QHelpEvent *event, QAbstractItemView *view,
                 const QStyleOptionViewItem &option,
                 const QModelIndex &index) override;

signals:
  void contextMenuRequested(const std::string &path, int groupId,
                            const QPoint &globalPos);
  void fileDoubleClicked(const std::string &path);
};
