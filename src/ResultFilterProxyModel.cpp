#include "ResultFilterProxyModel.hpp"
#include "ResultListModel.hpp"
#include <QString>

ResultFilterProxyModel::ResultFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent) {}

void ResultFilterProxyModel::setSearchText(const QString &searchText) {
  if (m_searchText != searchText) {
    m_searchText = searchText;
    updateVisibleGroups();
    invalidateFilter();
  }
}

void ResultFilterProxyModel::updateVisibleGroups() {
  m_visibleGroupIds.clear();

  if (m_searchText.isEmpty()) {
    return; // Fast path: all visible when no search text
  }

  // Determine which groups have at least one image matching the search text
  ResultListModel *source = qobject_cast<ResultListModel *>(sourceModel());
  if (!source)
    return;

  int rowCount = source->rowCount(QModelIndex());
  for (int row = 0; row < rowCount; ++row) {
    const auto &item = source->getItem(row);
    if (item.type == ResultListItem::ImageRow) {
      if (m_visibleGroupIds.find(item.groupId) != m_visibleGroupIds.end()) {
        continue; // Group already marked as visible
      }

      for (const auto &img : item.images) {
        QString pathStr = QString::fromStdString(img.path);
        if (pathStr.contains(m_searchText, Qt::CaseInsensitive)) {
          m_visibleGroupIds.insert(item.groupId);
          break; // One match is enough for the entire group
        }
      }
    }
  }
}

bool ResultFilterProxyModel::filterAcceptsRow(
    int source_row, const QModelIndex &source_parent) const {
  if (m_searchText.isEmpty()) {
    return true; // Show all if no filter
  }

  ResultListModel *source = qobject_cast<ResultListModel *>(sourceModel());
  if (!source)
    return true;

  const auto &item = source->getItem(source_row);
  return m_visibleGroupIds.find(item.groupId) != m_visibleGroupIds.end();
}
