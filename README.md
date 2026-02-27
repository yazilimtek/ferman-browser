# Ferzan Browser

Ferzan Browser, GTK4 ve WebKitGTK kullanılarak C++20 ile yazılmış hafif, hızlı ve açık kaynaklı bir web tarayıcısıdır.

![Platform](https://img.shields.io/badge/platform-Linux-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B20-orange)
![License](https://img.shields.io/badge/license-MIT-green)
![Build](https://img.shields.io/github/actions/workflow/status/pardusus/ferzan-browser/build.yml?branch=main)

## Özellikler

- **Sekme sistemi** — Çoklu sekme, header bar'da gösterim
- **Ana sayfa** — Gömülü başlangıç sayfası ve Google arama
- **Akıllı adres çubuğu** — URL veya arama sorgusu olarak algılar
- **Favicon desteği** — Her sekme için site simgesi
- **Sekme bilgileri** — `#id`, favicon ve site adı her sekmede görünür
- **Sekme davranışı** — Son sekme kapanınca uygulama kapanmaz, ana sayfa açılır
- **Adres çubuğu araçları** — Temizle, kopyala ve favori butonları
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

## Derleme

```bash
git clone https://github.com/pardusus/ferzan-browser.git
cd ferzan-browser
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Çalıştırma

```bash
./build/ferzan-browser
```

## Proje Yapısı

```
ferzan-browser/
├── CMakeLists.txt          # CMake yapılandırması
├── src/
│   ├── main.cpp            # Giriş noktası — GtkApplication
│   ├── tab.h               # Tab veri yapısı
│   ├── browser_window.h    # BrowserWindow sınıf tanımı
│   └── browser_window.cpp  # UI, tab yönetimi, sinyal callback'leri
└── .github/
    └── workflows/
        └── build.yml       # GitHub Actions CI
```

## Kullanım

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

## Katkı

1. Fork'layın
2. Feature branch oluşturun: `git checkout -b feature/yeni-ozellik`
3. Değişikliklerinizi commit edin: `git commit -m 'feat: yeni özellik ekle'`
4. Branch'inizi push edin: `git push origin feature/yeni-ozellik`
5. Pull Request açın

## Lisans

Bu proje [MIT Lisansı](LICENSE) altında dağıtılmaktadır.
