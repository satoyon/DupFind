#include <QCoreApplication>
#include <QDirIterator>
#include <QProcess>
#include <QSettings>
#include <QSharedMemory>
#include <QStringList>
#include <QThread>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <unordered_map>

#include "DatabaseManager.hpp"
#include "ImageHasher.hpp"
#include <opencv2/core/utils/logger.hpp>

void workerAdd(const QStringList &dirs);
void cmdAdd(const QStringList &dirs);
void cmdTh(int thValue);
void cmdSearch(const QString &imageFile);
void cmdStrict(bool strict);

int main(int argc, char *argv[]) {
  // OpenCVの不要なINFOログ（並列バックエンド読み込み失敗など）を抑制する
  cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);

  QCoreApplication app(argc, argv);
  QStringList args = app.arguments();

  if (args.size() < 2) {
    std::cerr << "Usage: DupFindCmd add <dir1> <dir2> ...\n"
              << "       DupFindCmd th <N>\n"
              << "       DupFindCmd search <image_file>\n"
              << "       DupFindCmd strict [on|off]\n";
    return 1;
  }

  QString command = args[1];

  if (command == "--worker-add") {
    QStringList dirs = args.mid(2);
    workerAdd(dirs);
    return 0;
  } else if (command == "add") {
    if (args.size() < 3) {
      std::cerr << "Error: 'add' Requires at least one directory.\n";
      return 1;
    }
    cmdAdd(args.mid(2));
    return 0;
  } else if (command == "th") {
    if (args.size() != 3) {
      std::cerr << "Error: 'th' Requires exactly one integer.\n";
      return 1;
    }
    bool ok;
    int val = args[2].toInt(&ok);
    if (!ok || val < 0 || val > 32) {
      std::cerr << "Error: N must be an integer between 0 and 32.\n";
      return 1;
    }
    cmdTh(val);
    return 0;
  } else if (command == "search") {
    if (args.size() != 3) {
      std::cerr << "Error: 'search' Requires exactly one image file.\n";
      return 1;
    }
    cmdSearch(args[2]);
    return 0;
  } else if (command == "strict") {
    if (args.size() != 3) {
      std::cerr << "Error: 'strict' Requires exactly one argument.\n";
      return 1;
    }
    bool val = (args[2] == "on");
    cmdStrict(val);
    return 0;
  } else {
    std::cerr << "Unknown command: " << command.toStdString() << "\n";
    return 1;
  }
}

void cmdTh(int thValue) {
  QString iniPath = QCoreApplication::applicationDirPath() + "/DupFind.ini";
  QSettings settings(iniPath, QSettings::IniFormat);
  settings.setValue("threshold", thValue);
  std::cout << "Successfully updated threshold to " << thValue << ".\n";
}

// GUI以外からディレクトリ追加を指示するコマンド
// 設定ファイル(INI)を更新した上で、バックグラウンドのワーカプロセスをデタッチ起動する
void cmdAdd(const QStringList &dirs) {
  QString iniPath = QCoreApplication::applicationDirPath() + "/DupFind.ini";
  QSettings settings(iniPath, QSettings::IniFormat);

  QStringList existingDirs = settings.value("directories").toStringList();
  bool changed = false;

  for (const QString &dir : dirs) {
    if (!existingDirs.contains(
            dir, Qt::CaseInsensitive)) { // 念のためWindowsなど考慮
      existingDirs.append(dir);
      changed = true;
    }
  }

  if (changed) {
    settings.setValue("directories", existingDirs);
  }

  std::cout << "Started background scan.\n";

  // デタッチしてバックグラウンドプロセスを起動
  QString program = QCoreApplication::applicationFilePath();
  QStringList workerArgs;
  workerArgs << "--worker-add" << dirs;

  QProcess::startDetached(program, workerArgs);
}

// バックグラウンドで画像ファイルのハッシュ値(dHash,
// pHash)を計算し、DBへ書き込む処理
// GUI動作との競合を避ける配慮などが実装されている
void workerAdd(const QStringList &dirs) {
  // バックグラウンド処理のためCPU優先度を下げる
  QThread::currentThread()->setPriority(QThread::IdlePriority);

  // GUIが起動しているか確認するための共有メモリ
  QSharedMemory sharedMem("DupFind_GUI_Instance");

  DatabaseManager dbManager("dupfind_cache.db");
  if (!dbManager.open())
    return;

  auto cachedList = dbManager.getAllImages();
  std::unordered_map<std::string, ImageData> cache;
  for (const auto &img : cachedList) {
    cache[img.path] = img;
  }

  const QStringList filters = {"*.jpg",  "*.png",  "*.jpeg", "*.bmp",
                               "*.webp", "*.tiff", "*.heic", "*.heif"};
  int count = 0;

  for (const QString &dirPath : dirs) {
    QDirIterator it(dirPath, filters, QDir::Files | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);

    while (it.hasNext()) {
      std::string stdPath = it.next().toStdString();

      // 少し粒度を荒くしてGUI起動チェック (10ファイルごと)
      if (count % 10 == 0) {
        if (sharedMem.attach()) {
          // GUIが共有メモリを確保している＝起動中
          sharedMem.detach();
          return; // 即座に終了
        }
      }
      count++;

      try {
        std::error_code ec;
        auto currentSize = std::filesystem::file_size(stdPath, ec);
        auto currentMtime = std::chrono::duration_cast<std::chrono::seconds>(
                                std::filesystem::last_write_time(stdPath, ec)
                                    .time_since_epoch())
                                .count();

        auto cacheIt = cache.find(stdPath);
        if (cacheIt != cache.end()) {
          if (cacheIt->second.file_size == static_cast<int64_t>(currentSize) &&
              cacheIt->second.timestamp == static_cast<int64_t>(currentMtime)) {
            continue; // すでに最新ハッシュ計算済み
          }
        }

        cv::Mat img = ImageHasher::loadImage(stdPath);
        if (img.empty())
          continue;

        ImageData data;
        data.path = stdPath;
        data.dhash = ImageHasher::calculateDHash(img);
        data.phash = ImageHasher::calculatePHash(img);
        data.timestamp = currentMtime;
        data.file_size = currentSize;
        data.is_searched = false;

        dbManager.addImage(data);

      } catch (...) {
        // ファイルIOエラー等はスキップ
      }
    }
  }
}

// 指定された1つの画像ファイルと類似する「DB上の全ての画像」を検索してパスを出力するコマンド
void cmdSearch(const QString &imageFile) {
  std::string stdPath = imageFile.toStdString();
  if (!std::filesystem::exists(stdPath)) {
    return;
  }

  cv::Mat img = ImageHasher::loadImage(stdPath);
  if (img.empty()) {
    return;
  }

  uint64_t dhash = ImageHasher::calculateDHash(img);
  uint64_t phash = ImageHasher::calculatePHash(img);

  QString iniPath = QCoreApplication::applicationDirPath() + "/DupFind.ini";
  QSettings settings(iniPath, QSettings::IniFormat);
  int threshold = settings.value("threshold", 5).toInt();
  bool strict = settings.value("strict_mode", false).toBool();

  DatabaseManager dbManager("dupfind_cache.db");
  if (!dbManager.open())
    return;

  auto allImages = dbManager.getAllImages();
  for (const auto &imgData : allImages) {
    int distD = ImageHasher::hammingDistance(dhash, imgData.dhash);
    int distP = ImageHasher::hammingDistance(phash, imgData.phash);

    bool similar = strict ? (distD <= threshold && distP <= threshold)
                          : (distD <= threshold || distP <= threshold);

    if (similar) {
      std::cout << imgData.path << "\n";
    }
  }
}

void cmdStrict(bool strict) {
  QString iniPath = QCoreApplication::applicationDirPath() + "/DupFind.ini";
  QSettings settings(iniPath, QSettings::IniFormat);
  settings.setValue("strict_mode", strict);
  std::cout << "Successfully updated strict mode to " << (strict ? "on" : "off")
            << ".\n";
}
