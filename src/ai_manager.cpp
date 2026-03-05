#include "ai_manager.h"
#include "settings_manager.h"
#include <gio/gio.h>
#include <libsoup/soup.h>
#include <ctime>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace ferman {

// ── Basit JSON yardımcıları ───────────────────────────────────────────────────

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += (char)c;
    }
    return out;
}

// Basit değer çıkarıcı: "key":"value" → value
// \uXXXX → UTF-8 dönüşümü
static void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

static std::string json_str(const std::string& json, const std::string& key) {
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
            else if (nc == '/')  result += '/';
            else if (nc == 'u' && pos + 4 < json.size()) {
                // \uXXXX — 4 hex basamağı oku
                char hex[5] = {};
                for (int k = 0; k < 4; ++k) hex[k] = json[++pos];
                uint32_t cp = (uint32_t)strtoul(hex, nullptr, 16);
                // Surrogate pair kontrolü: \uD800-\uDBFF → high surrogate
                if (cp >= 0xD800 && cp <= 0xDBFF &&
                    pos + 2 < json.size() &&
                    json[pos+1] == '\\' && json[pos+2] == 'u') {
                    pos += 2; // \u
                    char hex2[5] = {};
                    for (int k = 0; k < 4; ++k) hex2[k] = json[++pos];
                    uint32_t low = (uint32_t)strtoul(hex2, nullptr, 16);
                    if (low >= 0xDC00 && low <= 0xDFFF)
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                }
                append_utf8(result, cp);
            } else {
                result += nc;
            }
        } else if (c == '"') {
            break;
        } else {
            result += c;
        }
    }
    return result;
}

static int64_t json_int(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    pos += needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '"')) ++pos;
    int64_t val = 0;
    bool neg = false;
    if (pos < json.size() && json[pos] == '-') { neg = true; ++pos; }
    while (pos < json.size() && std::isdigit((unsigned char)json[pos]))
        val = val * 10 + (json[pos++] - '0');
    return neg ? -val : val;
}

// ── Singleton ─────────────────────────────────────────────────────────────────

AiManager& AiManager::Get() {
    static AiManager inst;
    return inst;
}

void AiManager::Init(const std::string& data_dir) {
    data_dir_ = data_dir + "/ai_chats";
    std::filesystem::create_directories(data_dir_);
}

// ── Dosya yolları ─────────────────────────────────────────────────────────────

std::string AiManager::ChatPath(int64_t id) const {
    char buf[64];
    snprintf(buf, sizeof(buf), "/chat_%lld.json", (long long)id);
    return data_dir_ + buf;
}

// ── Serialize / Parse ─────────────────────────────────────────────────────────

std::string AiManager::SerializeChat(const AiChat& chat) const {
    std::ostringstream o;
    o << "{\n";
    o << "  \"id\":" << chat.id << ",\n";
    o << "  \"title\":\"" << json_escape(chat.title) << "\",\n";
    o << "  \"created_at\":" << chat.created_at << ",\n";
    o << "  \"provider\":\"" << json_escape(chat.provider) << "\",\n";
    o << "  \"model\":\"" << json_escape(chat.model) << "\",\n";
    o << "  \"messages\":[\n";
    for (size_t i = 0; i < chat.messages.size(); ++i) {
        const auto& m = chat.messages[i];
        o << "    {\"role\":\"" << json_escape(m.role)
          << "\",\"content\":\"" << json_escape(m.content)
          << "\",\"timestamp\":" << m.timestamp << "}";
        if (i + 1 < chat.messages.size()) o << ",";
        o << "\n";
    }
    o << "  ]\n}\n";
    return o.str();
}

AiChat AiManager::ParseChat(const std::string& json) const {
    AiChat chat;
    chat.id         = json_int(json, "id");
    chat.title      = json_str(json, "title");
    chat.created_at = json_int(json, "created_at");
    chat.provider   = json_str(json, "provider");
    chat.model      = json_str(json, "model");

    // messages dizisini parse et
    auto mstart = json.find("\"messages\":[");
    if (mstart == std::string::npos) return chat;
    mstart += 12;
    size_t depth = 0;
    size_t obj_start = std::string::npos;
    for (size_t i = mstart; i < json.size(); ++i) {
        char c = json[i];
        if (c == '{') {
            if (depth == 0) obj_start = i;
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0 && obj_start != std::string::npos) {
                std::string obj = json.substr(obj_start, i - obj_start + 1);
                AiMessage msg;
                msg.role      = json_str(obj, "role");
                msg.content   = json_str(obj, "content");
                msg.timestamp = json_int(obj, "timestamp");
                chat.messages.push_back(msg);
                obj_start = std::string::npos;
            }
        } else if (c == ']' && depth == 0) {
            break;
        }
    }
    return chat;
}

// ── CRUD ──────────────────────────────────────────────────────────────────────

int64_t AiManager::NewChat(const std::string& provider, const std::string& model) {
    AiChat chat;
    chat.id         = (int64_t)std::time(nullptr);
    chat.created_at = chat.id;
    chat.provider   = provider;
    chat.model      = model;
    chat.title      = "Yeni Sohbet";
    SaveChat(chat);
    return chat.id;
}

AiChat AiManager::LoadChat(int64_t id) const {
    std::ifstream f(ChatPath(id));
    if (!f) return {};
    std::string json((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    return ParseChat(json);
}

void AiManager::SaveChat(const AiChat& chat) {
    std::ofstream f(ChatPath(chat.id), std::ios::trunc);
    if (f) f << SerializeChat(chat);
}

void AiManager::DeleteChat(int64_t id) {
    std::filesystem::remove(ChatPath(id));
}

std::vector<AiChat> AiManager::ListChats(const AiChatFilter& filter) const {
    std::vector<AiChat> result;
    if (!std::filesystem::exists(data_dir_)) return result;

    for (const auto& entry : std::filesystem::directory_iterator(data_dir_)) {
        if (entry.path().extension() != ".json") continue;
        std::ifstream f(entry.path());
        if (!f) continue;
        std::string json((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
        AiChat chat = ParseChat(json);
        if (chat.id == 0) continue;

        // Filtre: tarih
        if (filter.from_ts > 0 && chat.created_at < filter.from_ts) continue;
        if (filter.to_ts   > 0 && chat.created_at > filter.to_ts)   continue;

        // Filtre: anahtar kelime (başlık veya mesaj içeriği)
        if (!filter.keyword.empty()) {
            std::string kw = filter.keyword;
            std::transform(kw.begin(), kw.end(), kw.begin(), ::tolower);
            std::string title = chat.title;
            std::transform(title.begin(), title.end(), title.begin(), ::tolower);
            bool found = (title.find(kw) != std::string::npos);
            if (!found) {
                for (const auto& m : chat.messages) {
                    std::string mc = m.content;
                    std::transform(mc.begin(), mc.end(), mc.begin(), ::tolower);
                    if (mc.find(kw) != std::string::npos) { found = true; break; }
                }
            }
            if (!found) continue;
        }
        result.push_back(std::move(chat));
    }

    // En yeni sohbet önce
    std::sort(result.begin(), result.end(),
        [](const AiChat& a, const AiChat& b){ return a.created_at > b.created_at; });
    return result;
}

// ── Provider yapılandırması ───────────────────────────────────────────────────

// API key önekinden provider otomatik belirle (AiAgent::DetectProvider ile aynı mantık)
static std::string DetectProviderFromKey(const std::string& api_key) {
    if (api_key.rfind("fai-",    0) == 0) return "ferman";
    if (api_key.rfind("sk-ant-", 0) == 0) return "anthropic";
    if (api_key.rfind("sk-or-",  0) == 0) return "openrouter";
    if (api_key.rfind("gsk_",    0) == 0) return "groq";
    if (api_key.rfind("ds-",     0) == 0) return "deepseek";
    if (api_key.rfind("sk-",     0) == 0) return "openai";
    return "deepseek"; // varsayılan
}

AiManager::ProviderCfg AiManager::BuildProviderCfg(
        const std::string& provider_hint,
        const std::string& /*model*/,
        const std::string& api_key,
        const std::string& base_url) const
{
    // Önce API key'den gerçek provider'ı belirle, hint sadece fallback
    std::string provider = api_key.empty() ? provider_hint : DetectProviderFromKey(api_key);

    ProviderCfg cfg;
    if (provider == "ferman") {
        cfg.url = base_url.empty()
            ? "https://ferman.net.tr/api/v1/chat/completions"
            : base_url;
        cfg.auth_header = "Bearer " + api_key;
    } else if (provider == "anthropic") {
        cfg.url = base_url.empty()
            ? "https://api.anthropic.com/v1/messages"
            : base_url;
        cfg.auth_header  = "";         // Anthropic Bearer kullanmaz
        cfg.extra_header = api_key;    // x-api-key header'ında gider
    } else if (provider == "groq") {
        cfg.url = base_url.empty()
            ? "https://api.groq.com/openai/v1/chat/completions"
            : base_url;
        cfg.auth_header = "Bearer " + api_key;
    } else if (provider == "openrouter") {
        cfg.url = base_url.empty()
            ? "https://openrouter.ai/api/v1/chat/completions"
            : base_url;
        cfg.auth_header = "Bearer " + api_key;
    } else if (provider == "deepseek") {
        cfg.url = base_url.empty()
            ? "https://api.deepseek.com/chat/completions"
            : base_url;
        cfg.auth_header = "Bearer " + api_key;
    } else {
        // openai + bilinmeyen → OpenAI uyumlu format
        cfg.url = base_url.empty()
            ? "https://api.openai.com/v1/chat/completions"
            : base_url;
        cfg.auth_header = "Bearer " + api_key;
    }
    return cfg;
}

// ── Request body ──────────────────────────────────────────────────────────────

std::string AiManager::BuildRequestBody(const AiChat& chat,
                                         const std::string& provider,
                                         const std::string& model) const
{
    std::ostringstream o;
    if (provider == "anthropic") {
        // Anthropic format — system mesajlarını birleştir, kullanıcı/asistan ayrı
        std::string sys_combined;
        for (const auto& m : chat.messages)
            if (m.role == "system") sys_combined += m.content + "\n";
        o << "{\"model\":\"" << json_escape(model) << "\","
          << "\"max_tokens\":4096,";
        if (!sys_combined.empty())
            o << "\"system\":\"" << json_escape(sys_combined) << "\",";
        o << "\"messages\":[";
        bool first = true;
        for (const auto& m : chat.messages) {
            if (m.role == "system") continue;
            if (!first) o << ",";
            o << "{\"role\":\"" << json_escape(m.role) << "\","
              << "\"content\":\"" << json_escape(m.content) << "\"}";
            first = false;
        }
        o << "]}";
    } else {
        // OpenAI / DeepSeek uyumlu format — system mesajları dahil
        o << "{\"model\":\"" << json_escape(model) << "\","
          << "\"stream\":true,"
          << "\"messages\":[";
        bool first = true;
        for (const auto& m : chat.messages) {
            if (!first) o << ",";
            o << "{\"role\":\"" << json_escape(m.role) << "\","
              << "\"content\":\"" << json_escape(m.content) << "\"}";
            first = false;
        }
        o << "]}";
    }
    return o.str();
}

// ── Response parse ────────────────────────────────────────────────────────────

std::string AiManager::ParseResponse(const std::string& body,
                                      const std::string& provider) const
{
    if (provider == "anthropic") {
        // {"content":[{"type":"text","text":"..."}],...}
        auto pos = body.find("\"text\":\"");
        if (pos == std::string::npos) return {};
        return json_str("{" + body.substr(pos - 1), "text");
    } else {
        // OpenAI/DeepSeek: choices[0].message.content
        auto pos = body.find("\"content\":\"");
        if (pos == std::string::npos) return {};
        // json_str ile parse et
        std::string sub = body.substr(pos - 1);
        sub[0] = '{';
        // bul kapanışı
        return json_str("{" + body.substr(pos), "content");
    }
}

// ── Async HTTP gönderme ───────────────────────────────────────────────────────

// SSE chunk parser: "data: {...}\n" satırından delta content çıkar
static std::string parse_sse_chunk(const std::string& line, const std::string& provider) {
    // "data: " önekini atla
    if (line.rfind("data: ", 0) != 0) return {};
    std::string json = line.substr(6);
    if (json == "[DONE]") return {};

    if (provider == "anthropic") {
        // {"type":"content_block_delta","delta":{"type":"text_delta","text":"..."}}
        auto pos = json.find("\"text\":\"");
        if (pos == std::string::npos) return {};
        return json_str("{" + json.substr(pos - 1), "text");
    } else {
        // OpenAI/DeepSeek: choices[0].delta.content
        auto pos = json.find("\"content\":\"");
        if (pos == std::string::npos) return {};
        return json_str("{" + json.substr(pos), "content");
    }
}

struct SendCtx {
    AiChat*            chat;
    std::string        provider;
    std::string        model;
    AiResponseCallback callback;
    AiManager*         mgr;
    GInputStream*      stream;
    SoupSession*       session;
    std::string        line_buf;   // biriken satır tamponu
    std::string        full_text;  // toplanan tam yanıt
    static const gsize kBufSize = 4096;
    char               read_buf[4096];
};

// Forward declaration
static void sse_read_next(SendCtx* ctx);

static void sse_on_read(GObject* src, GAsyncResult* res, gpointer ud) {
    auto* ctx = static_cast<SendCtx*>(ud);
    GError* err = nullptr;
    gssize n = g_input_stream_read_finish(G_INPUT_STREAM(src), res, &err);

    if (err) {
        std::string emsg = err->message;
        g_error_free(err);
        // Main thread'e hata ilet
        struct ErrRet { SendCtx* ctx; std::string msg; };
        auto* r = new ErrRet{ctx, emsg};
        g_main_context_invoke(nullptr, [](gpointer ud) -> gboolean {
            auto* r = static_cast<ErrRet*>(ud);
            r->ctx->callback("", true, r->msg);
            g_object_unref(r->ctx->stream);
            g_object_unref(r->ctx->session);
            delete r->ctx;
            delete r;
            return G_SOURCE_REMOVE;
        }, r);
        return;
    }

    if (n <= 0) {
        // Stream bitti — kalan tamponu işle
        if (!ctx->line_buf.empty()) {
            std::string chunk = parse_sse_chunk(ctx->line_buf, ctx->provider);
            if (!chunk.empty()) ctx->full_text += chunk;
            ctx->line_buf.clear();
        }
        // Asistan mesajını chat'e kaydet ve done callback
        struct DoneRet { SendCtx* ctx; };
        auto* r = new DoneRet{ctx};
        g_main_context_invoke(nullptr, [](gpointer ud) -> gboolean {
            auto* r = static_cast<DoneRet*>(ud);
            auto* ctx = r->ctx;
            AiMessage am;
            am.role      = "assistant";
            am.content   = ctx->full_text;
            am.timestamp = (int64_t)std::time(nullptr);
            ctx->chat->messages.push_back(am);
            if (ctx->chat->title == "Yeni Sohbet") {
                for (const auto& m : ctx->chat->messages) {
                    if (m.role == "user") {
                        ctx->chat->title = m.content.substr(0, 40);
                        if (m.content.size() > 40) ctx->chat->title += "\xe2\x80\xa6";
                        break;
                    }
                }
            }
            ctx->mgr->SaveChat(*ctx->chat);
            ctx->callback(ctx->full_text, true, "");
            g_object_unref(ctx->stream);
            g_object_unref(ctx->session);
            delete ctx;
            delete r;
            return G_SOURCE_REMOVE;
        }, r);
        return;
    }

    // Gelen byte'ları satır tamponu'na ekle ve '\n'lerde kes
    for (gssize i = 0; i < n; ++i) {
        char c = ctx->read_buf[i];
        if (c == '\n') {
            std::string line = ctx->line_buf;
            ctx->line_buf.clear();
            if (line.empty() || line == "\r") continue;
            // '\r' varsa sil
            if (!line.empty() && line.back() == '\r') line.pop_back();

            std::string chunk = parse_sse_chunk(line, ctx->provider);
            if (!chunk.empty()) {
                ctx->full_text += chunk;
                // Main thread'e chunk ilet (harf harf güncelleme)
                struct ChunkRet { SendCtx* ctx; std::string chunk; };
                auto* cr = new ChunkRet{ctx, chunk};
                g_main_context_invoke(nullptr, [](gpointer ud) -> gboolean {
                    auto* cr = static_cast<ChunkRet*>(ud);
                    cr->ctx->callback(cr->chunk, false, "");
                    delete cr;
                    return G_SOURCE_REMOVE;
                }, cr);
            }
        } else {
            ctx->line_buf += c;
        }
    }

    // Devam et — bir sonraki chunk'ı oku
    sse_read_next(ctx);
}

static void sse_read_next(SendCtx* ctx) {
    g_input_stream_read_async(
        ctx->stream,
        ctx->read_buf,
        SendCtx::kBufSize,
        G_PRIORITY_DEFAULT,
        nullptr,
        sse_on_read,
        ctx);
}

void AiManager::SendMessage(AiChat& chat,
                             const std::string& provider,
                             const std::string& model,
                             const std::string& api_key,
                             const std::string& base_url,
                             AiResponseCallback callback)
{
    auto cfg  = BuildProviderCfg(provider, model, api_key, base_url);
    auto body = BuildRequestBody(chat, provider, model);

    SoupSession* session = soup_session_new();
    SoupMessage* msg = soup_message_new("POST", cfg.url.c_str());
    if (!msg) {
        g_object_unref(session);
        callback("", true, "Geçersiz URL: " + cfg.url);
        return;
    }

    SoupMessageHeaders* hdrs = soup_message_get_request_headers(msg);
    soup_message_headers_append(hdrs, "Content-Type", "application/json");
    if (!cfg.auth_header.empty())
        soup_message_headers_append(hdrs, "Authorization", cfg.auth_header.c_str());
    if (!cfg.extra_header.empty())
        soup_message_headers_append(hdrs, "x-api-key", cfg.extra_header.c_str());
    if (provider == "anthropic") {
        soup_message_headers_append(hdrs, "anthropic-version", "2023-06-01");
        soup_message_headers_append(hdrs, "accept", "text/event-stream");
    }
    // Tüm provider'lara tarayıcı kimliğini gönder
    {
        const std::string& dev_id = SettingsManager::Get().Prefs().device_id;
        if (!dev_id.empty())
            soup_message_headers_append(hdrs, "X-Browser-ID", dev_id.c_str());
    }

    GBytes* gbody = g_bytes_new(body.c_str(), body.size());
    soup_message_set_request_body_from_bytes(msg, "application/json", gbody);
    g_bytes_unref(gbody);

    auto* ctx = new SendCtx{};
    ctx->chat     = &chat;
    ctx->provider = provider;
    ctx->model    = model;
    ctx->callback = callback;
    ctx->mgr      = this;
    ctx->session  = session;

    soup_session_send_async(session, msg, G_PRIORITY_DEFAULT, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer ud) {
            auto* ctx     = static_cast<SendCtx*>(ud);
            auto* session = SOUP_SESSION(src);
            GError* err   = nullptr;
            GInputStream* stream = soup_session_send_finish(session, res, &err);

            if (err || !stream) {
                std::string emsg = err ? std::string(err->message) : "Bağlantı hatası";
                if (err) g_error_free(err);
                if (stream) g_object_unref(stream);
                struct ErrRet { SendCtx* ctx; std::string msg; };
                auto* r = new ErrRet{ctx, emsg};
                g_main_context_invoke(nullptr, [](gpointer ud) -> gboolean {
                    auto* r = static_cast<ErrRet*>(ud);
                    r->ctx->callback("", true, r->msg);
                    g_object_unref(r->ctx->session);
                    delete r->ctx;
                    delete r;
                    return G_SOURCE_REMOVE;
                }, r);
                return;
            }

            ctx->stream = stream;
            // SSE okumaya başla
            sse_read_next(ctx);
        }, ctx);
}

} // namespace ferman
