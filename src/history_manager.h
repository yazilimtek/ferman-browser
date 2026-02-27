#pragma once
#include <string>
#include <vector>
#include <sqlite3.h>

namespace ferzan {

struct HistoryEntry {
    int64_t     id;
    std::string url;
    std::string title;
    int64_t     visited_at;  // unix timestamp
};

class HistoryManager {
public:
    static HistoryManager& Get();

    void Init(const std::string& db_path);
    void AddVisit(const std::string& url, const std::string& title);
    std::vector<HistoryEntry> Query(const std::string& prefix, int limit = 8) const;
    std::vector<HistoryEntry> Recent(int limit = 50) const;
    void Clear();

private:
    HistoryManager() = default;
    sqlite3* db_ = nullptr;
};

} // namespace ferzan
