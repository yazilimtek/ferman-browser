# Ferman Browser

Ferman Browser, GTK4 ve WebKitGTK kullanılarak C++20 ile yazılmış hafif, hızlı ve açık kaynaklı bir web tarayıcısıdır.

![Platform](https://img.shields.io/badge/platform-Linux-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B20-orange)
![License](https://img.shields.io/badge/license-MIT-green)
![Build](https://img.shields.io/github/actions/workflow/status/pardusus/ferman-browser/build.yml?branch=main)

## Özellikler

- **Kurulum Karşılama Ekranı** — İlk açılışta ferman.net.tr ile entegrasyon
- **Sekme sistemi** — Çoklu sekme, header bar'da gösterim
- **Ana sayfa** — Gömülü başlangıç sayfası ve Google arama
- **Akıllı adres çubuğu** — URL veya arama sorgusu olarak algılar
- **Yapay Zeka Entegrasyonu** — Çoklu AI provider desteği (OpenAI, Anthropic, DeepSeek, Groq)
- **Favicon desteği** — Her sekme için site simgesi
- **Sekme bilgileri** — `#id`, favicon ve site adı her sekmede görünür
- **Sekme davranışı** — Son sekme kapanınca uygulama kapanmaz, ana sayfa açılır
- **Adres çubuğu araçları** — Temizle, kopyala ve favori butonları
- **Güvenli Ayarlar** — API key şifreli saklama
- **GTK4 native UI** — Modern, sistem temasıyla uyumlu arayüz

## Gereksinimler

| Paket | Minimum Sürüm |
|---|---|
| CMake | 3.20 |
| GCC / Clang | C++20 desteği |
| GTK4 | 4.0 |
| WebKitGTK | 6.0 (webkitgtk-6.0) |
| pkg-config | — |

### Debian / Ubuntu / Pardus

```bash
sudo apt install \
    cmake \
    g++ \
    libgtk-4-dev \
    libwebkitgtk-6.0-dev \
    pkg-config
```

### Fedora

```bash
sudo dnf install \
    cmake \
    gcc-c++ \
    gtk4-devel \
    webkitgtk6.0-devel \
    pkg-config
```

### Arch Linux

```bash
sudo pacman -S cmake gcc gtk4 webkitgtk-6.0 pkgconf
```

## Kurulum (Önerilen)

```bash
git clone https://github.com/yazilimtek/ferman-browser.git
cd ferman-browser
sudo ./install.sh
```

Kaldırmak için:

```bash
sudo ./install.sh --uninstall
```

Özel önek ile kurulum:

```bash
sudo ./install.sh --prefix /usr/local
```

## Manuel Derleme & Kurulum

```bash
git clone https://github.com/yazilimtek/ferman-browser.git
cd ferman-browser
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build --prefix /usr
sudo gtk-update-icon-cache /usr/share/icons/hicolor
sudo update-desktop-database /usr/share/applications
```

## Paket Oluşturma (.deb / .tar.gz)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cd build && cpack
# → ferman-browser-1.0.0-linux-amd64.deb
# → ferman-browser-1.0.0-linux-amd64.tar.gz
```

## Geliştirme Ortamında Çalıştırma

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
./build/ferman-browser
```

## Proje Yapısı

```
ferman-browser/
├── CMakeLists.txt          # CMake yapılandırması
├── src/
│   ├── main.cpp            # Giriş noktası — GtkApplication
│   ├── tab.h               # Tab veri yapısı
│   ├── browser_window.h    # BrowserWindow sınıf tanımı
│   ├── browser_window.cpp  # UI, tab yönetimi, sinyal callback'leri
│   ├── settings_manager.h  # Ayarlar yönetimi
│   ├── settings_manager.cpp
│   ├── setup_manager.h     # Kurulum API entegrasyonu
│   ├── setup_manager.cpp
│   ├── crypto_utils.h      # API key şifreleme
│   ├── crypto_utils.cpp
│   ├── ai_manager.h        # Yapay zeka yönetimi
│   ├── ai_manager.cpp
│   ├── session_manager.h   # Oturum yönetimi
│   ├── history_manager.h   # Geçmiş yönetimi
│   ├── bookmark_manager.h  # Yer imi yönetimi
│   └── download_manager.h  # İndirme yönetimi
└── .github/
    └── workflows/
        └── build.yml       # GitHub Actions CI
```

## Kullanım

### İlk Çalıştırma

Tarayıcıyı ilk kez açtığınızda kurulum karşılama ekranı görünür:
1. Email, şifre ve isim bilgilerinizi girin
2. "Kurulumu Başlat" butonuna tıklayın
3. ferman.net.tr ile otomatik entegrasyon sağlanır
4. API key şifreli olarak kaydedilir

**Alternatif**: "Kurulumu Atla" seçeneği ile daha sonra manuel yapılandırabilirsiniz.

### Temel Kullanım

| Eylem | Yöntem |
|---|---|
| Yeni sekme | `[+]` butonu (header bar sağı) |
| Sekmeyi kapat | `[✕]` butonu (sekme sağı) |
| Sekme değiştir | Sekmeye tıkla |
| Adres git / Ara | Adres çubuğuna yaz + Enter |
| Geri / İleri | ◀ ▶ butonları |
| Yenile / Durdur | ↻ butonu |
| URL temizle | Adres çubuğu yanı `✕` |
| URL kopyala | Adres çubuğu yanı kopyala ikonu |
| Ayarlar | Menü → Ayarlar veya `ferman://ayarlar` |
| Yapay Zeka | Sağ panel butonu |

## Katkı

1. Fork'layın
2. Feature branch oluşturun: `git checkout -b feature/yeni-ozellik`
3. Değişikliklerinizi commit edin: `git commit -m 'feat: yeni özellik ekle'`
4. Branch'inizi push edin: `git push origin feature/yeni-ozellik`
5. Pull Request açın

## Lisans

Bu proje [MIT Lisansı](LICENSE) altında dağıtılmaktadır.
