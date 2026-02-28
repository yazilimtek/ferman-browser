#pragma once
#include <string>

namespace ferzan {

struct Settings {
    std::string homepage         = "ferzan://home";
    double      default_zoom     = 1.0;
    bool        javascript_enabled = true;
    bool        hardware_accel   = false;  // varsayılan NEVER (offset bug)
    std::string language         = "tr";
    std::string search_engine    = "google";  // google|bing|yahoo|yandex|duckduckgo|baidu
    std::string download_dir      = "";        // boşsa ~/İndirilenler
    int         history_days      = 90;        // geçmiş sakla (gün), 0=sonsuz
    int         max_tabs          = 20;        // maks sekme sayısı
    bool        restore_tabs      = false;     // başlatmada sekmeleri geri yükle
};

// Arama motoru URL'si döndürür (sorgu %s ile)
inline std::string SearchEngineUrl(const std::string& engine) {
    if (engine == "bing")       return "https://www.bing.com/search?q=";
    if (engine == "yahoo")      return "https://search.yahoo.com/search?p=";
    if (engine == "yandex")     return "https://yandex.com/search/?text=";
    if (engine == "duckduckgo") return "https://duckduckgo.com/?q=";
    if (engine == "baidu")      return "https://www.baidu.com/s?wd=";
    return "https://www.google.com/search?q=";  // varsayılan google
}

class SettingsManager {
public:
    static SettingsManager& Get();

    void Init(const std::string& config_dir);
    void Save();

    Settings& Prefs() { return settings_; }
    const Settings& Prefs() const { return settings_; }

private:
    SettingsManager() = default;
    void Load();

    std::string filepath_;
    Settings    settings_;
};

} // namespace ferzan
