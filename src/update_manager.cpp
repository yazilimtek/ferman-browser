#include "update_manager.h"
#include <json-glib/json-glib.h>
#include <cstring>
#include <sstream>
#include <vector>

namespace ferman {

constexpr const char* GITHUB_API_URL = "https://api.github.com/repos/yazilimtek/ferman-browser/releases/latest";
constexpr const char* CURRENT_VERSION = "1.0.2";

UpdateManager& UpdateManager::Get() {
    static UpdateManager instance;
    return instance;
}

std::string UpdateManager::GetCurrentVersion() const {
    return CURRENT_VERSION;
}

bool UpdateManager::IsNewerVersion(const std::string& current, const std::string& latest) const {
    // Semantic versioning karşılaştırması: v1.2.3 formatı
    auto parse_version = [](const std::string& v) -> std::vector<int> {
        std::vector<int> parts;
        std::string ver = v;
        // "v" prefix'ini kaldır
        if (!ver.empty() && ver[0] == 'v') ver = ver.substr(1);
        
        std::stringstream ss(ver);
        std::string part;
        while (std::getline(ss, part, '.')) {
            try {
                parts.push_back(std::stoi(part));
            } catch (...) {
                parts.push_back(0);
            }
        }
        // En az 3 parça olsun (major.minor.patch)
        while (parts.size() < 3) parts.push_back(0);
        return parts;
    };

    auto curr_parts = parse_version(current);
    auto latest_parts = parse_version(latest);

    // Major.Minor.Patch karşılaştırması
    for (size_t i = 0; i < 3; ++i) {
        if (latest_parts[i] > curr_parts[i]) return true;
        if (latest_parts[i] < curr_parts[i]) return false;
    }
    return false; // Eşit
}

struct UpdateCheckData {
    std::function<void(const UpdateInfo&)> callback;
    UpdateManager* manager;
};

void UpdateManager::CheckForUpdates(std::function<void(const UpdateInfo&)> callback) {
    SoupSession* session = soup_session_new();
    SoupMessage* msg = soup_message_new("GET", GITHUB_API_URL);
    
    // GitHub API için User-Agent gerekli
    SoupMessageHeaders* headers = soup_message_get_request_headers(msg);
    soup_message_headers_append(headers, "User-Agent", "Ferman-Browser");
    soup_message_headers_append(headers, "Accept", "application/vnd.github.v3+json");

    auto* data = new UpdateCheckData{callback, this};
    
    soup_session_send_and_read_async(
        session,
        msg,
        G_PRIORITY_DEFAULT,
        nullptr,
        (GAsyncReadyCallback)OnResponseReceived,
        data
    );
    
    g_object_unref(msg);
    g_object_unref(session);
}

void UpdateManager::OnResponseReceived(SoupSession* session, GAsyncResult* res, gpointer user_data) {
    auto* data = static_cast<UpdateCheckData*>(user_data);
    UpdateInfo info;
    
    GError* error = nullptr;
    GBytes* bytes = soup_session_send_and_read_finish(session, res, &error);
    
    if (error) {
        g_error_free(error);
        info.is_newer = false;
        data->callback(info);
        delete data;
        return;
    }
    
    gsize size;
    const char* response_data = static_cast<const char*>(g_bytes_get_data(bytes, &size));
    std::string json_response(response_data, size);
    g_bytes_unref(bytes);
    
    // JSON parse et
    data->manager->ParseLatestRelease(json_response, info);
    
    // Versiyon karşılaştır
    std::string current = data->manager->GetCurrentVersion();
    info.is_newer = data->manager->IsNewerVersion(current, info.version);
    
    data->callback(info);
    delete data;
}

std::string UpdateManager::ParseLatestRelease(const std::string& json_response, UpdateInfo& info) {
    GError* error = nullptr;
    JsonParser* parser = json_parser_new();
    
    if (!json_parser_load_from_data(parser, json_response.c_str(), -1, &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        return "";
    }
    
    JsonNode* root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        return "";
    }
    
    JsonObject* obj = json_node_get_object(root);
    
    // Tag name (versiyon)
    if (json_object_has_member(obj, "tag_name")) {
        info.version = json_object_get_string_member(obj, "tag_name");
    }
    
    // Release notes
    if (json_object_has_member(obj, "body")) {
        info.release_notes = json_object_get_string_member(obj, "body");
    }
    
    // Yayın tarihi
    if (json_object_has_member(obj, "published_at")) {
        info.published_at = json_object_get_string_member(obj, "published_at");
    }
    
    // Assets (indirme linkleri)
    if (json_object_has_member(obj, "assets")) {
        JsonArray* assets = json_object_get_array_member(obj, "assets");
        guint len = json_array_get_length(assets);
        
        for (guint i = 0; i < len; ++i) {
            JsonObject* asset = json_array_get_object_element(assets, i);
            if (!asset) continue;
            
            const char* name = json_object_get_string_member(asset, "name");
            const char* url = json_object_get_string_member(asset, "browser_download_url");
            
            if (name && url) {
                std::string filename(name);
                if (filename.find(".deb") != std::string::npos) {
                    info.download_url_deb = url;
                } else if (filename.find(".tar.gz") != std::string::npos || 
                           filename.find(".tgz") != std::string::npos) {
                    info.download_url_tar = url;
                }
            }
        }
    }
    
    g_object_unref(parser);
    return info.version;
}

} // namespace ferman
