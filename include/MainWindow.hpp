#pragma once
#include "DatabaseManager.hpp"
#include "SimilaritySearch.hpp"
#include <unordered_set>
#include <QCheckBox>
#include <QCloseEvent>
#include <QEvent>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QObject>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <memory>
#include <vector>

// アプリケーションのメインウィンドウ（GUI）を管理するクラス
class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

private slots:
  // イベントに応答するスロット関数群
  void onAddDirectory();    // 検索対象ディレクトリ追加ボタン押下
  void onRemoveDirectory(); // リストからディレクトリ除外ボタン押下
  void onStartScan();       // 重複検索スキャン開始ボタン押下
  void onDeleteSelected();  // チェックされた重複画像を削除するボタン押下
  void onThresholdChanged(int value); // 類似度しきい値スライダー変更時
  void onStrictChanged(int state);    // Strictモードチェックボックス変更時
  void onScanFinished();              // ディレクトリの一括スキャン完了時
  void performAsyncSearch();          // 非同期での類似画像群の抽出実行
  void onSearchFinished();            // 類似画像の検索完了時
  void onClearResults();              // 結果表示のクリア
  void removeGroupFromView(int groupId); // 指定したグループをリストから除外

private:
  // 内部処理・初期化メソッド
  void setupUi();      // UIのレイアウトと初期化
  void loadSettings(); // INIファイルからの設定値復元
  void saveSettings(); // INIファイルへの設定値保存
  void updateResultGrid(const std::vector<DuplicateGroup>
                            &groups, bool preserveState = false); // 結果グリッドへサムネイルを並べる
  std::vector<ImageData>
  getFilteredImages(); // 現在リストにあるディレクトリの画像のみ抽出
  bool
  eventFilter(QObject *obj,
              QEvent *event) override; // 右クリックメニュー等のイベントフィルタ
  void
  closeEvent(QCloseEvent *event) override; // ウィンドウ終了時の保存処理など

  // データベースマネージャーのインスタンス
  std::unique_ptr<DatabaseManager> m_dbManager;

  // UI要素
  QListWidget *m_dirList; // 追加済みディレクトリのリスト
  QPushButton *m_addDirBtn;
  QPushButton *m_removeDirBtn;
  QPushButton *m_startScanBtn;
  QPushButton *m_deselectBtn;  // チェック状態を全クリアするボタン
  QProgressBar *m_progressBar; // スキャン・検索時のプログレスバー
  QScrollArea *m_scrollArea;   // 検索結果表示用スクロールエリア
  QWidget *m_resultWidget;
  QGridLayout *m_resultLayout; // 検索結果をグリッドで配置するレイアウト
  QPushButton *m_deleteBtn;
  QPushButton *m_clearBtn;

  // 類似判定コントロール要素
  QSlider *m_thresholdSlider;  // ハミング距離しきい値の設定スライダー
  QLabel *m_thresholdLabel;    // しきい値の現在数値ラベル
  QCheckBox *m_strictCheckBox; // pHash, dHash両方を厳密チェックするか
  int m_currentThreshold = 5;
  bool m_strictMode = false;

  // 非同期（バックグラウンド）処理用オブジェクト
  QFutureWatcher<void> *m_scanWatcher; // 画像スキャンの進捗監視
  QFutureWatcher<std::vector<DuplicateGroup>>
      *m_searchWatcher; // 類似検索の進捗監視
  QTimer
      *m_searchTimer; // 頻繁なスライダ操作をまとめる（デバウンス）ためのタイマ

  // キャッシュ
  std::vector<ImageData>
      m_lastScannedImages;  // 各ディレクトリから抽出した画像キャッシュ
  QStringList m_loadedDirs; // 起動時にロードされたディレクトリ群

  // 結果画面に追加された画像とチェックボックスを管理する構造体
  struct ResultItem {
    QCheckBox *checkbox; // 削除対象としてマークするチェックボックス
    std::string path;    // チェックボックスに紐づく画像パス
    int groupId;         // 属する重複グループのID（全削除警告用）
  };
  std::vector<ResultItem>
      m_resultItems; // 現在表示されている結果アイテムのリスト
  std::vector<DuplicateGroup> m_currentGroups; // 現在表示中のグループ一覧
  std::unordered_set<std::string> m_ignoredPaths; // セッション中に除外した画像のパス
};
