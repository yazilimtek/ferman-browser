#include "settings_manager.h"
#include <glib.h>
#include <string>
#include <algorithm>
#include <ctime>

namespace ferman {

SettingsManager& SettingsManager::Get() {
    static SettingsManager inst;
    return inst;
}

void SettingsManager::Init(const std::string& config_dir) {
    g_mkdir_with_parents(config_dir.c_str(), 0755);
    filepath_ = config_dir + "/settings.ini";
    Load();
}

void SettingsManager::Load() {
    GKeyFile* kf = g_key_file_new();
    GError* err  = nullptr;
    if (!g_key_file_load_from_file(kf, filepath_.c_str(),
                                    G_KEY_FILE_NONE, &err)) {
        if (err) g_error_free(err);
        g_key_file_free(kf);
        return;
    }
    auto get_str = [&](const char* g, const char* k, const std::string& def) -> std::string {
        char* v = g_key_file_get_string(kf, g, k, nullptr);
        std::string r = v ? v : def;
        g_free(v);
        return r;
    };
    auto get_dbl = [&](const char* g, const char* k, double def) -> double {
        GError* e = nullptr;
        double v = g_key_file_get_double(kf, g, k, &e);
        if (e) { g_error_free(e); return def; }
        return v;
    };
    auto get_bool = [&](const char* g, const char* k, bool def) -> bool {
        GError* e = nullptr;
        gboolean v = g_key_file_get_boolean(kf, g, k, &e);
        if (e) { g_error_free(e); return def; }
        return static_cast<bool>(v);
    };

    settings_.homepage          = get_str ("General", "homepage",          settings_.homepage);
    settings_.default_zoom      = get_dbl ("General", "default_zoom",      settings_.default_zoom);
    settings_.javascript_enabled= get_bool("General", "javascript_enabled",settings_.javascript_enabled);
    settings_.hardware_accel    = get_bool("General", "hardware_accel",    settings_.hardware_accel);
    settings_.language          = get_str ("General", "language",          settings_.language);
    settings_.search_engine     = get_str ("General", "search_engine",     settings_.search_engine);
    settings_.download_dir       = get_str ("General", "download_dir",       settings_.download_dir);
    settings_.history_days       = (int)get_dbl("General", "history_days",  (double)settings_.history_days);
    settings_.max_tabs           = (int)get_dbl("General", "max_tabs",       (double)settings_.max_tabs);
    settings_.restore_tabs       = get_bool("General", "restore_tabs",      settings_.restore_tabs);
    settings_.font_size           = (int)get_dbl("General", "font_size",       (double)settings_.font_size);
    settings_.min_font_size       = (int)get_dbl("General", "min_font_size",   (double)settings_.min_font_size);
    settings_.ai_provider         = get_str ("AI", "provider",  settings_.ai_provider);
    settings_.ai_api_key          = get_str ("AI", "api_key",   settings_.ai_api_key);
    settings_.ai_model            = get_str ("AI", "model",     settings_.ai_model);
    settings_.ai_base_url         = get_str ("AI", "base_url",  settings_.ai_base_url);

    g_key_file_free(kf);
}

void SettingsManager::Save() {
    GKeyFile* kf = g_key_file_new();
    g_key_file_set_string (kf, "General", "homepage",          settings_.homepage.c_str());
    g_key_file_set_double (kf, "General", "default_zoom",      settings_.default_zoom);
    g_key_file_set_boolean(kf, "General", "javascript_enabled",settings_.javascript_enabled);
    g_key_file_set_boolean(kf, "General", "hardware_accel",    settings_.hardware_accel);
    g_key_file_set_string (kf, "General", "language",          settings_.language.c_str());
    g_key_file_set_string (kf, "General", "search_engine",     settings_.search_engine.c_str());
    g_key_file_set_string (kf, "General", "download_dir",       settings_.download_dir.c_str());
    g_key_file_set_double (kf, "General", "history_days",       (double)settings_.history_days);
    g_key_file_set_double (kf, "General", "max_tabs",           (double)settings_.max_tabs);
    g_key_file_set_boolean(kf, "General", "restore_tabs",       settings_.restore_tabs);
    g_key_file_set_double (kf, "General", "font_size",           (double)settings_.font_size);
    g_key_file_set_double (kf, "General", "min_font_size",       (double)settings_.min_font_size);
    g_key_file_set_string (kf, "AI",      "provider",            settings_.ai_provider.c_str());
    g_key_file_set_string (kf, "AI",      "api_key",             settings_.ai_api_key.c_str());
    g_key_file_set_string (kf, "AI",      "model",               settings_.ai_model.c_str());
    g_key_file_set_string (kf, "AI",      "base_url",            settings_.ai_base_url.c_str());

    GError* err = nullptr;
    if (!g_key_file_save_to_file(kf, filepath_.c_str(), &err)) {
        if (err) g_error_free(err);
    }
    g_key_file_free(kf);
}

// ── AiAgentStore ─────────────────────────────────────────────────────────────
AiAgentStore& AiAgentStore::Get() {
    static AiAgentStore inst;
    return inst;
}

void AiAgentStore::Init(const std::string& config_dir) {
    g_mkdir_with_parents(config_dir.c_str(), 0755);
    filepath_ = config_dir + "/ai_agents.ini";
    agents_.clear();
    GKeyFile* kf = g_key_file_new();
    GError* err = nullptr;
    if (!g_key_file_load_from_file(kf, filepath_.c_str(), G_KEY_FILE_NONE, &err)) {
        if (err) g_error_free(err);
        g_key_file_free(kf);
        return;
    }
    gsize n_groups = 0;
    gchar** groups = g_key_file_get_groups(kf, &n_groups);
    for (gsize i = 0; i < n_groups; ++i) {
        std::string grp = groups[i];
        if (grp.rfind("agent.", 0) != 0) continue;
        AiAgent a;
        a.id = grp.substr(6);
        auto gs = [&](const char* k, const std::string& def) -> std::string {
            char* v = g_key_file_get_string(kf, grp.c_str(), k, nullptr);
            std::string r = v ? v : def;
            g_free(v); return r;
        };
        a.name    = gs("name",    "");
        a.api_key = gs("api_key", "");
        a.api_url = gs("api_url", "");
        a.model   = gs("model",   "");
        agents_.push_back(a);
    }
    g_strfreev(groups);
    g_key_file_free(kf);
}

void AiAgentStore::Save() {
    GKeyFile* kf = g_key_file_new();
    for (const auto& a : agents_) {
        std::string grp = "agent." + a.id;
        g_key_file_set_string(kf, grp.c_str(), "name",    a.name.c_str());
        g_key_file_set_string(kf, grp.c_str(), "api_key", a.api_key.c_str());
        g_key_file_set_string(kf, grp.c_str(), "api_url", a.api_url.c_str());
        g_key_file_set_string(kf, grp.c_str(), "model",   a.model.c_str());
    }
    GError* err = nullptr;
    if (!g_key_file_save_to_file(kf, filepath_.c_str(), &err))
        if (err) g_error_free(err);
    g_key_file_free(kf);
}

AiAgent* AiAgentStore::FindById(const std::string& id) {
    for (auto& a : agents_)
        if (a.id == id) return &a;
    return nullptr;
}

std::string AiAgentStore::AddAgent(const AiAgent& a) {
    AiAgent copy = a;
    if (copy.id.empty()) {
        // basit id: timestamp + index
        copy.id = std::to_string((int64_t)time(nullptr)) + "_" + std::to_string(agents_.size());
    }
    // güncelle varsa
    for (auto& existing : agents_) {
        if (existing.id == copy.id) { existing = copy; Save(); return copy.id; }
    }
    agents_.push_back(copy);
    Save();
    return copy.id;
}

void AiAgentStore::RemoveAgent(const std::string& id) {
    agents_.erase(std::remove_if(agents_.begin(), agents_.end(),
        [&](const AiAgent& a){ return a.id == id; }), agents_.end());
    Save();
}

} // namespace ferman
