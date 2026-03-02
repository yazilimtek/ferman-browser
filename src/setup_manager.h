#pragma once
#include <string>
#include <functional>

namespace ferman {

struct SetupData {
    std::string api_key;
    std::string email;
    std::string name;
    std::string plan_type;
    int         daily_limit;
    std::string homepage;
    std::string search_engine;
    std::string ai_provider;
    std::string ai_model;
    std::string ai_base_url;
};

class SetupManager {
public:
    static SetupManager& Get();
    
    // Async HTTP POST isteği ile kurulum bilgilerini al
    void SendSetupRequest(
        const std::string& email,
        const std::string& password,
        const std::string& name,
        std::function<void(bool success, const SetupData& data, const std::string& error)> callback
    );

private:
    SetupManager() = default;
    
    // JSON parse yardımcıları
    std::string ParseJsonString(const std::string& json, const std::string& key);
    int ParseJsonInt(const std::string& json, const std::string& key);
    bool ParseJsonBool(const std::string& json, const std::string& key);
    bool ParseSetupResponse(const std::string& json, SetupData& data, std::string& error);
};

} // namespace ferman
