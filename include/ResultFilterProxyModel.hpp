#pragma once
#include <QSortFilterProxyModel>
#include <unordered_set>
#include <QString>

class ResultFilterProxyModel : public QSortFilterProxyModel {
  Q_OBJECT

public:
  explicit ResultFilterProxyModel(QObject *parent = nullptr);

  // Sets the search term and precalculates visible groups
  void setSearchText(const QString &searchText);

protected:
  // Overrides default filtering to filter by our precalculated group visibility
  bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override;

private:
  QString m_searchText;
  std::unordered_set<int> m_visibleGroupIds;

  // Helper method to scan the underlying model and populate m_visibleGroupIds
  void updateVisibleGroups();
};
