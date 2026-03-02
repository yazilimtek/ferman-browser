#include "crypto_utils.h"
#include <cstring>

namespace ferman {

// Base64 karakter tablosu
static const char kBase64Chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string CryptoUtils::Base64Encode(const std::string& input) {
    std::string output;
    int val = 0;
    int valb = -6;
    
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            output.push_back(kBase64Chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    
    if (valb > -6) {
        output.push_back(kBase64Chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    
    while (output.size() % 4) {
        output.push_back('=');
    }
    
    return output;
}

std::string CryptoUtils::Base64Decode(const std::string& input) {
    std::string output;
    int val = 0;
    int valb = -8;
    
    for (unsigned char c : input) {
        if (c == '=') break;
        
        const char* pos = strchr(kBase64Chars, c);
        if (!pos) continue;
        
        val = (val << 6) + (pos - kBase64Chars);
        valb += 6;
        
        if (valb >= 0) {
            output.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    
    return output;
}

std::string CryptoUtils::XorCrypt(const std::string& input, const std::string& key) {
    std::string output;
    output.reserve(input.size());
    
    size_t key_len = key.length();
    for (size_t i = 0; i < input.size(); ++i) {
        output.push_back(input[i] ^ key[i % key_len]);
    }
    
    return output;
}

std::string CryptoUtils::EncryptApiKey(const std::string& plain) {
    if (plain.empty()) return "";
    
    // 1. XOR ile şifrele
    std::string xored = XorCrypt(plain, kEncryptionKey);
    
    // 2. Base64 encode et
    return Base64Encode(xored);
}

std::string CryptoUtils::DecryptApiKey(const std::string& encrypted) {
    if (encrypted.empty()) return "";
    
    // 1. Base64 decode et
    std::string decoded = Base64Decode(encrypted);
    
    // 2. XOR ile çöz (XOR simetrik olduğu için aynı işlem)
    return XorCrypt(decoded, kEncryptionKey);
}

} // namespace ferman
