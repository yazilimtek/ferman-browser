#include "bookmark_manager.h"
#include <gio/gio.h>
#include <ctime>
#include <fstream>
#include <sstream>

namespace ferzan {

BookmarkManager& BookmarkManager::Get() {
    static BookmarkManager inst;
    return inst;
}

void BookmarkManager::Init(const std::string& json_path) {
    filepath_ = json_path;
    Load();
}

bool BookmarkManager::IsBookmarked(const std::string& url) const {
    for (const auto& b : bookmarks_)
        if (b.url == url) return true;
    return false;
}

void BookmarkManager::Add(const std::string& url, const std::string& title, const std::string& folder) {
    if (IsBookmarked(url)) return;
    Bookmark b;
    b.url      = url;
    b.title    = title;
    b.folder   = folder;
    b.added_at = static_cast<int64_t>(time(nullptr));
    bookmarks_.push_back(b);
    Save();
}

void BookmarkManager::Rename(const std::string& url, const std::string& new_title) {
    for (auto& b : bookmarks_)
        if (b.url == url) { b.title = new_title; break; }
    Save();
}

void BookmarkManager::MoveToFolder(const std::string& url, const std::string& folder) {
    for (auto& b : bookmarks_)
        if (b.url == url) { b.folder = folder; break; }
    Save();
}

void BookmarkManager::Remove(const std::string& url) {
    auto it = bookmarks_.begin();
    while (it != bookmarks_.end()) {
        if (it->url == url) { it = bookmarks_.erase(it); }
        else ++it;
    }
    Save();
}

// Minimal JSON parser/writer (harici bağımlılık olmadan)
static std::string JsonEscape(const std::string& s) {
    std::string r;
    for (char c : s) {
        if      (c == '"')  r += "\\\"";
        else if (c == '\\') r += "\\\\";
        else if (c == '\n') r += "\\n";
        else if (c == '\r') r += "\\r";
        else                r += c;
    }
    return r;
}

static std::string JsonUnescape(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i+1 < s.size()) {
            ++i;
            if      (s[i] == '"')  r += '"';
            else if (s[i] == '\\') r += '\\';
            else if (s[i] == 'n')  r += '\n';
            else if (s[i] == 'r')  r += '\r';
            else { r += '\\'; r += s[i]; }
        } else r += s[i];
    }
    return r;
}

void BookmarkManager::Save() {
    std::ofstream f(filepath_);
    if (!f) return;
    f << "[\n";
    for (size_t i = 0; i < bookmarks_.size(); ++i) {
        const auto& b = bookmarks_[i];
        f << "  {\"url\":\"" << JsonEscape(b.url)
          << "\",\"title\":\"" << JsonEscape(b.title)
          << "\",\"folder\":\"" << JsonEscape(b.folder)
          << "\",\"added_at\":" << b.added_at << "}";
        if (i+1 < bookmarks_.size()) f << ",";
        f << "\n";
    }
    f << "]\n";
}

void BookmarkManager::Load() {
    bookmarks_.clear();
    std::ifstream f(filepath_);
    if (!f) return;
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    // Minimal JSON array parser
    size_t pos = 0;
    auto skip_ws = [&]() { while (pos < content.size() && isspace(content[pos])) ++pos; };
    auto read_string = [&]() -> std::string {
        if (pos >= content.size() || content[pos] != '"') return "";
        ++pos;
        std::string s;
        while (pos < content.size() && content[pos] != '"') {
            if (content[pos] == '\\' && pos+1 < content.size()) {
                ++pos;
                if      (content[pos] == '"')  s += '"';
                else if (content[pos] == '\\') s += '\\';
                else if (content[pos] == 'n')  s += '\n';
                else { s += '\\'; s += content[pos]; }
            } else s += content[pos];
            ++pos;
        }
        if (pos < content.size()) ++pos;
        return s;
    };

    skip_ws();
    if (pos >= content.size() || content[pos] != '[') return;
    ++pos;
    while (pos < content.size()) {
        skip_ws();
        if (content[pos] == ']') break;
        if (content[pos] == '{') {
            ++pos;
            Bookmark b;
            while (pos < content.size() && content[pos] != '}') {
                skip_ws();
                if (content[pos] == '"') {
                    std::string key = read_string();
                    skip_ws();
                    if (pos < content.size() && content[pos] == ':') ++pos;
                    skip_ws();
                    if (key == "url")   b.url   = read_string();
                    else if (key == "title") b.title = read_string();
                    else if (key == "folder") b.folder = read_string();
                    else if (key == "added_at") {
                        std::string num;
                        while (pos < content.size() && (isdigit(content[pos]) || content[pos] == '-'))
                            num += content[pos++];
                        if (!num.empty()) b.added_at = std::stoll(num);
                    } else {
                        // skip value
                        if (content[pos] == '"') read_string();
                        else while (pos < content.size() && content[pos] != ',' && content[pos] != '}') ++pos;
                    }
                } else ++pos;
                skip_ws();
                if (pos < content.size() && content[pos] == ',') ++pos;
            }
            if (pos < content.size()) ++pos;
            if (!b.url.empty()) bookmarks_.push_back(b);
        } else ++pos;
        skip_ws();
        if (pos < content.size() && content[pos] == ',') ++pos;
    }
}

} // namespace ferzan
