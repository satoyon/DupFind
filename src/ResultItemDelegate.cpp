#include "ResultItemDelegate.hpp"
#include "ResultListModel.hpp"
#include <QApplication>
#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <cstddef>

ResultItemDelegate::ResultItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent) {}

void ResultItemDelegate::paint(QPainter *painter,
                               const QStyleOptionViewItem &option,
                               const QModelIndex &index) const {
  const auto *model = qobject_cast<const ResultListModel *>(index.model());
  if (!model)
    return;

  painter->save();

  const auto &item = model->getItem(index.row());
  if (item.type == ResultListItem::Header) {
    QRect r = option.rect;
    r.adjust(0, 5, 0, -5);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setBrush(QColor("#f0f0f0"));
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(r, 4, 4);

    QFont font = painter->font();
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(Qt::black);
    painter->drawText(r.adjusted(5, 0, -5, 0), Qt::AlignVCenter | Qt::AlignLeft,
                      item.headerText);
  } else {
    int cardWidth = option.rect.width() / 4;
    for (int i = 0; i < static_cast<int>(item.images.size()); ++i) {
      const auto &imgData = item.images[i];
      QRect cardRect(option.rect.x() + i * cardWidth, option.rect.y(),
                     cardWidth, option.rect.height());

      // Thumbnail
      QRect thumbRect(cardRect.x() + (cardWidth - 150) / 2, cardRect.y() + 5,
                      150, 150);
      QPixmap pix = model->getThumbnail(imgData.path);
      if (pix.isNull()) {
        painter->setPen(Qt::black);
        painter->drawText(thumbRect, Qt::AlignCenter, "Loading...");
      } else {
        // center pixmap
        QPoint topLeft(thumbRect.center().x() - pix.width() / 2,
                       thumbRect.center().y() - pix.height() / 2);
        painter->drawPixmap(topLeft, pix);
      }

      // Checkbox
      QRect cbRect(cardRect.x() + 5, thumbRect.bottom() + 5, cardWidth - 10,
                   20);
      QStyleOptionButton cbOpt;
      cbOpt.rect = cbRect;
      cbOpt.text = "Delete candidate";
      cbOpt.state = QStyle::State_Enabled;
      if (model->isChecked(imgData.path)) {
        cbOpt.state |= QStyle::State_On;
      } else {
        cbOpt.state |= QStyle::State_Off;
      }

      QApplication::style()->drawControl(QStyle::CE_CheckBox, &cbOpt, painter);

      // Info text
      QRect textRect(cardRect.x() + 5, cbRect.bottom() + 2, cardWidth - 10,
                     cardRect.bottom() - cbRect.bottom() - 2);
      QString info = QString("%1 KB\n%2")
                         .arg(imgData.file_size / 1024)
                         .arg(QString::fromStdString(imgData.path));

      QFont font = painter->font();
      font.setPixelSize(10);
      painter->setFont(font);
      painter->setPen(QColor("#666666"));
      painter->drawText(textRect,
                        Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, info);
    }
  }

  painter->restore();
}

QSize ResultItemDelegate::sizeHint(const QStyleOptionViewItem &option,
                                   const QModelIndex &index) const {
  const auto *model = qobject_cast<const ResultListModel *>(index.model());
  if (!model)
    return QSize(0, 0);
  const auto &item = model->getItem(index.row());
  if (item.type == ResultListItem::Header) {
    return QSize(option.rect.width(), 40);
  } else {
    return QSize(option.rect.width(), 220);
  }
}

bool ResultItemDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                     const QStyleOptionViewItem &option,
                                     const QModelIndex &index) {
  if (!event)
    return false;

  auto *listModel = qobject_cast<ResultListModel *>(model);
  if (!listModel)
    return false;

  const auto &item = listModel->getItem(index.row());
  if (item.type != ResultListItem::ImageRow)
    return false;

  if (event->type() == QEvent::MouseButtonRelease) {
    QMouseEvent *me = static_cast<QMouseEvent *>(event);
    int cardWidth = option.rect.width() / 4;
    for (int i = 0; i < static_cast<int>(item.images.size()); ++i) {
      const auto &imgData = item.images[i];
      QRect cardRect(option.rect.x() + i * cardWidth, option.rect.y(),
                     cardWidth, option.rect.height());

      if (cardRect.contains(me->pos())) {
        if (me->button() == Qt::RightButton) {
          emit contextMenuRequested(imgData.path, item.groupId,
                                    me->globalPosition().toPoint());
          return true;
        } else if (me->button() == Qt::LeftButton) {
          QRect thumbRect(cardRect.x() + (cardWidth - 150) / 2,
                          cardRect.y() + 5, 150, 150);
          QRect cbRect(cardRect.x() + 5, thumbRect.bottom() + 5, cardWidth - 10,
                       20);
          if (cbRect.contains(me->pos())) {
            bool currentState = listModel->isChecked(imgData.path);
            listModel->setChecked(imgData.path, !currentState);
            return true;
          }
        }
      }
    }
  } else if (event->type() == QEvent::MouseButtonDblClick) {
    QMouseEvent *me = static_cast<QMouseEvent *>(event);
    int cardWidth = option.rect.width() / 4;
    for (int i = 0; i < static_cast<int>(item.images.size()); ++i) {
      const auto &imgData = item.images[i];
      QRect cardRect(option.rect.x() + i * cardWidth, option.rect.y(),
                     cardWidth, option.rect.height());
      if (cardRect.contains(me->pos())) {
        emit fileDoubleClicked(imgData.path);
        return true;
      }
    }
  }

  return QStyledItemDelegate::editorEvent(event, model, option, index);
}

bool ResultItemDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view,
                                   const QStyleOptionViewItem &option,
                                   const QModelIndex &index) {
  if (!event || !view || !index.isValid())
    return false;

  if (event->type() == QEvent::ToolTip) {
    const auto *listModel =
        qobject_cast<const ResultListModel *>(index.model());
    if (!listModel)
      return false;

    const auto &item = listModel->getItem(index.row());
    if (item.type != ResultListItem::ImageRow)
      return false;

    int cardWidth = option.rect.width() / 4;
    for (int i = 0; i < static_cast<int>(item.images.size()); ++i) {
      const auto &imgData = item.images[i];
      QRect cardRect(option.rect.x() + i * cardWidth, option.rect.y(),
                     cardWidth, option.rect.height());
      if (cardRect.contains(event->pos())) {
        QFileInfo fileInfo(QString::fromStdString(imgData.path));
        QString toolTip = fileInfo.fileName();
        QToolTip::showText(event->globalPos(), toolTip, nullptr, cardRect);
        return true;
      }
    }
  }

  return QStyledItemDelegate::helpEvent(event, view, option, index);
}
