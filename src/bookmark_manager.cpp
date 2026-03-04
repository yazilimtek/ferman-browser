#include "bookmark_manager.h"
#include <gio/gio.h>
#include <algorithm>
#include <ctime>
#include <fstream>
#include <sstream>

namespace ferman {

static std::string JsonEscape(const std::string& s);  // forward

BookmarkManager& BookmarkManager::Get() {
    static BookmarkManager inst;
    return inst;
}

void BookmarkManager::Init(const std::string& json_path) {
    filepath_ = json_path;
    // Folders dosyası: bookmarks.json → bookmark_folders.json
    folders_path_ = json_path;
    auto dot = folders_path_.rfind('.');
    if (dot != std::string::npos)
        folders_path_ = folders_path_.substr(0, dot) + "_folders.json";
    else
        folders_path_ += "_folders.json";
    Load();
    LoadFolders();
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

void BookmarkManager::ReorderBookmark(const std::string& url, int new_index) {
    auto it = bookmarks_.begin();
    while (it != bookmarks_.end() && it->url != url) ++it;
    if (it == bookmarks_.end()) return;
    Bookmark bm = *it;
    bookmarks_.erase(it);
    if (new_index < 0) new_index = 0;
    if (new_index > (int)bookmarks_.size()) new_index = (int)bookmarks_.size();
    bookmarks_.insert(bookmarks_.begin() + new_index, bm);
    // sort_order güncelle
    for (int i = 0; i < (int)bookmarks_.size(); ++i)
        bookmarks_[i].sort_order = i;
    Save();
}

void BookmarkManager::AddFolder(const std::string& path) {
    if (path.empty()) return;
    // Zaten varsa ekleme
    for (auto& f : folders_) if (f.name == path) return;
    BookmarkFolder f;
    f.name = path;
    auto slash = path.rfind('/');
    if (slash != std::string::npos) {
        f.parent = path.substr(0, slash);
        f.label  = path.substr(slash + 1);
    } else {
        f.parent = "";
        f.label  = path;
    }
    folders_.push_back(f);
    SaveFolders();
}

void BookmarkManager::RemoveFolder(const std::string& path) {
    // İçindeki yer imlerini köke taşı
    for (auto& b : bookmarks_)
        if (b.folder == path || b.folder.rfind(path + "/", 0) == 0)
            b.folder = "";
    Save();
    // Alt klasörleri de sil
    folders_.erase(
        std::remove_if(folders_.begin(), folders_.end(),
            [&path](const BookmarkFolder& f){
                return f.name == path || f.name.rfind(path + "/", 0) == 0;
            }),
        folders_.end());
    SaveFolders();
}

void BookmarkManager::RenameFolder(const std::string& old_path, const std::string& new_name) {
    if (old_path.empty() || new_name.empty()) return;
    // Yeni tam yolu hesapla
    std::string new_path;
    auto slash = old_path.rfind('/');
    if (slash != std::string::npos)
        new_path = old_path.substr(0, slash + 1) + new_name;
    else
        new_path = new_name;

    // Klasör listesinde güncelle (eski yol ile başlayan tüm klasörler)
    for (auto& f : folders_) {
        if (f.name == old_path) {
            f.name  = new_path;
            f.label = new_name;
        } else if (f.name.rfind(old_path + "/", 0) == 0) {
            f.name = new_path + f.name.substr(old_path.size());
        }
        if (f.parent == old_path) f.parent = new_path;
        else if (f.parent.rfind(old_path + "/", 0) == 0)
            f.parent = new_path + f.parent.substr(old_path.size());
    }
    SaveFolders();
    // Yer imlerinde güncelle
    for (auto& b : bookmarks_) {
        if (b.folder == old_path)
            b.folder = new_path;
        else if (b.folder.rfind(old_path + "/", 0) == 0)
            b.folder = new_path + b.folder.substr(old_path.size());
    }
    Save();
}

std::vector<BookmarkFolder> BookmarkManager::GetFolders() const {
    return folders_;
}

std::vector<BookmarkFolder> BookmarkManager::GetSubFolders(const std::string& parent) const {
    std::vector<BookmarkFolder> result;
    for (const auto& f : folders_)
        if (f.parent == parent) result.push_back(f);
    return result;
}

void BookmarkManager::Remove(const std::string& url) {
    auto it = bookmarks_.begin();
    while (it != bookmarks_.end()) {
        if (it->url == url) { it = bookmarks_.erase(it); }
        else ++it;
    }
    Save();
}

// ── Folders JSON save/load ───────────────────────────────────────────────────
void BookmarkManager::SaveFolders() {
    std::ofstream f(folders_path_);
    if (!f) return;
    f << "[\n";
    for (size_t i = 0; i < folders_.size(); ++i) {
        const auto& fd = folders_[i];
        f << "  {\"name\":\"" << JsonEscape(fd.name)
          << "\",\"parent\":\"" << JsonEscape(fd.parent)
          << "\",\"label\":\"" << JsonEscape(fd.label) << "\"}";
        if (i+1 < folders_.size()) f << ",";
        f << "\n";
    }
    f << "]\n";
}

void BookmarkManager::LoadFolders() {
    folders_.clear();
    // Yer imlerinden mevcut klasörleri de otomatik topla (geriye dönük uyumluluk)
    std::vector<std::string> seen;
    for (const auto& b : bookmarks_) {
        if (b.folder.empty()) continue;
        bool found = false;
        for (auto& s : seen) if (s == b.folder) { found = true; break; }
        if (!found) seen.push_back(b.folder);
    }

    std::ifstream f(folders_path_);
    if (f) {
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        size_t pos = 0;
        auto skip_ws2 = [&]() { while (pos < content.size() && isspace(content[pos])) ++pos; };
        auto read_str2 = [&]() -> std::string {
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
        skip_ws2();
        if (pos < content.size() && content[pos] == '[') {
            ++pos;
            while (pos < content.size()) {
                skip_ws2();
                if (content[pos] == ']') break;
                if (content[pos] == '{') {
                    ++pos;
                    BookmarkFolder fd;
                    while (pos < content.size() && content[pos] != '}') {
                        skip_ws2();
                        if (content[pos] == '"') {
                            std::string key = read_str2();
                            skip_ws2();
                            if (pos < content.size() && content[pos] == ':') ++pos;
                            skip_ws2();
                            if      (key == "name")   fd.name   = read_str2();
                            else if (key == "parent") fd.parent = read_str2();
                            else if (key == "label")  fd.label  = read_str2();
                            else { if (content[pos] == '"') read_str2(); }
                        } else ++pos;
                        skip_ws2();
                        if (pos < content.size() && content[pos] == ',') ++pos;
                    }
                    if (pos < content.size()) ++pos;
                    if (!fd.name.empty()) {
                        if (fd.label.empty()) {
                            auto sl = fd.name.rfind('/');
                            fd.label = sl != std::string::npos ? fd.name.substr(sl+1) : fd.name;
                        }
                        // seen listesinden çıkar
                        auto sit = std::find(seen.begin(), seen.end(), fd.name);
                        if (sit != seen.end()) seen.erase(sit);
                        folders_.push_back(fd);
                    }
                } else ++pos;
                skip_ws2();
                if (pos < content.size() && content[pos] == ',') ++pos;
            }
        }
    }
    // Dosyada olmayan ama bookmark'larda geçen klasörleri ekle
    for (const auto& s : seen) {
        BookmarkFolder fd;
        fd.name = s;
        auto sl = s.rfind('/');
        fd.parent = sl != std::string::npos ? s.substr(0, sl) : "";
        fd.label  = sl != std::string::npos ? s.substr(sl+1) : s;
        folders_.push_back(fd);
    }
    if (!seen.empty()) SaveFolders();
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

} // namespace ferman
