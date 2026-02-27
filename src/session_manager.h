#pragma once
#include <webkit/webkit.h>
#include <string>

namespace ferzan {

// Kalıcı disk cache + oturum yöneticisi (singleton)
// webkit_network_session_new ile ~/.local/share/ferzan-browser/ altında saklar.
class SessionManager {
public:
    static SessionManager& Get();

    // Uygulama başlangıcında bir kez çağrıl
    void Init();

    WebKitNetworkSession* GetSession() const { return session_; }
    WebKitWebContext*     GetContext()  const { return context_; }

    std::string DataDir()  const { return data_dir_; }
    std::string CacheDir() const { return cache_dir_; }
    std::string CdnDir()   const { return cdn_dir_;  }

private:
    SessionManager() = default;
    void EnsureDirs();
    void SetupCdnScheme();

    WebKitNetworkSession* session_  = nullptr;
    WebKitWebContext*     context_  = nullptr;
    std::string data_dir_;
    std::string cache_dir_;
    std::string cdn_dir_;
};

} // namespace ferzan
