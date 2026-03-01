#pragma once
#include <string>
#include <vector>

namespace ferman {

struct Bookmark {
    std::string url;
    std::string title;
    std::string folder;   // boş = kök, dolu = klasör adı
    int64_t     added_at = 0;
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
    const std::vector<Bookmark>& All() const { return bookmarks_; }

private:
    BookmarkManager() = default;
    void Load();
    void Save();

    std::string filepath_;
    std::vector<Bookmark> bookmarks_;
};

} // namespace ferman
