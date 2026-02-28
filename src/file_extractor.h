#pragma once
#include <string>

namespace ferzan {

struct ExtractResult {
    std::string text;       // çıkarılan metin (boşsa hata var)
    std::string mime_type;  // tespit edilen MIME türü
    std::string error;      // hata mesajı
    bool        is_image = false;   // true → base64 olarak gönderilmeli
    std::string base64_data;        // resimler için base64
};

class FileExtractor {
public:
    // Dosyayı analiz et ve metin çıkar (senkron, iş parçacığında çağrılmalı)
    static ExtractResult Extract(const std::string& path);

private:
    static ExtractResult ExtractText(const std::string& path);
    static ExtractResult ExtractPdf(const std::string& path);
    static ExtractResult ExtractDocx(const std::string& path);  // DOCX/XLSX/PPTX (ZIP-based)
    static ExtractResult ExtractImage(const std::string& path);
    static ExtractResult ExtractHtml(const std::string& path);

    static std::string GuessExt(const std::string& path);
    static std::string Base64Encode(const std::string& data);
    static std::string StripHtmlTags(const std::string& html);
    // ZIP içinden XML dosyalarını okur, metin döndürür
    static std::string ReadZipXml(const std::string& zip_path,
                                   const std::string& xml_entry);
};

} // namespace ferzan
