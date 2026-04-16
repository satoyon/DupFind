#pragma once
#include <QAbstractListModel>
#include <QPixmap>
#include <QString>
#include <vector>
#include <unordered_set>
#include "SimilaritySearch.hpp"

struct ResultListItem {
    enum Type { Header, ImageRow, Message };
    Type type;
    int groupId;
    QString headerText;
    std::vector<ImageData> images; // Up to 4 images
};

class ResultListModel : public QAbstractListModel {
    Q_OBJECT

public:
    explicit ResultListModel(QObject *parent = nullptr);
    ~ResultListModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Custom APIs
    void setGroups(const std::vector<DuplicateGroup>& groups, bool preserveState = false);
    const ResultListItem& getItem(int row) const;
    
    bool isChecked(const std::string& path) const;
    void setChecked(const std::string& path, bool state);
    void clearAllChecks();
    void clear();

    const std::unordered_map<std::string, bool>& getCheckStates() const;
    QPixmap getThumbnail(const std::string& path) const;

private:
    void requestThumbnail(const std::string& path) const;
    void emitRowDataChangedForPath(const std::string& path);

    std::vector<ResultListItem> m_items;
    std::unordered_map<std::string, bool> m_checkStates;
    mutable std::unordered_set<std::string> m_loadingPaths;
    mutable std::unordered_map<std::string, QPixmap> m_thumbnails;
};
