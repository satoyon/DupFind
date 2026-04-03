#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <sqlite3.h>

struct ImageData {
    int64_t id;
    std::string path;
    uint64_t dhash;
    uint64_t phash;
    int64_t timestamp;
    int64_t file_size;
    bool is_searched = false;
};

class DatabaseManager {
public:
    DatabaseManager(const std::string& dbPath);
    ~DatabaseManager();

    bool open();
    void close();

    bool addImage(const ImageData& data);
    std::vector<ImageData> getAllImages();
    bool removeImage(const std::string& path);
    void cleanupStaleEntries();
 
    void beginTransaction();
    void commitTransaction();
    void rollbackTransaction();
 
    std::optional<ImageData> getImageByPath(const std::string& path);
    
    bool setDirectorySearchedStatus(const std::string& dirPath, bool isSearched);

private:
    std::string m_dbPath;
    sqlite3* m_db = nullptr;

    bool initSchema();
};
