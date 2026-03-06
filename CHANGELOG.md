# Changelog

Tüm önemli değişiklikler bu dosyada belgelenir.
Format: [Keep a Changelog](https://keepachangelog.com/tr/1.0.0/)
Versiyon: [Semantic Versioning](https://semver.org/lang/tr/)

---

## [Unreleased]

### Eklendi
- Sayfa kaynağını görüntüle (`view-source:` protokolü + sağ tık menüsü)
- Cloudflare bot tespiti azaltmak için Chrome API enjeksiyonu (`navigator.webdriver`, `window.chrome`, `navigator.plugins`)
- Dosya yükleme butonu geçici olarak gizlendi

---

## [1.0.0] - 2025-03-06

### Eklendi
- GTK4 + WebKitGTK tabanlı temel tarayıcı altyapısı
- Çoklu sekme sistemi — `#id` etiketiyle sekme tanımlama
- Akıllı adres çubuğu (URL / arama otomatik algılama)
- Yapay Zeka sağ panel — çoklu AI provider desteği (OpenAI, Anthropic, DeepSeek, Groq vb.)
- Çoklu AI ajan profili yönetimi
- AI chat operatörleri: `@ajan` ile ajan seçimi, `#id` ile sekme içeriği ekleme
- Sekme içeriği okuma — HTML'den arındırılarak AI'ya sistem mesajı olarak ekleme
- Yer imi yönetimi (klasör destekli)
- İndirme yönetimi
- Ziyaret geçmişi
- ferman.net.tr kurulum entegrasyonu
- Şifreli API key saklama
- Sayfa içi arama (Ctrl+F)
- Sağ tık context menu (yeni sekmede aç, kopyala, AI ile çevir)
- Zoom kontrolü (Ctrl+/-, adres çubuğu butonları)
- Klavye kısayolları (Ctrl+T, Ctrl+W, Ctrl+L, F5, Alt+←/→ vb.)
- `.deb` ve `.tar.gz` paket desteği (CPack)

---

## Versiyon Şeması

```
MAJOR.MINOR.PATCH

MAJOR — Geriye dönük uyumsuz büyük değişiklikler
MINOR — Geriye dönük uyumlu yeni özellikler
PATCH — Hata düzeltmeleri
```

## Release Prosedürü

```bash
# 1. Versiyon tag'i oluştur ve push'la
git tag v1.1.0 -m "Release v1.1.0"
git push origin v1.1.0

# GitHub Actions otomatik olarak:
# - Projeyi derler
# - .deb ve .tar.gz paketler
# - GitHub Release oluşturur ve paketleri yükler
```
