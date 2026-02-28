#include "file_extractor.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>

#ifdef HAVE_POPPLER
#include <poppler/glib/poppler.h>
#endif
#ifdef HAVE_LIBZIP
#include <zip.h>
#endif

namespace ferzan {

// ── Yardımcılar ───────────────────────────────────────────────────────────────

std::string FileExtractor::GuessExt(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

std::string FileExtractor::Base64Encode(const std::string& data) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        unsigned char b0 = (unsigned char)data[i];
        unsigned char b1 = (i+1 < data.size()) ? (unsigned char)data[i+1] : 0;
        unsigned char b2 = (i+2 < data.size()) ? (unsigned char)data[i+2] : 0;
        out += tbl[b0 >> 2];
        out += tbl[((b0 & 3) << 4) | (b1 >> 4)];
        out += (i+1 < data.size()) ? tbl[((b1 & 0xf) << 2) | (b2 >> 6)] : '=';
        out += (i+2 < data.size()) ? tbl[b2 & 0x3f] : '=';
    }
    return out;
}

std::string FileExtractor::StripHtmlTags(const std::string& html) {
    std::string out;
    bool in_tag = false;
    bool in_script = false;
    for (size_t i = 0; i < html.size(); ++i) {
        if (html[i] == '<') {
            in_tag = true;
            // script/style bloklarını atla
            std::string tag_name;
            for (size_t j = i+1; j < html.size() && j < i+10; ++j) {
                if (html[j] == '>' || html[j] == ' ') break;
                tag_name += std::tolower((unsigned char)html[j]);
            }
            if (tag_name == "script" || tag_name == "style") in_script = true;
            if (tag_name == "/script" || tag_name == "/style") in_script = false;
        } else if (html[i] == '>') {
            in_tag = false;
            if (!in_script) out += ' ';
        } else if (!in_tag && !in_script) {
            out += html[i];
        }
    }
    // Çoklu boşlukları tek boşluğa indir
    std::string clean;
    bool last_ws = false;
    for (char c : out) {
        if (std::isspace((unsigned char)c)) {
            if (!last_ws) clean += ' ';
            last_ws = true;
        } else {
            clean += c;
            last_ws = false;
        }
    }
    return clean;
}

// ── Ana dispatch ──────────────────────────────────────────────────────────────

ExtractResult FileExtractor::Extract(const std::string& path) {
    std::string ext = GuessExt(path);

    if (ext == "txt" || ext == "md" || ext == "csv" || ext == "json" ||
        ext == "xml" || ext == "py"  || ext == "cpp" || ext == "h"   ||
        ext == "js"  || ext == "ts"  || ext == "css" || ext == "sql")
        return ExtractText(path);

    if (ext == "html" || ext == "htm")
        return ExtractHtml(path);

    if (ext == "pdf")
        return ExtractPdf(path);

    if (ext == "docx" || ext == "xlsx" || ext == "pptx" || ext == "odt")
        return ExtractDocx(path);

    if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "webp" ||
        ext == "gif" || ext == "bmp"  || ext == "svg")
        return ExtractImage(path);

    // Bilinmeyen: metin olarak dene
    return ExtractText(path);
}

// ── Düz metin ─────────────────────────────────────────────────────────────────

ExtractResult FileExtractor::ExtractText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {"", "", "Dosya açılamadı: " + path};
    std::string text((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    // 500KB sınırı
    if (text.size() > 512 * 1024)
        text = text.substr(0, 512 * 1024) + "\n[...dosya kesildi...]";
    ExtractResult r;
    r.text = text;
    r.mime_type = "text/plain";
    return r;
}

// ── HTML ──────────────────────────────────────────────────────────────────────

ExtractResult FileExtractor::ExtractHtml(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {"", "", "Dosya açılamadı: " + path};
    std::string html((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    ExtractResult r;
    r.text = StripHtmlTags(html);
    r.mime_type = "text/html";
    return r;
}

// ── PDF (poppler-glib) ────────────────────────────────────────────────────────

ExtractResult FileExtractor::ExtractPdf(const std::string& path) {
    ExtractResult r;
    r.mime_type = "application/pdf";
#ifdef HAVE_POPPLER
    std::string uri = "file://" + path;
    GError* err = nullptr;
    PopplerDocument* doc = poppler_document_new_from_file(uri.c_str(), nullptr, &err);
    if (!doc) {
        r.error = err ? err->message : "PDF açılamadı";
        if (err) g_error_free(err);
        return r;
    }
    int n = poppler_document_get_n_pages(doc);
    std::ostringstream out;
    for (int i = 0; i < n; ++i) {
        PopplerPage* page = poppler_document_get_page(doc, i);
        if (!page) continue;
        char* text = poppler_page_get_text(page);
        if (text) { out << text << "\n"; g_free(text); }
        g_object_unref(page);
        if ((size_t)out.tellp() > 512 * 1024) {
            out << "\n[...PDF kesildi...]";
            break;
        }
    }
    g_object_unref(doc);
    r.text = out.str();
#else
    r.error = "PDF desteği etkin değil (poppler-glib kurulu değil)";
#endif
    return r;
}

// ── DOCX / XLSX / PPTX (libzip + XML parse) ───────────────────────────────────

std::string FileExtractor::ReadZipXml(const std::string& zip_path,
                                       const std::string& xml_entry) {
#ifdef HAVE_LIBZIP
    int zerr = 0;
    zip_t* za = zip_open(zip_path.c_str(), ZIP_RDONLY, &zerr);
    if (!za) return {};
    zip_int64_t idx = zip_name_locate(za, xml_entry.c_str(), 0);
    if (idx < 0) { zip_close(za); return {}; }
    zip_stat_t zst;
    zip_stat_index(za, (zip_uint64_t)idx, 0, &zst);
    zip_file_t* zf = zip_fopen_index(za, (zip_uint64_t)idx, 0);
    if (!zf) { zip_close(za); return {}; }
    std::string buf(zst.size, '\0');
    zip_fread(zf, buf.data(), zst.size);
    zip_fclose(zf);
    zip_close(za);
    return buf;
#else
    (void)zip_path; (void)xml_entry;
    return {};
#endif
}

ExtractResult FileExtractor::ExtractDocx(const std::string& path) {
    ExtractResult r;
#ifdef HAVE_LIBZIP
    std::string ext = GuessExt(path);
    std::string xml_path;
    if (ext == "docx" || ext == "odt") xml_path = "word/document.xml";
    else if (ext == "xlsx")            xml_path = "xl/sharedStrings.xml";
    else if (ext == "pptx")            xml_path = "ppt/slides/slide1.xml";
    else                               xml_path = "word/document.xml";
    std::string xml = ReadZipXml(path, xml_path);
    if (xml.empty() && GuessExt(path) == "xlsx")
        xml = ReadZipXml(path, "xl/worksheets/sheet1.xml");
    if (xml.empty()) { r.error = "Dosya içeriği okunamadı (ZIP/XML)"; return r; }
    r.text = StripHtmlTags(xml);
    r.mime_type = "application/vnd.openxmlformats";
#else
    r.error = "DOCX/XLSX desteği etkin değil (libzip kurulu değil)";
#endif
    return r;
}

// ── Resim (base64) ────────────────────────────────────────────────────────────

ExtractResult FileExtractor::ExtractImage(const std::string& path) {
    ExtractResult r;
    r.is_image  = true;
    r.mime_type = "image/" + GuessExt(path);

    std::ifstream f(path, std::ios::binary);
    if (!f) { r.error = "Resim açılamadı: " + path; return r; }
    std::string data((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    // 10MB sınırı
    if (data.size() > 10 * 1024 * 1024) {
        r.error = "Resim çok büyük (maks 10MB)";
        return r;
    }
    r.base64_data = "data:" + r.mime_type + ";base64," + Base64Encode(data);
    r.text = "[Resim: " + path.substr(path.rfind('/') + 1) + "]";
    return r;
}

} // namespace ferzan
