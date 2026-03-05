# Ferman Browser

Ferman Browser, GTK4 ve WebKitGTK kullanılarak C++20 ile yazılmış hafif, hızlı ve açık kaynaklı bir web tarayıcısıdır.

![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-blue)
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

---

## Windows Kurulumu (MSYS2 / MinGW-w64)

Ferman Browser, Windows 10/11 üzerinde **MSYS2** ortamı aracılığıyla derlenip çalıştırılabilir.  
MSYS2, Windows'ta GTK4 ve WebKitGTK dahil tüm bağımlılıkları sağlayan Unix benzeri bir geliştirme ortamıdır.

### 1. MSYS2 Kurulumu

1. [https://www.msys2.org](https://www.msys2.org) adresinden **MSYS2** yükleyicisini indirin ve kurun.
2. Kurulum tamamlandıktan sonra **MSYS2 UCRT64** terminalini açın (Başlat menüsünden).
3. Sistemi güncelleyin:

```bash
pacman -Syu
```

> Terminal kapanırsa tekrar açıp güncellemeyi tamamlayın:

```bash
pacman -Su
```

### 2. Bağımlılıkları Yükleme

**MSYS2 UCRT64** terminalinde aşağıdaki komutu çalıştırın:

```bash
pacman -S --needed \
    mingw-w64-ucrt-x86_64-gcc \
    mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja \
    mingw-w64-ucrt-x86_64-pkg-config \
    mingw-w64-ucrt-x86_64-gtk4 \
    mingw-w64-ucrt-x86_64-webkitgtk-6.0 \
    mingw-w64-ucrt-x86_64-sqlite3 \
    mingw-w64-ucrt-x86_64-libsoup3 \
    mingw-w64-ucrt-x86_64-poppler \
    mingw-w64-ucrt-x86_64-libzip \
    git
```

> **Not:** `webkitgtk-6.0` paketi büyük olup indirilmesi biraz zaman alabilir (~500 MB).

### 3. Kaynak Kodu İndirme

```bash
git clone https://github.com/yazilimtek/ferman-browser.git
cd ferman-browser
```

### 4. Derleme

```bash
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++

cmake --build build -j$(nproc)
```

### 5. Çalıştırma

```bash
./build/ferman-browser.exe
```

Ya da Windows Explorer'dan `build/ferman-browser.exe` dosyasını çift tıklayarak açabilirsiniz.

> **DLL Uyarısı:** İlk çalıştırmada eksik DLL hatası alırsanız, `ferman-browser.exe` dosyasının yanına gerekli DLL'leri kopyalamanız gerekir:

```bash
# Gerekli DLL'leri exe'nin yanına kopyala
ldd build/ferman-browser.exe | grep ucrt64 | awk '{print $3}' | xargs -I{} cp {} build/
```

### 6. Dağıtım Paketi Oluşturma (İsteğe Bağlı)

Taşınabilir bir klasör oluşturmak için tüm bağımlılıkları tek klasöre toplayabilirsiniz:

```bash
mkdir -p dist
cp build/ferman-browser.exe dist/

# Tüm bağımlı DLL'leri kopyala
ldd build/ferman-browser.exe \
    | grep '/ucrt64/' \
    | awk '{print $3}' \
    | xargs -I{} cp {} dist/

# GLib şema dosyaları (GTK için gerekli)
cp -r /ucrt64/share/glib-2.0/schemas dist/share/glib-2.0/
glib-compile-schemas dist/share/glib-2.0/schemas/

# GTK teması ve ikonlar
mkdir -p dist/share/icons
cp -r /ucrt64/share/icons/hicolor dist/share/icons/
```

> `dist/` klasörünü ZIP'leyerek GTK4 kurulu olmayan Windows bilgisayarlara taşıyabilirsiniz.

### Windows'a Özgü Notlar

| Konu | Açıklama |
|---|---|
| **Terminal** | Her zaman **MSYS2 UCRT64** terminalini kullanın (MINGW64 veya MSYS değil) |
| **Yol ayraçları** | CMake komutlarında `/` kullanın, `\` değil |
| **Antivirus** | Derleme sırasında bazı antivirüs yazılımları yavaşlatabilir; `build/` klasörünü istisna listesine ekleyin |
| **WebKit süreci** | WebKitGTK, Windows'ta ayrı bir `WebKitNetworkProcess.exe` süreci başlatır — bu normaldir |
| **Veri dizini** | Ayarlar ve yer imleri `%APPDATA%\ferman-browser\` altında saklanır |
| **Poppler/LibZip** | PDF ve DOCX desteği için `poppler` ve `libzip` paketlerini kurun (yukarıda dahil edildi) |

---

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
