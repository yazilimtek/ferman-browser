#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace ferzan {

struct AiMessage {
    std::string role;       // "user" | "assistant" | "system"
    std::string content;
    int64_t     timestamp = 0;
};

struct AiChat {
    int64_t                  id = 0;
    std::string              title;
    int64_t                  created_at = 0;
    std::string              provider;   // hangi provider ile oluşturuldu
    std::string              model;
    std::vector<AiMessage>   messages;
};

// Filtreleme için
struct AiChatFilter {
    std::string keyword;     // başlık araması
    int64_t     from_ts = 0; // 0 = sınırsız
    int64_t     to_ts   = 0; // 0 = sınırsız
};

// Async cevap callback: (chunk, is_done, error)
using AiResponseCallback = std::function<void(const std::string&, bool, const std::string&)>;

class AiManager {
public:
    static AiManager& Get();

    void Init(const std::string& data_dir);

    // Sohbet yönetimi
    int64_t     NewChat(const std::string& provider, const std::string& model);
    AiChat      LoadChat(int64_t id) const;
    void        SaveChat(const AiChat& chat);
    void        DeleteChat(int64_t id);
    std::vector<AiChat> ListChats(const AiChatFilter& filter = {}) const;

    // HTTP gönderme (async — GIO thread pool kullanır, callback main thread'de çağrılır)
    void SendMessage(AiChat& chat,
                     const std::string& provider,
                     const std::string& model,
                     const std::string& api_key,
                     const std::string& base_url,
                     AiResponseCallback callback);

private:
    AiManager() = default;

    std::string ChatPath(int64_t id) const;
    std::string SerializeChat(const AiChat& chat) const;
    AiChat      ParseChat(const std::string& json) const;

    // Provider endpoint ve headers
    struct ProviderCfg {
        std::string url;
        std::string auth_header; // "Authorization: Bearer KEY"
        std::string extra_header; // Anthropic: "x-api-key: KEY"
    };
    ProviderCfg BuildProviderCfg(const std::string& provider,
                                  const std::string& model,
                                  const std::string& api_key,
                                  const std::string& base_url) const;

    std::string BuildRequestBody(const AiChat& chat,
                                  const std::string& provider,
                                  const std::string& model) const;
    std::string ParseResponse(const std::string& body,
                               const std::string& provider) const;

    std::string data_dir_;
};

} // namespace ferzan
