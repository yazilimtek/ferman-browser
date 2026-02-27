#pragma once
#include <string>

namespace ferzan {

struct Settings {
    std::string homepage         = "ferzan://home";
    double      default_zoom     = 1.0;
    bool        javascript_enabled = true;
    bool        hardware_accel   = false;  // varsayılan NEVER (offset bug)
    std::string language         = "tr";
};

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
