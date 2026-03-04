#pragma once
#include <string>
#include <vector>

namespace ferman {

struct Bookmark {
    std::string url;
    std::string title;
    std::string folder;   // boş = kök, dolu = klasör yolu ("ana/alt" formatında iç içe)
    int64_t     added_at = 0;
    int         sort_order = 0;  // sürükleme sıralaması
};

struct BookmarkFolder {
    std::string name;    // klasör tam yolu ("ana" veya "ana/alt")
    std::string parent;  // üst klasör yolu (kök için boş)
    std::string label;   // gösterim ismi (sadece son kısım)
};

class BookmarkManager {
public:
    static BookmarkManager& Get();

    void Init(const std::string& json_path);
    bool IsBookmarked(const std::string& url) const;
    void Add(const std::string& url, const std::string& title, const std::string& folder = "");
    void Remove(const std::string& url);
    void Rename(const std::string& url, const std::string& new_title);
    void MoveToFolder(const std::string& url, const std::string& folder);
    void ReorderBookmark(const std::string& url, int new_index);
    const std::vector<Bookmark>& All() const { return bookmarks_; }

    // Klasör yönetimi
    void AddFolder(const std::string& path);           // "ana" veya "ana/alt"
    void RemoveFolder(const std::string& path);        // klasör + içindekiler silinir
    void RenameFolder(const std::string& old_path, const std::string& new_name);
    std::vector<BookmarkFolder> GetFolders() const;    // tüm klasörler
    std::vector<BookmarkFolder> GetSubFolders(const std::string& parent) const;

private:
    BookmarkManager() = default;
    void Load();
    void Save();
    void SaveFolders();
    void LoadFolders();

    std::string filepath_;
    std::string folders_path_;  // ayrı folders.json
    std::vector<Bookmark>       bookmarks_;
    std::vector<BookmarkFolder> folders_;  // explicit klasör listesi
};

} // namespace ferman
