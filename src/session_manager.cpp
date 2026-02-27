#include "session_manager.h"
#include <gio/gio.h>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace ferzan {

// CDN URL pattern → local filename eşlemesi
// Host prefix eşleştirmesi: URL host bu listedekilerden biriyle başlıyorsa intercept et
static const struct CdnPattern {
    const char* host_contains;  // URL host'unda aranacak dize
    const char* path_prefix;    // URL path prefix (boş = tümü)
} kCdnPatterns[] = {
    { "cdn.jsdelivr.net",         "" },
    { "cdnjs.cloudflare.com",     "" },
    { "code.jquery.com",          "" },
    { "maxcdn.bootstrapcdn.com",  "" },
    { "stackpath.bootstrapcdn.com","" },
    { "ajax.googleapis.com",      "/ajax/libs/" },
    { "unpkg.com",                "" },
    { "cdn.tailwindcss.com",      "" },
    { "cdn.staticfile.org",       "" },
};

static bool IsCdnUrl(const char* uri) {
    if (!uri) return false;
    for (auto& p : kCdnPatterns) {
        if (strstr(uri, p.host_contains)) {
            if (!p.path_prefix[0] || strstr(uri, p.path_prefix))
                return true;
        }
    }
    return false;
}

// URI'den güvenli dosya adı türet (/ → _)
static std::string UriToFilename(const char* uri) {
    std::string s = uri;
    // scheme'i kaldır
    auto pos = s.find("://");
    if (pos != std::string::npos) s = s.substr(pos + 3);
    // geçersiz karakterleri _ ile değiştir
    for (char& c : s) {
        if (c == '/' || c == '?' || c == '&' || c == '=' || c == ':')
            c = '_';
    }
    if (s.size() > 200) s = s.substr(0, 200);
    return s;
}

// ── Singleton ──────────────────────────────────────────────────────────────

SessionManager& SessionManager::Get() {
    static SessionManager inst;
    return inst;
}

void SessionManager::EnsureDirs() {
    const char* home = g_get_home_dir();

    data_dir_  = std::string(home) + "/.local/share/ferzan-browser";
    cache_dir_ = std::string(home) + "/.cache/ferzan-browser";
    cdn_dir_   = cache_dir_ + "/cdn";

    for (const auto& d : { data_dir_, cache_dir_, cdn_dir_ }) {
        g_mkdir_with_parents(d.c_str(), 0755);
    }
}

// CDN intercept: "ferzan-cdn" URI şeması — doğrudan dosya servisi
// WebKit content URI şeması üzerinden yerel CDN dosyaları sunulur.
void SessionManager::SetupCdnScheme() {
    // "ferzan-cdn://cdn/<filename>" → cdn_dir_/filename
    webkit_web_context_register_uri_scheme(context_, "ferzan-cdn",
        [](WebKitURISchemeRequest* request, gpointer ud) {
            auto* self = static_cast<SessionManager*>(ud);
            const char* path = webkit_uri_scheme_request_get_path(request);
            // path: /cdn/<filename>
            std::string filepath = self->cdn_dir_;
            if (path && *path) {
                // strip leading /cdn/
                const char* fn = strstr(path, "/cdn/");
                filepath += "/";
                filepath += (fn ? fn + 5 : path + 1);
            }
            GFile* file = g_file_new_for_path(filepath.c_str());
            GError* err = nullptr;
            GFileInputStream* fis = g_file_read(file, nullptr, &err);
            if (fis) {
                // MIME türünü uzantıdan tahmin et
                const char* mime = "application/octet-stream";
                if (filepath.rfind(".js") == filepath.size()-3)   mime = "application/javascript";
                else if (filepath.rfind(".css") == filepath.size()-4) mime = "text/css";
                GFileInfo* fi = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                                  G_FILE_QUERY_INFO_NONE, nullptr, nullptr);
                goffset size = fi ? g_file_info_get_size(fi) : -1;
                if (fi) g_object_unref(fi);
                webkit_uri_scheme_request_finish(request,
                    G_INPUT_STREAM(fis), size, mime);
                g_object_unref(fis);
            } else {
                webkit_uri_scheme_request_finish_error(request, err);
                if (err) g_error_free(err);
            }
            g_object_unref(file);
        }, this, nullptr);
}

void SessionManager::Init() {
    EnsureDirs();

    // Kalıcı network session oluştur
    session_ = webkit_network_session_new(data_dir_.c_str(), cache_dir_.c_str());

    // Favicon DB
    WebKitWebsiteDataManager* dm =
        webkit_network_session_get_website_data_manager(session_);
    webkit_website_data_manager_set_favicons_enabled(dm, TRUE);

    // ITP (Intelligent Tracking Prevention) kapat — site uyumluluğu için
    webkit_network_session_set_itp_enabled(session_, FALSE);

    // Kalıcı kimlik bilgileri (login oturumları)
    webkit_network_session_set_persistent_credential_storage_enabled(session_, TRUE);

    // Web context (cache modeli)
    context_ = webkit_web_context_get_default();
    webkit_web_context_set_cache_model(context_, WEBKIT_CACHE_MODEL_WEB_BROWSER);

    // CDN URI şeması kaydet
    SetupCdnScheme();
}

} // namespace ferzan
