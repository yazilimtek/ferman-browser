#pragma once
#include <string>

namespace ferman {

// Basit XOR + Base64 şifreleme ile API key koruma
// Not: Bu basit bir koruma mekanizmasıdır, güvenlik için HTTPS kullanımı önerilir
class CryptoUtils {
public:
    // API key'i şifrele (XOR + Base64)
    static std::string EncryptApiKey(const std::string& plain);
    
    // Şifreli API key'i çöz (Base64 + XOR)
    static std::string DecryptApiKey(const std::string& encrypted);

private:
    static constexpr const char* kEncryptionKey = "FermanBrowser2026SecretKey";
    
    // Base64 encoding/decoding
    static std::string Base64Encode(const std::string& input);
    static std::string Base64Decode(const std::string& input);
    
    // XOR şifreleme
    static std::string XorCrypt(const std::string& input, const std::string& key);
};

} // namespace ferman
