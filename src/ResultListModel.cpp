#include "ResultListModel.hpp"
#include "ImageHasher.hpp"
#include <QImageReader>
#include <QtConcurrent>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>

ResultListModel::ResultListModel(QObject *parent) : QAbstractListModel(parent) {
}

ResultListModel::~ResultListModel() {
}

int ResultListModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_items.size();
}

QVariant ResultListModel::data(const QModelIndex &index, int role) const {
    // Custom roles not strictly used since Delegate casts and reads data manually.
    if (!index.isValid() || index.row() >= m_items.size()) return QVariant();
    return QVariant();
}

const ResultListItem& ResultListModel::getItem(int row) const {
    return m_items[row];
}

void ResultListModel::setGroups(const std::vector<DuplicateGroup>& groups, bool preserveState) {
    beginResetModel();
    
    std::unordered_map<std::string, bool> oldState;
    if (preserveState) {
        oldState = m_checkStates;
    }
    m_checkStates.clear();
    m_items.clear();
    
    int groupId = 0;
    if (groups.empty()) {
        ResultListItem msg;
        msg.type = ResultListItem::Message;
        msg.headerText = tr("No similar images found.");
        m_items.push_back(msg);
    } else {
        for (const auto& group : groups) {
            if (group.images.empty()) continue;

            const ImageData* bestImage = &group.images[0];
            for (size_t i = 1; i < group.images.size(); ++i) {
                if (group.images[i].file_size > bestImage->file_size) {
                    bestImage = &group.images[i];
                }
            }

            for (const auto& img : group.images) {
                if (preserveState && oldState.find(img.path) != oldState.end()) {
                    m_checkStates[img.path] = oldState[img.path];
                } else {
                    m_checkStates[img.path] = (&img != bestImage);
                }
            }

            ResultListItem header;
            header.type = ResultListItem::Header;
            header.groupId = groupId;
            header.headerText = tr("Duplicate Group - %1 images").arg(group.images.size());
            m_items.push_back(header);
            
            ResultListItem rowItem;
            rowItem.type = ResultListItem::ImageRow;
            rowItem.groupId = groupId;
            for (const auto& img : group.images) {
                rowItem.images.push_back(img);
                if (rowItem.images.size() == 4) {
                    m_items.push_back(rowItem);
                    rowItem.images.clear();
                }
            }
            if (!rowItem.images.empty()) {
                m_items.push_back(rowItem);
            }
            groupId++;
        }
    }
    endResetModel();
}

bool ResultListModel::isChecked(const std::string& path) const {
    auto it = m_checkStates.find(path);
    if (it != m_checkStates.end()) return it->second;
    return false;
}

void ResultListModel::setChecked(const std::string& path, bool state) {
    if (m_checkStates[path] != state) {
        m_checkStates[path] = state;
        emitRowDataChangedForPath(path);
    }
}

void ResultListModel::clearAllChecks() {
    for (auto& pair : m_checkStates) {
        pair.second = false;
    }
    if (!m_items.empty()) {
        emit dataChanged(index(0, 0), index(m_items.size()-1, 0));
    }
}

void ResultListModel::clear() {
    beginResetModel();
    m_items.clear();
    m_checkStates.clear();
    m_thumbnails.clear();
    m_loadingPaths.clear();
    endResetModel();
}

const std::unordered_map<std::string, bool>& ResultListModel::getCheckStates() const {
    return m_checkStates;
}

QPixmap ResultListModel::getThumbnail(const std::string& path) const {
    auto it = m_thumbnails.find(path);
    if (it != m_thumbnails.end()) return it->second;
    
    requestThumbnail(path);
    return QPixmap();
}

void ResultListModel::addThumbnail(const std::string& path, const QImage& image) {
    if (!image.isNull()) {
        m_thumbnails[path] = QPixmap::fromImage(image).scaled(150, 150, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        emitRowDataChangedForPath(path);
    }
}

void ResultListModel::requestThumbnail(const std::string& path) const {
    if (m_loadingPaths.contains(path)) return;
    
    m_loadingPaths.insert(path);
    QString qpath = QString::fromStdString(path);
    
    QtConcurrent::run([qpath, path]() -> QImage {
        QImageReader reader(qpath);
        reader.setAutoTransform(true);
        reader.setAllocationLimit(512);

        QSize imgSize = reader.size();
        if (imgSize.isValid()) {
            imgSize.scale(300, 300, Qt::KeepAspectRatio);
            reader.setScaledSize(imgSize);
        }

        QImage img = reader.read();
        if (!img.isNull()) {
            return img;
        } else {
            cv::Mat cvImg = ImageHasher::loadImage(path, 300);
            if (!cvImg.empty()) {
                cv::Mat rgb;
                cv::cvtColor(cvImg, rgb, cv::COLOR_BGR2RGB);
                QImage qimg(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
                return qimg.copy();
            }
        }
        return QImage();
    }).then(const_cast<ResultListModel*>(this), [this, path](QImage img) {
        m_loadingPaths.erase(path);
        if (!img.isNull()) {
            m_thumbnails[path] = QPixmap::fromImage(img).scaled(150, 150, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        } else {
            // エラー表示用ダミーピックスマップをセットしても良いが、ここでは空のまま
            m_thumbnails[path] = QPixmap(); 
        }
        const_cast<ResultListModel*>(this)->emitRowDataChangedForPath(path);
    });
}

void ResultListModel::emitRowDataChangedForPath(const std::string& path) {
    for (int i=0; i < static_cast<int>(m_items.size()); ++i) {
        if (m_items[i].type == ResultListItem::ImageRow) {
            for (const auto& img : m_items[i].images) {
                if (img.path == path) {
                    emit dataChanged(index(i, 0), index(i, 0));
                    return; // found it, rows don't repeat paths.
                }
            }
        }
    }
}
