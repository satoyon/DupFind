#include "DatabaseManager.hpp"
#include <iostream>
#include <sstream>
#include <optional>
#include <filesystem>
#include <chrono>
#include <vector>
#include <string>

DatabaseManager::DatabaseManager(const std::string& dbPath) : m_dbPath(dbPath) {}

DatabaseManager::~DatabaseManager() {
    close();
}

bool DatabaseManager::open() {
    if (sqlite3_open(m_dbPath.c_str(), &m_db) != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }
    // WALモードの有効化 (並列性と速度の向上)
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    return initSchema();
}

void DatabaseManager::close() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

// データベースのテーブル構成（スキーマ）を初期化する
// 初回起動時ならテーブルを作成、既存ならカラム追加などのマイグレーションを行う
bool DatabaseManager::initSchema() {
    const char* sql = "CREATE TABLE IF NOT EXISTS images ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "path TEXT UNIQUE,"
                      "dhash INTEGER,"
                      "phash INTEGER,"
                      "timestamp INTEGER,"
                      "file_size INTEGER,"
                      "is_searched INTEGER DEFAULT 0);";
    char* errMsg = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    // Migration: try to add file_size if it doesn't exist (silently ignore error if it does)
    sqlite3_exec(m_db, "ALTER TABLE images ADD COLUMN file_size INTEGER;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "ALTER TABLE images ADD COLUMN is_searched INTEGER DEFAULT 0;", nullptr, nullptr, nullptr);
    return true;
}

// 既存パスなら上書き(REPLACE)、新規なら挿入(INSERT)でDBに画像データを追加する
bool DatabaseManager::addImage(const ImageData& data) {
    const char* sql = "INSERT OR REPLACE INTO images (path, dhash, phash, timestamp, file_size, is_searched) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, data.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(data.dhash));
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(data.phash));
    sqlite3_bind_int64(stmt, 4, data.timestamp);
    sqlite3_bind_int64(stmt, 5, data.file_size);
    sqlite3_bind_int(stmt, 6, data.is_searched ? 1 : 0);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

std::vector<ImageData> DatabaseManager::getAllImages() {
    std::vector<ImageData> results;
    const char* sql = "SELECT id, path, dhash, phash, timestamp, file_size, is_searched FROM images;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ImageData data;
        data.id = sqlite3_column_int64(stmt, 0);
        data.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        data.dhash = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2));
        data.phash = static_cast<uint64_t>(sqlite3_column_int64(stmt, 3));
        data.timestamp = sqlite3_column_int64(stmt, 4);
        data.file_size = sqlite3_column_int64(stmt, 5);
        data.is_searched = sqlite3_column_int(stmt, 6) != 0;
        results.push_back(data);
    }
    sqlite3_finalize(stmt);
    return results;
}

bool DatabaseManager::removeImage(const std::string& path) {
    const char* sql = "DELETE FROM images WHERE path = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}
 
#include <filesystem>
// DBにはあるがディスク上に実ファイルが存在しない「古いゴミデータ」を削除しクリーンナップする
void DatabaseManager::cleanupStaleEntries() {
    auto images = getAllImages();
    beginTransaction();
    for (const auto& img : images) {
        if (!std::filesystem::exists(img.path)) {
            removeImage(img.path);
        }
    }
    commitTransaction();
}
 
void DatabaseManager::beginTransaction() {
    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
}
 
void DatabaseManager::commitTransaction() {
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
}
 
void DatabaseManager::rollbackTransaction() {
    sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
}
 
std::optional<ImageData> DatabaseManager::getImageByPath(const std::string& path) {
    const char* sql = "SELECT id, path, dhash, phash, timestamp, file_size, is_searched FROM images WHERE path = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
 
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
 
    std::optional<ImageData> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        ImageData data;
        data.id = sqlite3_column_int64(stmt, 0);
        data.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        data.dhash = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2));
        data.phash = static_cast<uint64_t>(sqlite3_column_int64(stmt, 3));
        data.timestamp = sqlite3_column_int64(stmt, 4);
        data.file_size = sqlite3_column_int64(stmt, 5);
        data.is_searched = sqlite3_column_int(stmt, 6) != 0;
        result = data;
    }
    sqlite3_finalize(stmt);
    return result;
}

bool DatabaseManager::setDirectorySearchedStatus(const std::string& dirPath, bool isSearched) {
    const char* sql = "UPDATE images SET is_searched = ? WHERE path LIKE ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, isSearched ? 1 : 0);
    
    std::string pattern = dirPath;
    if (!pattern.empty() && pattern.back() != '/' && pattern.back() != '\\') {
        pattern += "/";
    }
    pattern += "%";

    sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}
