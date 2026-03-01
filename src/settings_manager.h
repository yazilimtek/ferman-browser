#pragma once
#include <string>
#include <vector>

namespace ferman {

struct Settings {
    std::string homepage         = "ferman://home";
    double      default_zoom     = 1.0;
    bool        javascript_enabled = true;
    bool        hardware_accel   = false;  // varsayılan NEVER (offset bug)
    std::string language         = "tr";
    std::string search_engine    = "google";  // google|bing|yahoo|yandex|duckduckgo|baidu
    std::string download_dir      = "";        // boşsa ~/İndirilenler
    int         history_days      = 90;        // geçmiş sakla (gün), 0=sonsuz
    int         max_tabs          = 20;        // maks sekme sayısı
    bool        restore_tabs      = false;     // başlatmada sekmeleri geri yükle
    int         font_size         = 16;        // varsayılan yazı boyutu (px)
    int         min_font_size     = 10;        // minimum yazı boyutu (px)
    // Yapay Zeka — eski tek-ajan alanlar (geriye dönük uyumluluk)
    std::string ai_provider       = "deepseek";
    std::string ai_api_key        = "";
    std::string ai_model          = "deepseek-chat";
    std::string ai_base_url       = "";
};

// Yapay zeka ajanı
struct AiAgent {
    std::string id;        // benzersiz (uuid benzeri string)
    std::string name;      // kullanıcının verdiği isim
    std::string api_key;
    std::string api_url;   // boşsa varsayılan endpoint
    std::string model;
    // Provider API key önekinden otomatik belirlenir
    static std::string DetectProvider(const std::string& api_key) {
        if (api_key.rfind("sk-ant-", 0) == 0)  return "anthropic";
        if (api_key.rfind("sk-or-",  0) == 0)  return "openrouter";
        if (api_key.rfind("sk-",     0) == 0)  return "openai";
        if (api_key.rfind("ds-",     0) == 0)  return "deepseek";
        if (api_key.rfind("gsk_",    0) == 0)  return "groq";
        return "deepseek"; // varsayılan
    }
};

class AiAgentStore {
public:
    static AiAgentStore& Get();
    void Init(const std::string& config_dir);
    void Save();
    std::vector<AiAgent>& Agents() { return agents_; }
    const std::vector<AiAgent>& Agents() const { return agents_; }
    AiAgent* FindById(const std::string& id);
    std::string AddAgent(const AiAgent& a);  // returns id
    void RemoveAgent(const std::string& id);
private:
    AiAgentStore() = default;
    std::string filepath_;
    std::vector<AiAgent> agents_;
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

} // namespace ferman
