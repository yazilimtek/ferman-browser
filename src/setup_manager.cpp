#include "setup_manager.h"
#include <libsoup/soup.h>
#include <glib.h>
#include <cstring>
#include <cctype>

namespace ferman {

SetupManager& SetupManager::Get() {
    static SetupManager inst;
    return inst;
}

// Basit JSON string parser: "key":"value" → value
std::string SetupManager::ParseJsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    std::string result;
    for (; pos < json.size(); ++pos) {
        char c = json[pos];
        if (c == '\\' && pos + 1 < json.size()) {
            char nc = json[++pos];
            if      (nc == 'n')  result += '\n';
            else if (nc == 'r')  result += '\r';
            else if (nc == 't')  result += '\t';
            else if (nc == '"')  result += '"';
            else if (nc == '\\') result += '\\';
            else                 result += nc;
        } else if (c == '"') {
            break;
        } else {
            result += c;
        }
    }
    return result;
}

// Basit JSON int parser: "key":123 → 123
int SetupManager::ParseJsonInt(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    pos += needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '"')) ++pos;
    int val = 0;
    bool neg = false;
    if (pos < json.size() && json[pos] == '-') { neg = true; ++pos; }
    while (pos < json.size() && std::isdigit((unsigned char)json[pos]))
        val = val * 10 + (json[pos++] - '0');
    return neg ? -val : val;
}

// Basit JSON bool parser: "key":true → true
bool SetupManager::ParseJsonBool(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos += needle.size();
    while (pos < json.size() && json[pos] == ' ') ++pos;
    if (pos + 4 <= json.size() && json.substr(pos, 4) == "true") return true;
    return false;
}

bool SetupManager::ParseSetupResponse(const std::string& json, SetupData& data, std::string& error) {
    // success kontrolü
    bool success = ParseJsonBool(json, "success");
    if (!success) {
        error = ParseJsonString(json, "message");
        if (error.empty()) error = "Bilinmeyen hata";
        return false;
    }
    
    // data alanlarını parse et
    data.api_key       = ParseJsonString(json, "api_key");
    data.email         = ParseJsonString(json, "email");
    data.name          = ParseJsonString(json, "name");
    data.plan_type     = ParseJsonString(json, "plan_type");
    data.daily_limit   = ParseJsonInt(json, "daily_limit");
    data.homepage      = ParseJsonString(json, "homepage");
    data.search_engine = ParseJsonString(json, "search_engine");
    data.ai_provider   = ParseJsonString(json, "ai_provider");
    data.ai_model      = ParseJsonString(json, "ai_model");
    data.ai_base_url   = ParseJsonString(json, "ai_base_url");
    
    if (data.api_key.empty()) {
        error = "API key alınamadı";
        return false;
    }
    
    return true;
}

void SetupManager::SendSetupRequest(
    const std::string& email,
    const std::string& password,
    const std::string& name,
    std::function<void(bool success, const SetupData& data, const std::string& error)> callback
) {
    // JSON body oluştur
    std::string json_body = "{"
        "\"email\":\"" + email + "\","
        "\"password\":\"" + password + "\","
        "\"name\":\"" + name + "\""
        "}";
    
    // libsoup session ve message oluştur
    SoupSession* session = soup_session_new();
    SoupMessage* msg = soup_message_new("POST", "https://ferman.net.tr/api/browser/setup");
    
    if (!msg) {
        g_object_unref(session);
        SetupData empty_data;
        callback(false, empty_data, "Geçersiz URL");
        return;
    }
    
    // Request headers
    SoupMessageHeaders* req_headers = soup_message_get_request_headers(msg);
    soup_message_headers_append(req_headers, "Content-Type", "application/json");
    
    // Request body
    GBytes* body_bytes = g_bytes_new(json_body.c_str(), json_body.size());
    soup_message_set_request_body_from_bytes(msg, "application/json", body_bytes);
    g_bytes_unref(body_bytes);
    
    // Callback context
    struct CallbackContext {
        std::function<void(bool, const SetupData&, const std::string&)> callback;
        SetupManager* manager;
    };
    
    auto* ctx = new CallbackContext{callback, this};
    
    // Async request gönder
    soup_session_send_and_read_async(
        session, msg, G_PRIORITY_DEFAULT, nullptr,
        [](GObject* source, GAsyncResult* result, gpointer user_data) {
            auto* ctx = static_cast<CallbackContext*>(user_data);
            SoupSession* sess = SOUP_SESSION(source);
            
            GError* error = nullptr;
            GBytes* response_bytes = soup_session_send_and_read_finish(sess, result, &error);
            
            SetupData data;
            std::string error_msg;
            bool success = false;
            
            if (error) {
                error_msg = std::string("Bağlantı hatası: ") + error->message;
                g_error_free(error);
            } else if (response_bytes) {
                gsize size;
                const char* response_data = static_cast<const char*>(
                    g_bytes_get_data(response_bytes, &size));
                std::string response_json(response_data, size);
                
                success = ctx->manager->ParseSetupResponse(response_json, data, error_msg);
                g_bytes_unref(response_bytes);
            } else {
                error_msg = "Yanıt alınamadı";
            }
            
            // Callback'i main thread'de çağır
            struct IdleData {
                std::function<void(bool, const SetupData&, const std::string&)> callback;
                bool success;
                SetupData data;
                std::string error;
            };
            
            auto* idle_data = new IdleData{ctx->callback, success, data, error_msg};
            
            g_idle_add([](gpointer ud) -> gboolean {
                auto* id = static_cast<IdleData*>(ud);
                id->callback(id->success, id->data, id->error);
                delete id;
                return G_SOURCE_REMOVE;
            }, idle_data);
            
            delete ctx;
            g_object_unref(sess);
        },
        ctx
    );
    
    g_object_unref(msg);
}

} // namespace ferman
