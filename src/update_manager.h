#ifndef UPDATE_MANAGER_H
#define UPDATE_MANAGER_H

#include <string>
#include <functional>
#include <libsoup/soup.h>

namespace ferman {

struct UpdateInfo {
    std::string version;
    std::string download_url_deb;
    std::string download_url_tar;
    std::string release_notes;
    std::string published_at;
    bool is_newer = false;
};

class UpdateManager {
public:
    static UpdateManager& Get();

    // GitHub Releases API'den en son sürümü kontrol et
    void CheckForUpdates(std::function<void(const UpdateInfo&)> callback);
    
    // Mevcut versiyon
    std::string GetCurrentVersion() const;
    
    // Versiyon karşılaştırma (semantic versioning)
    bool IsNewerVersion(const std::string& current, const std::string& latest) const;

private:
    UpdateManager() = default;
    ~UpdateManager() = default;
    UpdateManager(const UpdateManager&) = delete;
    UpdateManager& operator=(const UpdateManager&) = delete;

    static void OnResponseReceived(SoupSession* session, GAsyncResult* res, gpointer user_data);
    
    std::string ParseLatestRelease(const std::string& json_response, UpdateInfo& info);
};

} // namespace ferman

#endif // UPDATE_MANAGER_H
