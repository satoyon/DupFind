#pragma once
#include "DatabaseManager.hpp"
#include "SimilaritySearch.hpp"
#include <QCheckBox>
#include <QFutureWatcher>
#include <QTimer>
#include <QEvent>
#include <QObject>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QCloseEvent>
#include <memory>
#include <vector>

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

private slots:
  void onAddDirectory();
  void onRemoveDirectory();
  void onStartScan();
  void onDeleteSelected();
  void onThresholdChanged(int value);
  void onStrictChanged(int state);
  void onScanFinished();
  void performAsyncSearch();
  void onSearchFinished();
  void onClearResults();

private:
  void setupUi();
  void loadSettings();
  void saveSettings();
  void updateResultGrid(const std::vector<DuplicateGroup> &groups);
  std::vector<ImageData> getFilteredImages();
  bool eventFilter(QObject *obj, QEvent *event) override;
  void closeEvent(QCloseEvent *event) override;

  // Database
  std::unique_ptr<DatabaseManager> m_dbManager;

  // UI Elements
  QListWidget *m_dirList;
  QPushButton *m_addDirBtn;
  QPushButton *m_removeDirBtn;
  QPushButton *m_startScanBtn;
  QProgressBar *m_progressBar;
  QScrollArea *m_scrollArea;
  QWidget *m_resultWidget;
  QGridLayout *m_resultLayout;
  QPushButton *m_deleteBtn;
  QPushButton *m_clearBtn;

  // Similarity Controls
  QSlider *m_thresholdSlider;
  QLabel *m_thresholdLabel;
  QCheckBox *m_strictCheckBox;
  int m_currentThreshold = 10;
  bool m_strictMode = false;

  // Async
  QFutureWatcher<void> *m_scanWatcher;
  QFutureWatcher<std::vector<DuplicateGroup>> *m_searchWatcher;
  QTimer *m_searchTimer;
 
  // Cache
  std::vector<ImageData> m_lastScannedImages;
  QStringList m_loadedDirs;

  // Checkboxes for tracking selected images
  struct ResultItem {
    QCheckBox *checkbox;
    std::string path;
  };
  std::vector<ResultItem> m_resultItems;
};
