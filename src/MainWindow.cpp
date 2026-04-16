#include "MainWindow.hpp"
#include "ImageHasher.hpp"
#include <opencv2/imgproc.hpp>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QPixmap>
#include <QUrl>
#include <QVBoxLayout>
#include <chrono>
#include <filesystem>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QImageReader>
#include <QMenu>
#include <QMimeData>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  QString dataPath =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (!QDir().exists(dataPath)) {
    QDir().mkpath(dataPath);
  }
  QString dbPath = dataPath + "/dupfind_cache.db";
  m_dbManager = std::make_unique<DatabaseManager>(dbPath.toStdString());
  m_dbManager->open();
  m_scanWatcher = new QFutureWatcher<void>(this);
  m_searchWatcher = new QFutureWatcher<std::vector<DuplicateGroup>>(this);
  m_networkManager = new QNetworkAccessManager(this);
  connect(m_networkManager, &QNetworkAccessManager::finished, this,
          &MainWindow::onUrlDownloadFinished);

  m_searchTimer = new QTimer(this);
  m_searchTimer->setSingleShot(true);
  m_searchTimer->setInterval(300);
  connect(m_searchTimer, &QTimer::timeout, this,
          &MainWindow::performAsyncSearch);
  connect(m_searchWatcher,
          &QFutureWatcher<std::vector<DuplicateGroup>>::finished, this,
          &MainWindow::onSearchFinished);

  // スタイルの適用
  QFile styleFile("resources/style.qss");
  if (styleFile.open(QFile::ReadOnly)) {
    QString styleSheet = QLatin1String(styleFile.readAll());
    setStyleSheet(styleSheet);
  }

  loadSettings(); // setupUi の前に読み込む
  setupUi();
}

MainWindow::~MainWindow() {}

std::vector<ImageData> MainWindow::getFilteredImages() {
  auto allImages = m_dbManager->getAllImages();
  if (m_dirList->count() == 0)
    return {};

  std::vector<ImageData> filtered;
  for (const auto &img : allImages) {
    bool match = false;
    for (int i = 0; i < m_dirList->count(); ++i) {
      std::string dirPath = m_dirList->item(i)->text().toStdString();
      std::string dirPathWithSlash = dirPath;
      if (!dirPathWithSlash.empty() && dirPathWithSlash.back() != '/' &&
          dirPathWithSlash.back() != '\\') {
        dirPathWithSlash += "/";
      }
      if (img.path.find(dirPathWithSlash) == 0 || img.path == dirPath) {
        match = true;
        break;
      }
    }
    if (match && m_ignoredPaths.find(img.path) == m_ignoredPaths.end()) {
      filtered.push_back(img);
    }
  }
  return filtered;
}

void MainWindow::setupUi() {
  auto *central = new QWidget();
  auto *mainLayout = new QVBoxLayout(central);

  // Toolbar-like top section
  auto *toolLayout = new QHBoxLayout();
  m_addDirBtn = new QPushButton(tr("Add Directory"));
  m_removeDirBtn = new QPushButton(tr("Remove Selected"));
  m_startScanBtn = new QPushButton(tr("Start Scan"));
  m_deleteBtn = new QPushButton(tr("Delete Selected Duplicates"));
  m_deleteBtn->setObjectName("deleteBtn"); // QSS で赤くするため

  toolLayout->addWidget(m_addDirBtn);
  toolLayout->addWidget(m_removeDirBtn);
  toolLayout->addStretch();

  // Slider section
  toolLayout->addWidget(new QLabel(tr("Similarity Threshold:")));
  m_thresholdSlider = new QSlider(Qt::Horizontal);
  m_thresholdSlider->setRange(0, 32);
  m_thresholdSlider->setValue(m_currentThreshold);
  m_thresholdSlider->setFixedWidth(150);
  m_thresholdLabel = new QLabel(QString::number(m_currentThreshold));
  toolLayout->addWidget(m_thresholdSlider);
  toolLayout->addWidget(m_thresholdLabel);

  m_strictCheckBox = new QCheckBox(tr("Strict"));
  m_strictCheckBox->setChecked(m_strictMode);
  toolLayout->addWidget(m_strictCheckBox);

  toolLayout->addWidget(m_startScanBtn);
  m_clearBtn = new QPushButton(tr("Clear Results"));
  toolLayout->addWidget(m_clearBtn);

  m_deselectBtn = new QPushButton(tr("Deselect All"));
  toolLayout->addWidget(m_deselectBtn);

  toolLayout->addWidget(m_deleteBtn);
  mainLayout->addLayout(toolLayout);

  // Split view
  auto *splitLayout = new QHBoxLayout();

  // Directory List
  m_dirList = new QListWidget();
  m_dirList->setMaximumWidth(250);
  //  m_dirList->addItems(m_loadedDirs); // ここで復元
  for (const QString &dir : m_loadedDirs) {
    auto *item = new QListWidgetItem(dir, m_dirList);
    item->setToolTip(dir);
  }
  splitLayout->addWidget(m_dirList);

  // Results section (Search box + List View)
  auto *resultLayout = new QVBoxLayout();
  m_searchBox = new QLineEdit();
  m_searchBox->setPlaceholderText(
      tr("Filter results by path or filename... (Press Esc to close)"));
  m_searchBox->setVisible(false);
  resultLayout->addWidget(m_searchBox);

  // Results List View
  m_resultView = new QListView();
  m_model = new ResultListModel(this);
  m_proxyModel = new ResultFilterProxyModel(this);
  m_proxyModel->setSourceModel(m_model);
  m_delegate = new ResultItemDelegate(this);

  m_resultView->setModel(m_proxyModel);
  m_resultView->setItemDelegate(m_delegate);
  m_resultView->setSelectionMode(QAbstractItemView::NoSelection);
  m_resultView->setSpacing(5);

  m_resultView->installEventFilter(this);
  m_searchBox->installEventFilter(this);

  resultLayout->addWidget(m_resultView);
  splitLayout->addLayout(resultLayout);

  mainLayout->addLayout(splitLayout);

  // Progress Bar
  m_progressBar = new QProgressBar();
  m_progressBar->setVisible(false);
  mainLayout->addWidget(m_progressBar);

  setCentralWidget(central);

  // ドロップを受け付ける
  setAcceptDrops(true);

  // Connections
  connect(m_addDirBtn, &QPushButton::clicked, this,
          &MainWindow::onAddDirectory);
  connect(m_removeDirBtn, &QPushButton::clicked, this,
          &MainWindow::onRemoveDirectory);
  connect(m_startScanBtn, &QPushButton::clicked, this,
          &MainWindow::onStartScan);
  connect(m_searchBox, &QLineEdit::textChanged, this,
          &MainWindow::onSearchTextChanged);
  connect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::onClearResults);

  connect(m_deselectBtn, &QPushButton::clicked, this,
          [this]() { m_model->clearAllChecks(); });

  connect(m_deleteBtn, &QPushButton::clicked, this,
          &MainWindow::onDeleteSelected);
  connect(m_thresholdSlider, &QSlider::valueChanged, this,
          &MainWindow::onThresholdChanged);
  connect(m_strictCheckBox, &QCheckBox::stateChanged, this,
          &MainWindow::onStrictChanged);
  connect(m_scanWatcher, &QFutureWatcher<void>::finished, this,
          &MainWindow::onScanFinished);

  connect(m_delegate, &ResultItemDelegate::contextMenuRequested, this,
          &MainWindow::onContextMenuRequested);
  connect(m_delegate, &ResultItemDelegate::fileDoubleClicked, this,
          &MainWindow::onFileDoubleClicked);
}

void MainWindow::loadSettings() {
  //  QString iniPath = QCoreApplication::applicationDirPath() + "/DupFind.ini";
  QString dataPath =
      QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  if (!QDir().exists(dataPath)) {
    QDir().mkpath(dataPath);
  }
  QString iniPath = dataPath + "/settings.ini";
  QSettings settings(iniPath, QSettings::IniFormat);

  m_currentThreshold = settings.value("threshold", 5).toInt();
  m_strictMode = settings.value("strict_mode", false).toBool();
  m_loadedDirs = settings.value("directories").toStringList();
}

void MainWindow::saveSettings() {
  //  QString iniPath = QCoreApplication::applicationDirPath() + "/DupFind.ini";
  QString dataPath =
      QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  if (!QDir().exists(dataPath)) {
    QDir().mkpath(dataPath);
  }
  QString iniPath = dataPath + "/settings.ini";
  QSettings settings(iniPath, QSettings::IniFormat);

  settings.setValue("threshold", m_currentThreshold);
  settings.setValue("strict_mode", m_strictMode);

  QStringList dirs;
  for (int i = 0; i < m_dirList->count(); ++i) {
    dirs << m_dirList->item(i)->text();
  }
  if (dirs.isEmpty()) {
    settings.remove("directories");
  } else {
    settings.setValue("directories", dirs);
  }
}

void MainWindow::closeEvent(QCloseEvent *event) {
  saveSettings();
  QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
  if (event->mimeData()->hasUrls()) {
    event->acceptProposedAction();
  }
}

// ドロップを受け付ける
void MainWindow::dropEvent(QDropEvent *e) {
  const QMimeData *mimeData = e->mimeData();
  if (!mimeData->hasUrls())
    return;

  std::vector<DuplicateGroup> results;
  auto candidates = getFilteredImages();
  bool dirAdded = false;

  for (const QUrl &url : mimeData->urls()) {
    QString localPath = url.toLocalFile();
    if (localPath.isEmpty()) {
      // 外部URLの可能性がある場合
      if (url.isValid() &&
          (url.scheme() == "http" || url.scheme() == "https")) {
        m_networkManager->get(QNetworkRequest(url));
        m_progressBar->setVisible(true);
        m_progressBar->setRange(0, 0);
        m_progressBar->setFormat(tr("Downloading image..."));
      }
      continue;
    }

    QFileInfo fileInfo(localPath);
    if (fileInfo.isDir()) {
      // 重複チェック
      bool exists = false;
      for (int i = 0; i < m_dirList->count(); ++i) {
        if (m_dirList->item(i)->text() == localPath) {
          exists = true;
          break;
        }
      }
      if (!exists) {
        auto *item = new QListWidgetItem(localPath, m_dirList);
        item->setToolTip(localPath);
        dirAdded = true;
      }
    } else if (fileInfo.isFile()) {
      cv::Mat img = ImageHasher::loadImage(localPath.toStdString());
      if (img.empty())
        continue;

      // ここでクリア
      m_model->clear();

      uint64_t dhash = ImageHasher::calculateDHash(img);
      uint64_t phash = ImageHasher::calculatePHash(img);

      DuplicateGroup foundGroup;
      // ドロップされた画像自身を最初に追加
      ImageData dropped;
      dropped.path = localPath.toStdString();
      dropped.dhash = dhash;
      dropped.phash = phash;
      dropped.file_size = fileInfo.size();
      dropped.timestamp = fileInfo.lastModified().toSecsSinceEpoch();
      foundGroup.images.push_back(dropped);

      for (const auto &cand : candidates) {
        // 自分自身（パスが同じ）は除外
        if (cand.path == dropped.path)
          continue;

        bool match = false;
        if (m_strictMode) {
          if (ImageHasher::hammingDistance(dhash, cand.dhash) <=
                  m_currentThreshold &&
              ImageHasher::hammingDistance(phash, cand.phash) <=
                  m_currentThreshold) {
            match = true;
          }
        } else {
          if (ImageHasher::hammingDistance(dhash, cand.dhash) <=
                  m_currentThreshold ||
              ImageHasher::hammingDistance(phash, cand.phash) <=
                  m_currentThreshold) {
            match = true;
          }
        }
        if (match) {
          foundGroup.images.push_back(cand);
        }
      }

      // 重複が見つかった場合のみ追加（自分1枚だけなら追加しない）
      if (foundGroup.images.size() > 1) {
        results.push_back(foundGroup);
      }
    }
  }

  if (!results.empty()) {
    updateResultGrid(results);
  }

  if (dirAdded) {
    saveSettings();
  }
}

void MainWindow::onAddDirectory() {
  QString dir =
      QFileDialog::getExistingDirectory(this, tr("Select Directory to Scan"));
  if (!dir.isEmpty()) {
    auto *item = new QListWidgetItem(dir, m_dirList);
    item->setToolTip(dir);
    saveSettings();
  }
}

void MainWindow::onRemoveDirectory() {
  auto *item = m_dirList->currentItem();
  if (item) {
    QString dirPath = item->text();
    m_dbManager->setDirectorySearchedStatus(dirPath.toStdString(), false);
    delete item;
    // キャッシュ再読み込み
    m_lastScannedImages = getFilteredImages();
    saveSettings();
  }
}

// 「Start Scan」ボタン押下時の処理
// 選択されたディレクトリを再帰的に走査し、並列処理によって高速に画像ハッシュを分散計算し、DB保存する
void MainWindow::onStartScan() {
  if (m_dirList->count() == 0) {
    QMessageBox::warning(this, tr("No Directory"),
                         tr("Please add at least one directory to scan."));
    return;
  }

  m_progressBar->setVisible(true);
  m_progressBar->setRange(0, 0); // 準備中
  m_startScanBtn->setEnabled(false);

  // 1. スキャン対象ディレクトリの収集
  std::vector<QString> dirPaths;
  for (int i = 0; i < m_dirList->count(); ++i) {
    dirPaths.push_back(m_dirList->item(i)->text());
  }

  // キャッシュ（DBの既存データ）を読み込み
  auto cachedList = m_dbManager->getAllImages();
  std::unordered_map<std::string, ImageData> cache;
  for (const auto &img : cachedList) {
    cache[img.path] = img;
  }

  // 2. 非同期で一連の処理を実行
  auto future = QtConcurrent::run([this, dirPaths, cache]() {
    // 全ファイルパスをリストアップ
    std::vector<std::string> allFiles;
    const QStringList filters = {"*.jpg",  "*.png",  "*.jpeg", "*.bmp",
                                 "*.webp", "*.tiff", "*.heic", "*.heif"};
    for (const auto &dirPath : dirPaths) {
      QDirIterator it(dirPath, filters, QDir::Files | QDir::NoSymLinks,
                      QDirIterator::Subdirectories);
      while (it.hasNext()) {
        allFiles.push_back(it.next().toStdString());
      }
    }

    // 3. 並列ハッシュ計算 (mapped)
    auto processFunc = [&cache](const std::string &stdPath) -> ImageData {
      try {
        std::error_code ec;
        auto currentSize = std::filesystem::file_size(stdPath, ec);
        auto currentMtime = std::chrono::duration_cast<std::chrono::seconds>(
                                std::filesystem::last_write_time(stdPath, ec)
                                    .time_since_epoch())
                                .count();

        auto it = cache.find(stdPath);
        if (it != cache.end()) {
          if (it->second.file_size == static_cast<int64_t>(currentSize) &&
              it->second.timestamp == static_cast<int64_t>(currentMtime)) {
            return {}; // キャッシュと一致する場合はDBの再書き込みを避けるため空を返す
          }
        }

        cv::Mat img = ImageHasher::loadImage(stdPath);
        ImageData data;
        data.path = stdPath;
        data.is_searched = false;
        if (!img.empty()) {
          data.dhash = ImageHasher::calculateDHash(img);
          data.phash = ImageHasher::calculatePHash(img);
        }
        data.timestamp = currentMtime;
        data.file_size = currentSize;
        return data;
      } catch (...) {
        return {};
      }
    };

    // 並列実行
    auto results = QtConcurrent::blockingMapped(allFiles, processFunc);

    // 4. DBへの一括保存 (メインスレッドの管理外のDB接続で行う)
    QString dataPath =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QString dbPath = dataPath + "/dupfind_cache.db";
    DatabaseManager db(dbPath.toStdString());
    if (db.open()) {
      db.cleanupStaleEntries(); // DBに存在して実体がないファイルを削除
      db.beginTransaction();
      for (const auto &data : results) {
        if (data.path.empty())
          continue; // 全く変更がないファイルはスキップ
        db.addImage(data);
      }
      db.commitTransaction();
    }
  });

  m_scanWatcher->setFuture(future);
}

void MainWindow::onScanFinished() {
  m_startScanBtn->setEnabled(true);

  auto images = getFilteredImages();
  m_lastScannedImages = images; // キャッシュを更新

  // スキャン後は同期処理で固まらないように非同期検索を起動する
  performAsyncSearch();
}

void MainWindow::onThresholdChanged(int value) {
  m_currentThreshold = value;
  m_thresholdLabel->setText(QString::number(value));

  // Hide and clear search to reset filter on new search condition
  if (m_searchBox->isVisible() || !m_searchBox->text().isEmpty()) {
    m_searchBox->clear();
    m_searchBox->setVisible(false);
    m_resultView->setFocus();
  }

  // フリーズして強制終了しても次回起動時に閾値が復元されるように保存
  saveSettings();

  // 操作が止まるまで待機（デバウンス）
  m_searchTimer->start();
}

void MainWindow::onStrictChanged(int state) {
  m_strictMode = (state == Qt::Checked);

  // Hide and clear search to reset filter on new search condition
  if (m_searchBox->isVisible() || !m_searchBox->text().isEmpty()) {
    m_searchBox->clear();
    m_searchBox->setVisible(false);
    m_resultView->setFocus();
  }

  m_searchTimer->start();
}

// ハミング距離などに基づき、メモリ上の全画像データから重複グループを非同期で抽出・クラスタリングする
void MainWindow::performAsyncSearch() {
  if (m_lastScannedImages.empty()) {
    m_lastScannedImages = getFilteredImages();
  }

  if (m_lastScannedImages.empty())
    return;

  m_progressBar->setVisible(true);
  m_progressBar->setRange(0, 0);
  m_progressBar->setFormat(tr("Searching duplicates... %p%"));

  // 現在実行中の検索があればキャンセルはできないが、WatcherのFutureを上書きすることで最新のみを追う
  auto future = QtConcurrent::run([images = m_lastScannedImages,
                                   threshold = m_currentThreshold,
                                   strict = m_strictMode]() {
    return SimilaritySearch::findDuplicates(images, threshold, strict);
  });

  m_searchWatcher->setFuture(future);
}

void MainWindow::onSearchFinished() {
  m_progressBar->setVisible(false);
  m_currentGroups = m_searchWatcher->result();
  updateResultGrid(m_currentGroups);

  // サムネイルが表示された時点で検索対象ディレクトリ群を検索済みとする
  for (int i = 0; i < m_dirList->count(); ++i) {
    QString dirPath = m_dirList->item(i)->text();
    m_dbManager->setDirectorySearchedStatus(dirPath.toStdString(), true);
  }
  // メモリ上のキャッシュも更新する
  m_lastScannedImages = getFilteredImages();
}

void MainWindow::removeGroupFromView(int groupId) {
  if (groupId >= 0 && groupId < static_cast<int>(m_currentGroups.size())) {
    for (const auto &img : m_currentGroups[groupId].images) {
      m_ignoredPaths.insert(img.path);
    }
    m_lastScannedImages = getFilteredImages();
    m_currentGroups.erase(m_currentGroups.begin() + groupId);
    updateResultGrid(m_currentGroups, true);
  }
}

void MainWindow::onClearResults() { m_model->clear(); }

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() == QEvent::KeyPress) {
    QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
    if (obj == m_resultView) {
      if ((keyEvent->modifiers() & Qt::ControlModifier) &&
          keyEvent->key() == Qt::Key_F) {
        m_searchBox->setVisible(true);
        m_searchBox->setFocus();
        return true;
      } else if (keyEvent->key() == Qt::Key_F &&
                 keyEvent->modifiers() == Qt::NoModifier) {
        m_searchBox->setVisible(true);
        m_searchBox->setFocus();
        return true;
      }
    } else if (obj == m_searchBox) {
      if (keyEvent->key() == Qt::Key_Escape) {
        m_searchBox->clear();
        m_searchBox->setVisible(false);
        m_resultView->setFocus();
        return true;
      }
    }
  }
  return QMainWindow::eventFilter(obj, event);
}

void MainWindow::onSearchTextChanged(const QString &text) {
  if (m_proxyModel) {
    m_proxyModel->setSearchText(text);
  }
}

void MainWindow::onFileDoubleClicked(const std::string &path) {
  if (!path.empty()) {
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QString::fromStdString(path)));
  }
}

void MainWindow::onContextMenuRequested(const std::string &path, int groupId,
                                        const QPoint &globalPos) {
  if (!path.empty()) {
    QMenu menu;
    QAction *copyAction = menu.addAction(tr("Copy Full Path(&C)"));
    menu.addSeparator();
    QAction *removeAction = menu.addAction(tr("Remove from List(&R)"));

    QAction *selectedAction = menu.exec(globalPos);
    if (selectedAction == copyAction) {
      QApplication::clipboard()->setText(QString::fromStdString(path));
    } else if (selectedAction == removeAction) {
      removeGroupFromView(groupId);
    }
  }
}

// 同一・類似と判定された画像のグループを受け取り、UI上のグリッドレイアウトへ動的にサムネイルと削除候補のチェックボックスを描画する
void MainWindow::updateResultGrid(const std::vector<DuplicateGroup> &groups,
                                  bool preserveState) {
  m_model->setGroups(groups, preserveState);
}

void MainWindow::onDeleteSelected() {
  std::unordered_set<int> visibleGroupIds;
  for (int i = 0; i < m_proxyModel->rowCount(); ++i) {
    QModelIndex proxyIndex = m_proxyModel->index(i, 0);
    QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
    const auto &item = m_model->getItem(sourceIndex.row());
    if (item.type == ResultListItem::Header) {
      visibleGroupIds.insert(item.groupId);
    }
  }

  int count = 0;
  std::map<int, int> groupTotalCount;
  std::map<int, int> groupCheckedCount;
  std::vector<std::string> pathsToDelete;

  const auto &checkStates = m_model->getCheckStates();

  int groupId = 0;
  for (const auto &group : m_currentGroups) {
    if (visibleGroupIds.find(groupId) != visibleGroupIds.end()) {
      for (const auto &img : group.images) {
        groupTotalCount[groupId]++;
        auto it = checkStates.find(img.path);
        if (it != checkStates.end() && it->second) {
          groupCheckedCount[groupId]++;
          count++;
          pathsToDelete.push_back(img.path);
        }
      }
    }
    groupId++;
  }

  if (count == 0)
    return;

  bool allCheckedInSomeGroup = false;
  for (auto const &[gId, total] : groupTotalCount) {
    if (groupCheckedCount[gId] == total && total > 0) {
      allCheckedInSomeGroup = true;
      break;
    }
  }

  if (allCheckedInSomeGroup) {
    auto warnRes = QMessageBox::warning(
        this, tr("Warning: All images selected"),
        tr("一部の類似画像グループで、すべての画像が削除対象としてチェックされ"
           "てい"
           "ます。\n"
           "このまま削除すると、それらの画像ファイルはすべて失われます。\n\n"
           "本当に削除を実行しますか？"),
        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
    if (warnRes != QMessageBox::Ok) {
      return;
    }
  }

  auto res = QMessageBox::question(
      this, tr("Confirm Deletion"),
      tr("Are you sure you want to move %1 images to Trash?").arg(count));

  if (res == QMessageBox::Yes) {
    std::vector<QString> failures;
    for (const auto &path : pathsToDelete) {
      QString qPath = QString::fromStdString(path);
      if (QFile::moveToTrash(qPath)) {
        m_dbManager->removeImage(path);
      } else {
        failures.push_back(qPath);
      }
    }
    // 壊れたサムネイルが表示される現象を抑えるため、ここでいったんCOMMITする
    m_dbManager->commitTransaction();

    if (!failures.empty()) {
      QString msg =
          tr("The following files could not be moved to trash and were "
             "NOT deleted:\n\n");
      for (const auto &f : failures) {
        msg += f + "\n";
      }
      QMessageBox::warning(this, tr("Deletion Error"), msg);
    }

    // リストをリフレッシュ
    auto images = getFilteredImages();
    m_currentGroups = SimilaritySearch::findDuplicates(
        images, m_currentThreshold, m_strictMode);
    updateResultGrid(m_currentGroups);
  }
}

void MainWindow::onUrlDownloadFinished(QNetworkReply *reply) {
  m_progressBar->setVisible(false);
  if (reply->error() != QNetworkReply::NoError) {
    QMessageBox::warning(
        this, tr("Download Error"),
        tr("Failed to download image: %1").arg(reply->errorString()));
    reply->deleteLater();
    return;
  }

  QByteArray data = reply->readAll();
  reply->deleteLater();

  std::vector<uchar> buffer(data.begin(), data.end());
  cv::Mat img = cv::imdecode(buffer, cv::IMREAD_COLOR);

  if (img.empty()) {
    QMessageBox::warning(this, tr("Invalid Image"),
                         tr("The dropped URL does not contain a valid image."));
    return;
  }

  m_model->clear();
  uint64_t dhash = ImageHasher::calculateDHash(img);
  uint64_t phash = ImageHasher::calculatePHash(img);

  auto candidates = getFilteredImages();
  DuplicateGroup foundGroup;

  // ドロップされた画像（URL）を「Dropped Image」として追加
  ImageData dropped;
  dropped.path = reply->url().toString().toStdString();
  dropped.dhash = dhash;
  dropped.phash = phash;
  dropped.file_size = data.size();
  dropped.timestamp = QDateTime::currentSecsSinceEpoch();
  foundGroup.images.push_back(dropped);

  // サムネイルをモデルに追加（URLパスだとQImageReaderで読めないため）
  cv::Mat rgb;
  cv::cvtColor(img, rgb, cv::COLOR_BGR2RGB);
  QImage qimg(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
  m_model->addThumbnail(dropped.path, qimg.copy()); // copy to ensure data ownership

  for (const auto &cand : candidates) {
    bool match = false;
    if (m_strictMode) {
      if (ImageHasher::hammingDistance(dhash, cand.dhash) <=
              m_currentThreshold &&
          ImageHasher::hammingDistance(phash, cand.phash) <=
              m_currentThreshold) {
        match = true;
      }
    } else {
      if (ImageHasher::hammingDistance(dhash, cand.dhash) <=
              m_currentThreshold ||
          ImageHasher::hammingDistance(phash, cand.phash) <=
              m_currentThreshold) {
        match = true;
      }
    }
    if (match) {
      foundGroup.images.push_back(cand);
    }
  }

  std::vector<DuplicateGroup> results;
  if (foundGroup.images.size() > 1) {
    results.push_back(foundGroup);
  }

  updateResultGrid(results);
}
