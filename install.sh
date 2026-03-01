#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
#  Ferman Browser — Kurulum Scripti
#  Kullanım: sudo ./install.sh [--prefix /usr] [--uninstall]
# ──────────────────────────────────────────────────────────────────────────────

set -e

APP_NAME="ferman-browser"
APP_DISPLAY="Ferman Browser"
VERSION="1.0.0"
PREFIX="/usr"
BUILD_DIR="$(dirname "$0")/build"
SRC_DIR="$(dirname "$0")"

# ── Argüman ayrıştırma ────────────────────────────────────────────────────────
UNINSTALL=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix) PREFIX="$2"; shift 2 ;;
        --uninstall) UNINSTALL=1; shift ;;
        -h|--help)
            echo "Kullanım: sudo $0 [--prefix <yol>] [--uninstall]"
            echo "  --prefix <yol>   Kurulum öneki (varsayılan: /usr)"
            echo "  --uninstall      Uygulamayı kaldır"
            exit 0 ;;
        *) echo "Bilinmeyen seçenek: $1"; exit 1 ;;
    esac
done

# ── Root kontrolü ─────────────────────────────────────────────────────────────
if [[ $EUID -ne 0 ]]; then
    echo "Hata: Bu script root yetkisi gerektirir."
    echo "       sudo $0 $* şeklinde çalıştırın."
    exit 1
fi

# ── Kaldırma modu ─────────────────────────────────────────────────────────────
if [[ $UNINSTALL -eq 1 ]]; then
    echo "==> $APP_DISPLAY kaldırılıyor…"
    rm -fv "$PREFIX/bin/$APP_NAME"
    rm -fv "$PREFIX/share/applications/$APP_NAME.desktop"
    rm -fv "$PREFIX/share/icons/hicolor/16x16/apps/$APP_NAME.png"
    rm -fv "$PREFIX/share/icons/hicolor/32x32/apps/$APP_NAME.png"
    rm -fv "$PREFIX/share/icons/hicolor/192x192/apps/$APP_NAME.png"
    rm -fv "$PREFIX/share/icons/hicolor/512x512/apps/$APP_NAME.png"
    command -v gtk-update-icon-cache &>/dev/null && \
        gtk-update-icon-cache -f "$PREFIX/share/icons/hicolor" || true
    command -v update-desktop-database &>/dev/null && \
        update-desktop-database "$PREFIX/share/applications" || true
    echo "==> Kaldırma tamamlandı."
    exit 0
fi

# ── Bağımlılık kontrolü ───────────────────────────────────────────────────────
echo "==> Bağımlılıklar kontrol ediliyor…"
MISSING=()
for pkg in libgtk-4-dev libwebkitgtk-6.0-dev libsqlite3-dev libsoup-3.0-dev cmake pkg-config g++; do
    if ! dpkg -s "$pkg" &>/dev/null 2>&1; then
        # dpkg yoksa (non-Debian) sadece pkg-config ile kontrol et
        if command -v dpkg &>/dev/null; then
            MISSING+=("$pkg")
        fi
    fi
done
if [[ ${#MISSING[@]} -gt 0 ]]; then
    echo "Uyarı: Şu paketler eksik olabilir: ${MISSING[*]}"
    echo "       sudo apt install ${MISSING[*]}"
fi

# ── Derleme ───────────────────────────────────────────────────────────────────
echo "==> Derleniyor (Release)…"
cmake "$SRC_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -Wno-dev \
    > /dev/null

JOBS=$(nproc 2>/dev/null || echo 2)
cmake --build "$BUILD_DIR" -j"$JOBS"

# ── Kurulum ───────────────────────────────────────────────────────────────────
echo "==> $APP_DISPLAY kuruluyor → $PREFIX"
cmake --install "$BUILD_DIR" --prefix "$PREFIX"

# ── İkon önbelleği ve masaüstü veritabanı ─────────────────────────────────────
echo "==> İkon önbelleği güncelleniyor…"
command -v gtk-update-icon-cache &>/dev/null && \
    gtk-update-icon-cache -f "$PREFIX/share/icons/hicolor" || true

echo "==> Masaüstü veritabanı güncelleniyor…"
command -v update-desktop-database &>/dev/null && \
    update-desktop-database "$PREFIX/share/applications" || true

echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║  $APP_DISPLAY $VERSION başarıyla kuruldu!       ║"
echo "║  Çalıştırmak için: ferman-browser                    ║"
echo "╚══════════════════════════════════════════════════════╝"
