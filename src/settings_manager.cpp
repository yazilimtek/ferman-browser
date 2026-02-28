#include "settings_manager.h"
#include <glib.h>
#include <string>

namespace ferzan {

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

    GError* err = nullptr;
    if (!g_key_file_save_to_file(kf, filepath_.c_str(), &err)) {
        if (err) g_error_free(err);
    }
    g_key_file_free(kf);
}

} // namespace ferzan
