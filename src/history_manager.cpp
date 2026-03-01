#include "history_manager.h"
#include <ctime>
#include <stdexcept>

namespace ferman {

HistoryManager& HistoryManager::Get() {
    static HistoryManager inst;
    return inst;
}

void HistoryManager::Init(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) return;
    sqlite3_exec(db_,
        "CREATE TABLE IF NOT EXISTS visits ("
        "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  url        TEXT NOT NULL,"
        "  title      TEXT,"
        "  visited_at INTEGER NOT NULL"
        ");",
        nullptr, nullptr, nullptr);
    sqlite3_exec(db_,
        "CREATE INDEX IF NOT EXISTS idx_url ON visits(url);",
        nullptr, nullptr, nullptr);
}

void HistoryManager::AddVisit(const std::string& url, const std::string& title) {
    if (!db_ || url.empty()) return;
    int64_t now = static_cast<int64_t>(time(nullptr));
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO visits(url, title, visited_at) VALUES(?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, url.c_str(),   -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, now);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<HistoryEntry> HistoryManager::Query(const std::string& prefix, int limit) const {
    std::vector<HistoryEntry> result;
    if (!db_ || prefix.empty()) return result;
    const char* sql =
        "SELECT id, url, title, visited_at FROM visits"
        " WHERE url LIKE ? OR title LIKE ?"
        " ORDER BY visited_at DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pat = "%" + prefix + "%";
        sqlite3_bind_text(stmt, 1, pat.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pat.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt,  3, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            HistoryEntry e;
            e.id         = sqlite3_column_int64(stmt, 0);
            e.url        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            auto* t      = sqlite3_column_text(stmt, 2);
            e.title      = t ? reinterpret_cast<const char*>(t) : "";
            e.visited_at = sqlite3_column_int64(stmt, 3);
            result.push_back(e);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

std::vector<HistoryEntry> HistoryManager::Recent(int limit) const {
    std::vector<HistoryEntry> result;
    if (!db_) return result;
    const char* sql =
        "SELECT id, url, title, visited_at FROM visits"
        " ORDER BY visited_at DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            HistoryEntry e;
            e.id         = sqlite3_column_int64(stmt, 0);
            e.url        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            auto* t      = sqlite3_column_text(stmt, 2);
            e.title      = t ? reinterpret_cast<const char*>(t) : "";
            e.visited_at = sqlite3_column_int64(stmt, 3);
            result.push_back(e);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

void HistoryManager::Clear() {
    if (!db_) return;
    sqlite3_exec(db_, "DELETE FROM visits;", nullptr, nullptr, nullptr);
}

} // namespace ferman
