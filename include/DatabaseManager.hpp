#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <sqlite3.h>

// データベースに保存される画像情報を保持する構造体
struct ImageData {
    int64_t id;               // DBのプライマリキー
    std::string path;         // 画像のファイルパス
    uint64_t dhash;           // 計算済みのdHash値
    uint64_t phash;           // 計算済みのpHash値
    int64_t timestamp;        // ファイルの最終更新時刻(再計算判定用)
    int64_t file_size;        // ファイルサイズ(再計算判定用)
    bool is_searched = false; // 検索完了ステータス(GUI等用)
};

// SQLiteを用いた画像メタデータとハッシュのDB管理クラス
class DatabaseManager {
public:
    DatabaseManager(const std::string& dbPath);
    ~DatabaseManager();

    // データベースとの接続を開く
    bool open();
    // データベースを閉じる
    void close();

    // 画像データをDBに追加または更新する
    bool addImage(const ImageData& data);
    // DB内の全画像情報を取得する
    std::vector<ImageData> getAllImages();
    // 指定されたパスの画像をDBから削除する
    bool removeImage(const std::string& path);
    // 実体ファイルが削除済みの古いエントリをDBから消去する
    void cleanupStaleEntries();
 
    // トランザクション処理（大量追加時の高速化のため）
    void beginTransaction();
    void commitTransaction();
    void rollbackTransaction();
 
    // 単一パスの画像情報をDBから検索して返す
    std::optional<ImageData> getImageByPath(const std::string& path);
    
    // 指定ディレクトリ配下の全画像の検索完了状態を一括更新する
    bool setDirectorySearchedStatus(const std::string& dirPath, bool isSearched);

private:
    std::string m_dbPath;
    sqlite3* m_db = nullptr;

    bool initSchema();
};
