#include "MainWindow.hpp"
#include "ImageHasher.hpp"
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPixmap>
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
#include <QPointer>
#include <QSettings>
#include <QUrl>
#include <QtConcurrent>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  m_dbManager = std::make_unique<DatabaseManager>("dupfind_cache.db");
  m_dbManager->open();
  m_scanWatcher = new QFutureWatcher<void>(this);
  m_searchWatcher = new QFutureWatcher<std::vector<DuplicateGroup>>(this);

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
  m_addDirBtn = new QPushButton("Add Directory");
  m_removeDirBtn = new QPushButton("Remove Selected");
  m_startScanBtn = new QPushButton("Start Scan");
  m_deleteBtn = new QPushButton("Delete Selected Duplicates");
  m_deleteBtn->setObjectName("deleteBtn"); // QSS で赤くするため

  toolLayout->addWidget(m_addDirBtn);
  toolLayout->addWidget(m_removeDirBtn);
  toolLayout->addStretch();

  // Slider section
  toolLayout->addWidget(new QLabel("Similarity Threshold:"));
  m_thresholdSlider = new QSlider(Qt::Horizontal);
  m_thresholdSlider->setRange(0, 32);
  m_thresholdSlider->setValue(m_currentThreshold);
  m_thresholdSlider->setFixedWidth(150);
  m_thresholdLabel = new QLabel(QString::number(m_currentThreshold));
  toolLayout->addWidget(m_thresholdSlider);
  toolLayout->addWidget(m_thresholdLabel);

  m_strictCheckBox = new QCheckBox("Strict");
  m_strictCheckBox->setChecked(m_strictMode);
  toolLayout->addWidget(m_strictCheckBox);

  toolLayout->addWidget(m_startScanBtn);
  m_clearBtn = new QPushButton("Clear Results");
  toolLayout->addWidget(m_clearBtn);

  m_deselectBtn = new QPushButton("Deselect All");
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

  // Results Scroll Area
  m_scrollArea = new QScrollArea();
  m_scrollArea->setWidgetResizable(true);
  m_resultWidget = new QWidget();
  m_resultLayout = new QGridLayout(m_resultWidget);
  m_scrollArea->setWidget(m_resultWidget);
  splitLayout->addWidget(m_scrollArea);

  mainLayout->addLayout(splitLayout);

  // Progress Bar
  m_progressBar = new QProgressBar();
  m_progressBar->setVisible(false);
  mainLayout->addWidget(m_progressBar);

  setCentralWidget(central);

  // Connections
  connect(m_addDirBtn, &QPushButton::clicked, this,
          &MainWindow::onAddDirectory);
  connect(m_removeDirBtn, &QPushButton::clicked, this,
          &MainWindow::onRemoveDirectory);
  connect(m_startScanBtn, &QPushButton::clicked, this,
          &MainWindow::onStartScan);
  connect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::onClearResults);

  connect(m_deselectBtn, &QPushButton::clicked, this, [this]() {
    for (auto &item : m_resultItems) {
      if (item.checkbox)
        item.checkbox->setChecked(false);
    }
  });

  connect(m_deleteBtn, &QPushButton::clicked, this,
          &MainWindow::onDeleteSelected);
  connect(m_thresholdSlider, &QSlider::valueChanged, this,
          &MainWindow::onThresholdChanged);
  connect(m_strictCheckBox, &QCheckBox::stateChanged, this,
          &MainWindow::onStrictChanged);
  connect(m_scanWatcher, &QFutureWatcher<void>::finished, this,
          &MainWindow::onScanFinished);
}

void MainWindow::loadSettings() {
  QString iniPath = QCoreApplication::applicationDirPath() + "/DupFind.ini";
  QSettings settings(iniPath, QSettings::IniFormat);

  m_currentThreshold = settings.value("threshold", 5).toInt();
  m_strictMode = settings.value("strict_mode", false).toBool();
  m_loadedDirs = settings.value("directories").toStringList();
}

void MainWindow::saveSettings() {
  QString iniPath = QCoreApplication::applicationDirPath() + "/DupFind.ini";
  QSettings settings(iniPath, QSettings::IniFormat);

  settings.setValue("threshold", m_currentThreshold);
  settings.setValue("strict_mode", m_strictMode);

  QStringList dirs;
  for (int i = 0; i < m_dirList->count(); ++i) {
    dirs << m_dirList->item(i)->text();
  }
  settings.setValue("directories", dirs);
}

void MainWindow::closeEvent(QCloseEvent *event) {
  saveSettings();
  QMainWindow::closeEvent(event);
}

void MainWindow::onAddDirectory() {
  QString dir =
      QFileDialog::getExistingDirectory(this, "Select Directory to Scan");
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
    QMessageBox::warning(this, "No Directory",
                         "Please add at least one directory to scan.");
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
    DatabaseManager db("dupfind_cache.db");
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

  // フリーズして強制終了しても次回起動時に閾値が復元されるように保存
  saveSettings();

  // 操作が止まるまで待機（デバウンス）
  m_searchTimer->start();
}

void MainWindow::onStrictChanged(int state) {
  m_strictMode = (state == Qt::Checked);
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
  m_progressBar->setFormat("Searching duplicates... %p%");

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

void MainWindow::onClearResults() {
  // UIをクリア
  QLayoutItem *child;
  while ((child = m_resultLayout->takeAt(0)) != nullptr) {
    if (child->widget())
      delete child->widget();
    delete child;
  }
  for (int i = 0; i < m_resultLayout->rowCount(); ++i) {
    m_resultLayout->setRowStretch(i, 0);
  }
  m_resultItems.clear();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() == QEvent::MouseButtonDblClick) {
    QString path = obj->property("filePath").toString();
    if (!path.isEmpty()) {
      QDesktopServices::openUrl(QUrl::fromLocalFile(path));
      return true;
    }
  } else if (event->type() == QEvent::ContextMenu) {
    QString path = obj->property("filePath").toString();
    if (!path.isEmpty()) {
      QContextMenuEvent *ce = static_cast<QContextMenuEvent *>(event);
      QMenu menu;
      QAction *copyAction = menu.addAction("Copy Full Path(&C)");
      menu.addSeparator();
      QAction *removeAction = menu.addAction("Remove from List(&R)");

      QAction *selectedAction = menu.exec(ce->globalPos());
      if (selectedAction == copyAction) {
        QApplication::clipboard()->setText(path);
      } else if (selectedAction == removeAction) {
        int groupId = obj->property("groupId").toInt();
        removeGroupFromView(groupId);
      }
      return true;
    }
  }
  return QMainWindow::eventFilter(obj, event);
}

// 同一・類似と判定された画像のグループを受け取り、UI上のグリッドレイアウトへ動的にサムネイルと削除候補のチェックボックスを描画する
void MainWindow::updateResultGrid(const std::vector<DuplicateGroup> &groups,
                                  bool preserveState) {
  // 状態の保存
  std::unordered_map<std::string, bool> previousState;
  if (preserveState) {
    for (const auto &item : m_resultItems) {
      if (item.checkbox) {
        previousState[item.path] = item.checkbox->isChecked();
      }
    }
  }

  // UIをクリア
  QLayoutItem *child;
  while ((child = m_resultLayout->takeAt(0)) != nullptr) {
    if (child->widget())
      delete child->widget();
    delete child;
  }
  for (int i = 0; i < m_resultLayout->rowCount(); ++i) {
    m_resultLayout->setRowStretch(i, 0);
  }
  m_resultItems.clear();

  int row = 0;
  int groupId = 0;
  for (const auto &group : groups) {
    // グループ内で残す1枚（ファイルサイズが最大のもの。同サイズなら最初の1枚）を特定する
    const ImageData *bestImage = &group.images[0];
    for (size_t i = 1; i < group.images.size(); ++i) {
      if (group.images[i].file_size > bestImage->file_size) {
        bestImage = &group.images[i];
      }
    }

    // グループヘッダー
    QLabel *groupLabel = new QLabel(
        QString("Duplicate Group - %1 images").arg(group.images.size()));
    groupLabel->setStyleSheet("font-weight: bold; background-color: #f0f0f0; "
                              "padding: 5px; border-radius: 4px;");
    m_resultLayout->addWidget(groupLabel, row++, 0, 1, 4);

    int col = 0;
    for (const auto &imgData : group.images) {
      QWidget *imgWidget = new QWidget();
      imgWidget->setObjectName("imgCard");
      QVBoxLayout *vBox = new QVBoxLayout(imgWidget);

      QLabel *thumb = new QLabel();
      thumb->setProperty("filePath", QString::fromStdString(imgData.path));
      thumb->setProperty("groupId", groupId);
      thumb->installEventFilter(this);
      //      thumb->setToolTip(
      //          "Double click to open\nRight click to copy path or remove from
      //          list");
      QFileInfo fileInfo(QString::fromStdString(imgData.path));
      thumb->setToolTip(fileInfo.baseName());

      thumb->setText("Loading...");
      thumb->setAlignment(Qt::AlignCenter);
      vBox->addWidget(thumb);

      QPointer<QLabel> safeThumb(thumb);
      std::string pathCopy = imgData.path;

      QtConcurrent::run([pathCopy]() -> QImage {
        QImageReader reader(QString::fromStdString(pathCopy));
        reader.setAutoTransform(true);
        reader.setAllocationLimit(512); // デフォルト128MB制限を512MBに引き上げ

        QSize imgSize = reader.size();
        if (imgSize.isValid()) {
          // サムネイル表示に必要なサイズ（高階調な表示のためここでは高めでもよいが、150x150枠）に
          // デコード段階で縮小指定する。JPEG等では飛躍的に高速化・省メモリ化される。
          imgSize.scale(300, 300, Qt::KeepAspectRatio);
          reader.setScaledSize(imgSize);
        }

        QImage img = reader.read();
        if (!img.isNull()) {
          return img;
        } else {
          // Qtの標準ImageReaderで読み込めない形式(WebPプラギン未導入時など)のフォールバック
          cv::Mat cvImg = ImageHasher::loadImage(pathCopy, 300);
          if (!cvImg.empty()) {
            cv::Mat rgb;
            cv::cvtColor(cvImg, rgb, cv::COLOR_BGR2RGB);
            QImage qimg(rgb.data, rgb.cols, rgb.rows, rgb.step,
                        QImage::Format_RGB888);
            return qimg.copy(); // OpenCVのMatデータ揮発を防ぐためcopy()
          }
        }
        return QImage();
      }).then(this, [safeThumb](QImage img) {
        if (!safeThumb)
          return; // 既にUIがクリアされている場合は何もしない

        if (!img.isNull()) {
          QPixmap pix = QPixmap::fromImage(img);
          safeThumb->setPixmap(pix.scaled(150, 150, Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation));
        } else {
          safeThumb->setText("Error Loading");
        }
      });

      // 自動チェック: 残す1枚（bestImage）以外を削除候補としてチェックする
      QCheckBox *cb = new QCheckBox("Delete candidate");
      if (preserveState &&
          previousState.find(imgData.path) != previousState.end()) {
        cb->setChecked(previousState[imgData.path]);
      } else {
        if (&imgData != bestImage) {
          cb->setChecked(true);
        }
      }
      vBox->addWidget(cb);

      QLabel *infoLabel =
          new QLabel(QString("%1 KB\n%2")
                         .arg(imgData.file_size / 1024)
                         .arg(QString::fromStdString(imgData.path)));
      infoLabel->setWordWrap(true);
      infoLabel->setMaximumWidth(150);
      infoLabel->setStyleSheet("font-size: 10px; color: #666;");
      vBox->addWidget(infoLabel);

      m_resultLayout->addWidget(imgWidget, row, col);
      m_resultItems.push_back({cb, imgData.path, groupId});

      col++;
      if (col >= 4) {
        col = 0;
        row++;
      }
    }
    row++;
    groupId++;
  }

  // 項目数が少ない時に各行が縦に間延びする（ヘッダーが極端に太くなる）のを防ぐため、
  // 最後の空行に対して余った縦スペースをすべて吸収させる
  m_resultLayout->setRowStretch(row, 1);
}

void MainWindow::onDeleteSelected() {
  int count = 0;
  std::map<int, int> groupTotalCount;
  std::map<int, int> groupCheckedCount;

  for (const auto &item : m_resultItems) {
    groupTotalCount[item.groupId]++;
    if (item.checkbox->isChecked()) {
      groupCheckedCount[item.groupId]++;
      count++;
    }
  }

  if (count == 0)
    return;

  bool allCheckedInSomeGroup = false;
  for (auto const &[gId, total] : groupTotalCount) {
    if (groupCheckedCount[gId] == total) {
      allCheckedInSomeGroup = true;
      break;
    }
  }

  if (allCheckedInSomeGroup) {
    auto warnRes = QMessageBox::warning(
        this, "Warning: All images selected",
        "一部の類似画像グループで、すべての画像が削除対象としてチェックされてい"
        "ます。\n"
        "このまま削除すると、それらの画像ファイルはすべて失われます。\n\n"
        "本当に削除を実行しますか？",
        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
    if (warnRes != QMessageBox::Ok) {
      return;
    }
  }

  auto res = QMessageBox::question(
      this, "Confirm Deletion",
      QString("Are you sure you want to move %1 images to Trash?").arg(count));

  if (res == QMessageBox::Yes) {
    std::vector<QString> failures;
    for (const auto &item : m_resultItems) {
      if (item.checkbox->isChecked()) {
        QString qPath = QString::fromStdString(item.path);
        // QFile::moveToTrash は Qt 5.15+ で利用可能
        if (QFile::moveToTrash(qPath)) {
          m_dbManager->removeImage(item.path);
        } else {
          failures.push_back(qPath);
        }
      }
    }

    if (!failures.empty()) {
      QString msg = "The following files could not be moved to trash and were "
                    "NOT deleted:\n\n";
      for (const auto &f : failures) {
        msg += f + "\n";
      }
      QMessageBox::warning(this, "Deletion Error", msg);
    }

    // リストをリフレッシュ
    auto images = getFilteredImages();
    m_currentGroups = SimilaritySearch::findDuplicates(
        images, m_currentThreshold, m_strictMode);
    updateResultGrid(m_currentGroups);
  }
}
