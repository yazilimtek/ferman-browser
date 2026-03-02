#include "browser_window.h"
#include "session_manager.h"
#include "history_manager.h"
#include "bookmark_manager.h"
#include "download_manager.h"
#include "settings_manager.h"
#include "setup_manager.h"
#include <algorithm>
#include <string>
#include <sstream>
#include <fstream>
#include <cstdio>
#include <ctime>
#include <cctype>

// kHomeHTML artık BuildHomeHTML() ile dinamik oluşturuluyor (arama motoru ayarına göre)

static const char* kTabCSS = R"(
.tab-row {
    min-width: 0;
    border-radius: 6px 6px 0 0;
}
.tab-row button,
.tab-row button image,
.tab-row label {
    min-width: 0;
    min-height: 0;
}
.tab-active {
    background-color: alpha(@window_bg_color, 0.92);
    box-shadow: inset 0 -2px 0 @accent_bg_color;
}
.tab-active > label, .tab-active label { color: @window_fg_color; font-weight: bold; }
.tab-inactive { background-color: transparent; }
.tab-inactive > label, .tab-inactive label { color: alpha(@window_fg_color, 0.55); font-weight: normal; }
.tab-inactive:hover { background-color: alpha(@window_fg_color, 0.07); }
.tab-id-label {
    font-size: 0.78rem;
    font-weight: 700;
    opacity: 0.65;
    min-width: 0;
}
.status-bar {
    font-size: 0.75rem;
    padding: 1px 8px;
    color: alpha(@window_fg_color, 0.6);
    background-color: alpha(@window_bg_color, 0.95);
    border-top: 1px solid alpha(@window_fg_color, 0.08);
}
.bookmarked {
    color: #e6a817;
}
.bookmarked image {
    color: #e6a817;
}
.find-bar {
    background-color: alpha(@window_bg_color, 0.97);
    border-top: 1px solid alpha(@window_fg_color, 0.10);
}
)";

namespace ferman {

// ── Constructor ──────────────────────────────────────────────────────────────

BrowserWindow::BrowserWindow(GtkApplication* app) {
    app_    = app;
    window_ = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window_), kAppName);
    gtk_window_set_default_size(GTK_WINDOW(window_), 1280, 800);
    g_object_set_data(G_OBJECT(window_), "browser-window", this);

    // Uygulama ikonu: kurulu sistemde hicolor temasından, geliştirmede dosyadan
    gtk_window_set_icon_name(GTK_WINDOW(window_), "ferman-browser");
    {
        static bool icon_set = false;
        if (!icon_set) {
            // Geliştirme fallback: kaynak klasöründen oku
            static const char* kIconPaths[] = {
                "resources/favicon.png",
                "resources/icons/512x512/apps/ferman-browser.png",
                nullptr
            };
            for (int i = 0; kIconPaths[i]; ++i) {
                GError* err = nullptr;
                GdkPixbuf* pb = gdk_pixbuf_new_from_file(kIconPaths[i], &err);
                if (pb) {
                    gtk_window_set_default_icon_name("ferman-browser");
                    g_object_unref(pb);
                    icon_set = true;
                    break;
                }
                if (err) g_error_free(err);
            }
            icon_set = true;
        }
    }

    // ── Modülleri başlat (sadece ilk pencerede init edilir) ──
    static bool modules_inited = false;
    if (!modules_inited) {
        modules_inited = true;
        // Memory pressure: herhangi bir WebKitNetworkSession oluşturulmadan önce çağrılmalı
        WebKitMemoryPressureSettings* mps = webkit_memory_pressure_settings_new();
        webkit_memory_pressure_settings_set_memory_limit(mps, 512);
        webkit_memory_pressure_settings_set_kill_threshold(mps, 0.95);
        webkit_memory_pressure_settings_set_strict_threshold(mps, 0.80);
        webkit_memory_pressure_settings_set_conservative_threshold(mps, 0.60);
        webkit_network_session_set_memory_pressure_settings(mps);
        webkit_memory_pressure_settings_free(mps);

        const char* home = g_get_home_dir();
        SessionManager::Get().Init();
        std::string data_dir = std::string(home) + "/.local/share/ferman-browser";
        std::string cfg_dir  = std::string(home) + "/.config/ferman-browser";
        HistoryManager::Get().Init(data_dir + "/history.db");
        BookmarkManager::Get().Init(data_dir + "/bookmarks.json");
        SettingsManager::Get().Init(cfg_dir);
        AiManager::Get().Init(data_dir);
        AiAgentStore::Get().Init(cfg_dir);
    }

    // ── Header bar ──
    GtkWidget* header = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(window_), header);

    // Home + Nav butonları — header bar'a (sol)
    GtkWidget* home_btn_hdr = gtk_button_new_from_icon_name("go-home-symbolic");
    gtk_widget_add_css_class(home_btn_hdr, "flat");
    gtk_widget_set_tooltip_text(home_btn_hdr, "Ana Sayfa");
    g_signal_connect(home_btn_hdr, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer ud) {
            auto* self = static_cast<BrowserWindow*>(ud);
            if (!self->active_tab_) return;
            const std::string& hp = SettingsManager::Get().Prefs().homepage;
            std::string url = hp.empty() ? self->kHomePage : hp;
            if (!url.empty() && url.find("://") == std::string::npos)
                url = "https://" + url;
            webkit_web_view_load_uri(
                WEBKIT_WEB_VIEW(self->active_tab_->webview), url.c_str());
        }), this);

    back_btn_   = gtk_button_new_from_icon_name("go-previous-symbolic");
    fwd_btn_    = gtk_button_new_from_icon_name("go-next-symbolic");
    reload_btn_ = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_sensitive(back_btn_, FALSE);
    gtk_widget_set_sensitive(fwd_btn_,  FALSE);
    gtk_widget_set_tooltip_text(back_btn_,   "Geri (Alt+Sol)");
    gtk_widget_set_tooltip_text(fwd_btn_,    "İleri (Alt+Sağ)");
    gtk_widget_set_tooltip_text(reload_btn_, "Yenile (Ctrl+R)");
    gtk_widget_add_css_class(back_btn_,   "flat");
    gtk_widget_add_css_class(fwd_btn_,    "flat");
    gtk_widget_add_css_class(reload_btn_, "flat");

    g_signal_connect(back_btn_,   "clicked", G_CALLBACK(OnBackCb),    this);
    g_signal_connect(fwd_btn_,    "clicked", G_CALLBACK(OnForwardCb), this);
    g_signal_connect(reload_btn_, "clicked", G_CALLBACK(OnReloadCb),  this);

    GtkWidget* nav_box_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(nav_box_hdr, "linked");
    gtk_box_append(GTK_BOX(nav_box_hdr), home_btn_hdr);
    gtk_box_append(GTK_BOX(nav_box_hdr), back_btn_);
    gtk_box_append(GTK_BOX(nav_box_hdr), fwd_btn_);
    gtk_box_append(GTK_BOX(nav_box_hdr), reload_btn_);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), nav_box_hdr);

    // Tab kutusu — doğrudan title_widget olarak eklenir,
    // GTK4 header bar onu sol/sağ widget'lar arasındaki alana sıkıştırır
    tab_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_set_homogeneous(GTK_BOX(tab_box_), FALSE);
    gtk_widget_set_halign(tab_box_, GTK_ALIGN_START);
    gtk_widget_set_hexpand(tab_box_, TRUE);
    gtk_widget_set_vexpand(tab_box_, TRUE);
    gtk_widget_set_valign(tab_box_, GTK_ALIGN_FILL);
    gtk_widget_set_overflow(tab_box_, GTK_OVERFLOW_HIDDEN);

    new_tab_btn_ = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_set_tooltip_text(new_tab_btn_, "Yeni sekme");
    gtk_widget_add_css_class(new_tab_btn_, "flat");

    // tab_box_'u pack_start ile ekle — sola yaslanır
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), tab_box_);

    // ── Hamburger menü (sağ) ──
    GMenu* menu_model = g_menu_new();

    // Sekme / Pencere bölümü
    GMenu* tab_section = g_menu_new();
    g_menu_append(tab_section,    "Yeni Sekme\t\t\tCtrl+T",   "win.new-tab");
    g_menu_append(tab_section,    "Yeni Pencere\t\tCtrl+N",   "win.new-window");
    g_menu_append(tab_section,    "Sekmeyi Kapat\t\tCtrl+W",  "win.close-tab");
    g_menu_append_section(menu_model, "", G_MENU_MODEL(tab_section));
    g_object_unref(tab_section);

    // Gezinti bölümü
    GMenu* nav_section = g_menu_new();
    g_menu_append(nav_section,    "Geri\t\t\t\tAlt+Sol",      "win.go-back");
    g_menu_append(nav_section,    "İleri\t\t\t\tAlt+Sağ",     "win.go-forward");
    g_menu_append(nav_section,    "Yenile\t\t\t\tCtrl+R",     "win.reload");
    g_menu_append(nav_section,    "Ana Sayfa",                "win.go-home");
    g_menu_append_section(menu_model, "Gezinti", G_MENU_MODEL(nav_section));
    g_object_unref(nav_section);

    // Görünüm bölümü
    GMenu* view_section = g_menu_new();
    g_menu_append(view_section,   "Yakınlaştır",               "win.zoom-in");
    g_menu_append(view_section,   "Uzaklaştır",               "win.zoom-out");
    g_menu_append(view_section,   "Varsayılan Boyut",          "win.zoom-reset");
    g_menu_append(view_section,   "Tam Ekran\t\t\tF11",       "win.fullscreen");
    g_menu_append(view_section,   "Favoriler",                 "win.toggle-bookmarks-bar");
    g_menu_append(view_section,   "Sayfada Ara\t\tCtrl+F",     "win.find-in-page");
    g_menu_append_section(menu_model, "Görünüm", G_MENU_MODEL(view_section));
    g_object_unref(view_section);

    // Araçlar bölümü
    GMenu* tools_section = g_menu_new();
    g_menu_append(tools_section,  "Geçmiş",                    "win.show-history");
    g_menu_append(tools_section,  "İndirmeler",                "win.show-downloads");
    g_menu_append(tools_section,  "Sayfayı Kaydet\t\tCtrl+S",  "win.save-page");
    g_menu_append(tools_section,  "Sayfayı Yazdır\t\tCtrl+P",  "win.print-page");
    g_menu_append_section(menu_model, "Araçlar", G_MENU_MODEL(tools_section));
    g_object_unref(tools_section);

    // Ayarlar
    GMenu* sys_section = g_menu_new();
    g_menu_append(sys_section,    "Ayarlar",                   "win.settings");
    g_menu_append_section(menu_model, "", G_MENU_MODEL(sys_section));
    g_object_unref(sys_section);

    // GActionGroup — window actions
    GSimpleActionGroup* ag = g_simple_action_group_new();
    struct { const char* name; } actions[] = {
        {"new-tab"}, {"new-window"}, {"close-tab"},
        {"reload"}, {"go-back"}, {"go-forward"}, {"go-home"},
        {"zoom-in"}, {"zoom-out"}, {"zoom-reset"},
        {"save-page"}, {"print-page"}, {"fullscreen"},
        {"clear-history"}, {"settings"}, {"about"},
        {"show-history"}, {"show-downloads"}, {"toggle-bookmarks-bar"}, {"find-in-page"}
    };
    for (auto& a : actions) {
        GSimpleAction* act = g_simple_action_new(a.name, nullptr);
        g_signal_connect(act, "activate", G_CALLBACK(OnMenuActionCb), this);
        g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act));
        g_object_unref(act);
    }
    // URI parametreli action (yeni pencerede aç)
    {
        GSimpleAction* act = g_simple_action_new("open-new-window", G_VARIANT_TYPE_STRING);
        g_signal_connect(act, "activate", G_CALLBACK(OnMenuActionWithParamCb), this);
        g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act));
        g_object_unref(act);
    }
    gtk_widget_insert_action_group(window_, "win", G_ACTION_GROUP(ag));
    g_object_unref(ag);

    menu_btn_ = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn_), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_btn_), G_MENU_MODEL(menu_model));
    gtk_widget_add_css_class(menu_btn_, "flat");
    gtk_widget_set_tooltip_text(menu_btn_, "Menü");
    g_object_unref(menu_model);
    // pack_end sırası GTK4'te ters: son eklenen en sola gider
    // İstenen görsel sıra (soldan sağa): [+] [⬇] [zoom] [☰]

    // Zoom göstergesi (header bar'dan kaldırıldı, sadece menüden kullanılıyor)
    // zoom_reset_btn_ — zoom seviyesi değişince etiketi güncellemek için saklıyoruz
    // (GTK menubar'da dinamik label güncelleme mümkün değil, bu yüzden header'da minimal bir gösterge)
    GtkWidget* zoom_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(zoom_bar, "linked");

    GtkWidget* zoom_out_hdr = gtk_button_new_from_icon_name("zoom-out-symbolic");
    gtk_widget_add_css_class(zoom_out_hdr, "flat");
    gtk_widget_set_tooltip_text(zoom_out_hdr, "Uzaklaştır");
    g_signal_connect(zoom_out_hdr, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer ud) {
            auto* self = static_cast<BrowserWindow*>(ud);
            if (!self->active_tab_) return;
            auto* wv = WEBKIT_WEB_VIEW(self->active_tab_->webview);
            double z = std::max(webkit_web_view_get_zoom_level(wv) - 0.1, 0.25);
            webkit_web_view_set_zoom_level(wv, z);
            if (self->zoom_reset_btn_) {
                char buf[12]; snprintf(buf, sizeof(buf), "%.0f%%", z * 100.0);
                gtk_button_set_label(GTK_BUTTON(self->zoom_reset_btn_), buf);
            }
        }), this);

    zoom_reset_btn_ = gtk_button_new_with_label("100%");
    gtk_widget_add_css_class(zoom_reset_btn_, "flat");
    gtk_widget_set_tooltip_text(zoom_reset_btn_, "Varsayılan boyut");
    g_signal_connect(zoom_reset_btn_, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer ud) {
            auto* self = static_cast<BrowserWindow*>(ud);
            if (!self->active_tab_) return;
            webkit_web_view_set_zoom_level(
                WEBKIT_WEB_VIEW(self->active_tab_->webview), 1.0);
            gtk_button_set_label(GTK_BUTTON(self->zoom_reset_btn_), "100%");
        }), this);

    GtkWidget* zoom_in_hdr = gtk_button_new_from_icon_name("zoom-in-symbolic");
    gtk_widget_add_css_class(zoom_in_hdr, "flat");
    gtk_widget_set_tooltip_text(zoom_in_hdr, "Yakınlaştır");
    g_signal_connect(zoom_in_hdr, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer ud) {
            auto* self = static_cast<BrowserWindow*>(ud);
            if (!self->active_tab_) return;
            auto* wv = WEBKIT_WEB_VIEW(self->active_tab_->webview);
            double z = std::min(webkit_web_view_get_zoom_level(wv) + 0.1, 5.0);
            webkit_web_view_set_zoom_level(wv, z);
            if (self->zoom_reset_btn_) {
                char buf[12]; snprintf(buf, sizeof(buf), "%.0f%%", z * 100.0);
                gtk_button_set_label(GTK_BUTTON(self->zoom_reset_btn_), buf);
            }
        }), this);

    gtk_box_append(GTK_BOX(zoom_bar), zoom_out_hdr);
    gtk_box_append(GTK_BOX(zoom_bar), zoom_reset_btn_);
    gtk_box_append(GTK_BOX(zoom_bar), zoom_in_hdr);

    // İndirme butonu
    DownloadManager::Get().Init(GTK_WINDOW(window_));
    download_btn_ = DownloadManager::Get().GetPanelButton();

    // GTK4 pack_end: son eklenen en sola gider.
    // Görsel sıra (soldan sağa): [+] [⬇] [zoom] [☰]
    // Ekleme sırası (tersten): menu → zoom → download → new_tab
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_btn_);    // en sağ
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), zoom_bar);     // menünün solu
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), download_btn_); // zoom'un solu
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), new_tab_btn_);  // download'ın solu

    // İndirme sinyali
    WebKitNetworkSession* net_sess = SessionManager::Get().GetSession();
    g_signal_connect(net_sess, "download-started",
        G_CALLBACK(+[](WebKitNetworkSession*, WebKitDownload* dl, gpointer) {
            DownloadManager::Get().OnDownloadStarted(dl);
        }), nullptr);

    // ── İçerik alanı: URL bar + stack ──
    GtkWidget* content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    // URL bar satırı
    GtkWidget* url_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_margin_start(url_bar, 6);
    gtk_widget_set_margin_end(url_bar, 6);
    gtk_widget_set_margin_top(url_bar, 4);
    gtk_widget_set_margin_bottom(url_bar, 4);

    url_entry_ = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(url_entry_), "Adres veya arama girin…");
    gtk_widget_set_hexpand(url_entry_, TRUE);

    GtkWidget* clear_btn = gtk_button_new_from_icon_name("edit-clear-symbolic");
    gtk_widget_add_css_class(clear_btn, "flat");
    gtk_widget_set_tooltip_text(clear_btn, "Adresi temizle");
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(OnClearUrlCb), this);

    GtkWidget* copy_btn = gtk_button_new_from_icon_name("edit-copy-symbolic");
    gtk_widget_add_css_class(copy_btn, "flat");
    gtk_widget_set_tooltip_text(copy_btn, "Adresi kopyala");
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(OnCopyUrlCb), this);

    fav_btn_ = gtk_button_new_from_icon_name("non-starred-symbolic");
    gtk_widget_add_css_class(fav_btn_, "flat");
    gtk_widget_set_tooltip_text(fav_btn_, "Favorilere ekle / kaldır");
    g_signal_connect(fav_btn_, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer ud) {
            auto* self = static_cast<BrowserWindow*>(ud);
            if (!self->active_tab_ || self->active_tab_->url.empty()) return;
            const std::string& url = self->active_tab_->url;
            if (BookmarkManager::Get().IsBookmarked(url))
                BookmarkManager::Get().Remove(url);
            else
                BookmarkManager::Get().Add(url, self->active_tab_->title);
            self->UpdateFavButton();
            self->RebuildBookmarksBar();
        }), this);

    // URL öneri listesi: ok tuşları ile gezinme
    GtkEventController* url_key = gtk_event_controller_key_new();
    g_signal_connect(url_key, "key-pressed",
        G_CALLBACK(+[](GtkEventControllerKey*, guint keyval, guint,
                       GdkModifierType, gpointer ud) -> gboolean {
            auto* self = static_cast<BrowserWindow*>(ud);
            if (!self->suggest_pop_ || !self->suggest_list_) return FALSE;
            if (!gtk_widget_get_visible(self->suggest_pop_)) return FALSE;
            if (keyval == GDK_KEY_Down) {
                GtkListBoxRow* sel = gtk_list_box_get_selected_row(GTK_LIST_BOX(self->suggest_list_));
                int idx = sel ? gtk_list_box_row_get_index(sel) + 1 : 0;
                GtkListBoxRow* next = gtk_list_box_get_row_at_index(GTK_LIST_BOX(self->suggest_list_), idx);
                if (next) gtk_list_box_select_row(GTK_LIST_BOX(self->suggest_list_), next);
                return TRUE;
            }
            if (keyval == GDK_KEY_Up) {
                GtkListBoxRow* sel = gtk_list_box_get_selected_row(GTK_LIST_BOX(self->suggest_list_));
                if (sel) {
                    int idx = gtk_list_box_row_get_index(sel) - 1;
                    if (idx >= 0) {
                        GtkListBoxRow* prev = gtk_list_box_get_row_at_index(GTK_LIST_BOX(self->suggest_list_), idx);
                        if (prev) gtk_list_box_select_row(GTK_LIST_BOX(self->suggest_list_), prev);
                    }
                }
                return TRUE;
            }
            if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
                GtkListBoxRow* sel = gtk_list_box_get_selected_row(GTK_LIST_BOX(self->suggest_list_));
                if (sel) {
                    const char* url = static_cast<const char*>(g_object_get_data(G_OBJECT(sel), "url"));
                    if (url && *url && self->active_tab_) {
                        gtk_editable_set_text(GTK_EDITABLE(self->url_entry_), url);
                        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(self->active_tab_->webview), url);
                        self->HideUrlSuggestions();
                        return TRUE;
                    }
                }
                return FALSE;
            }
            if (keyval == GDK_KEY_Escape) { self->HideUrlSuggestions(); return TRUE; }
            return FALSE;
        }), this);
    gtk_widget_add_controller(url_entry_, url_key);

    // URL autocomplete: sadece kullanıcı yazınca açılsın (programatik set_text tetiklemesin)
    g_signal_connect(url_entry_, "changed",
        G_CALLBACK(+[](GtkEditable* ed, gpointer ud) {
            auto* self = static_cast<BrowserWindow*>(ud);
            // Focus yoksa (sayfa yükleme programatik seçimi) gizle
            if (!gtk_widget_has_focus(GTK_WIDGET(ed))) {
                self->HideUrlSuggestions();
                return;
            }
            const char* txt = gtk_editable_get_text(ed);
            if (txt && strlen(txt) >= 2)
                self->ShowUrlSuggestions(txt);
            else
                self->HideUrlSuggestions();
        }), this);

    GtkWidget* entry_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(entry_row, TRUE);
    gtk_box_append(GTK_BOX(entry_row), url_entry_);
    gtk_box_append(GTK_BOX(entry_row), clear_btn);
    gtk_box_append(GTK_BOX(entry_row), copy_btn);
    gtk_box_append(GTK_BOX(entry_row), fav_btn_);

    // AI butonu
    ai_btn_ = gtk_button_new_with_label("AI");
    gtk_widget_add_css_class(ai_btn_, "ai-btn");
    gtk_widget_set_tooltip_text(ai_btn_, "AI Asistan (Ctrl+Shift+A)");
    g_signal_connect(ai_btn_, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer ud){
            static_cast<BrowserWindow*>(ud)->ToggleAiPanel();
        }), this);
    gtk_box_append(GTK_BOX(entry_row), ai_btn_);
    gtk_box_append(GTK_BOX(url_bar), entry_row);

    gtk_box_append(GTK_BOX(content_box), url_bar);

    // Favoriler barı (başlangıçta gizli)
    bookmarks_bar_ = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(bookmarks_bar_),
        GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(GTK_REVEALER(bookmarks_bar_), 200);
    gtk_revealer_set_reveal_child(GTK_REVEALER(bookmarks_bar_), FALSE);

    GtkWidget* bbar_frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* bbar_sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(bbar_frame), bbar_sep);

    bookmarks_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(bookmarks_box_, 6);
    gtk_widget_set_margin_end(bookmarks_box_, 6);
    gtk_widget_set_margin_top(bookmarks_box_, 3);
    gtk_widget_set_margin_bottom(bookmarks_box_, 3);
    gtk_box_append(GTK_BOX(bbar_frame), bookmarks_box_);
    gtk_revealer_set_child(GTK_REVEALER(bookmarks_bar_), bbar_frame);
    gtk_box_append(GTK_BOX(content_box), bookmarks_bar_);
    RebuildBookmarksBar();

    // WebView stack
    stack_ = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack_), GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_widget_set_hexpand(stack_, TRUE);
    gtk_widget_set_vexpand(stack_, TRUE);

    // AI panel’u oluştur
    BuildAiPanel();

    // Stack + AI outer yan yana (GtkPaned)
    GtkWidget* main_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(main_paned), stack_);
    gtk_paned_set_end_child(GTK_PANED(main_paned), ai_outer_);
    gtk_paned_set_resize_start_child(GTK_PANED(main_paned), TRUE);
    gtk_paned_set_resize_end_child(GTK_PANED(main_paned), FALSE);
    gtk_paned_set_shrink_start_child(GTK_PANED(main_paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(main_paned), FALSE);
    gtk_widget_set_hexpand(main_paned, TRUE);
    gtk_widget_set_vexpand(main_paned, TRUE);
    gtk_box_append(GTK_BOX(content_box), main_paned);

    // ── Sayfa içi arama bar'ı (Ctrl+F) ─────────────────────────────────────
    find_bar_ = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(find_bar_),
        GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(GTK_REVEALER(find_bar_), 180);
    gtk_revealer_set_reveal_child(GTK_REVEALER(find_bar_), FALSE);

    GtkWidget* find_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(find_box, "find-bar");
    gtk_widget_set_margin_start(find_box, 8);
    gtk_widget_set_margin_end(find_box, 8);
    gtk_widget_set_margin_top(find_box, 4);
    gtk_widget_set_margin_bottom(find_box, 4);

    GtkWidget* find_lbl = gtk_label_new("Sayfada Ara:");
    gtk_widget_add_css_class(find_lbl, "dim-label");
    gtk_box_append(GTK_BOX(find_box), find_lbl);

    find_entry_ = gtk_search_entry_new();
    gtk_widget_set_size_request(find_entry_, 240, -1);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(find_entry_), "Ara…");
    gtk_widget_set_hexpand(find_entry_, FALSE);
    gtk_box_append(GTK_BOX(find_box), find_entry_);

    find_count_lbl_ = gtk_label_new("");
    gtk_widget_add_css_class(find_count_lbl_, "dim-label");
    gtk_widget_set_size_request(find_count_lbl_, 60, -1);
    gtk_box_append(GTK_BOX(find_box), find_count_lbl_);

    GtkWidget* find_prev_btn = gtk_button_new_from_icon_name("go-up-symbolic");
    gtk_widget_add_css_class(find_prev_btn, "flat");
    gtk_widget_set_tooltip_text(find_prev_btn, "Önceki eşleşme");
    g_object_set_data(G_OBJECT(find_prev_btn), "bw", this);
    g_signal_connect(find_prev_btn, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer ud) {
            static_cast<BrowserWindow*>(ud)->FindPrev();
        }), this);
    gtk_box_append(GTK_BOX(find_box), find_prev_btn);

    GtkWidget* find_next_btn = gtk_button_new_from_icon_name("go-down-symbolic");
    gtk_widget_add_css_class(find_next_btn, "flat");
    gtk_widget_set_tooltip_text(find_next_btn, "Sonraki eşleşme");
    g_signal_connect(find_next_btn, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer ud) {
            static_cast<BrowserWindow*>(ud)->FindNext();
        }), this);
    gtk_box_append(GTK_BOX(find_box), find_next_btn);

    GtkWidget* find_close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(find_close_btn, "flat");
    gtk_widget_set_tooltip_text(find_close_btn, "Aramayı kapat");
    gtk_widget_set_halign(find_close_btn, GTK_ALIGN_END);
    gtk_widget_set_hexpand(find_close_btn, TRUE);
    g_signal_connect(find_close_btn, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer ud) {
            static_cast<BrowserWindow*>(ud)->HideFindBar();
        }), this);
    gtk_box_append(GTK_BOX(find_box), find_close_btn);

    // Search entry sinyalleri
    g_object_set_data(G_OBJECT(find_entry_), "bw", this);
    g_signal_connect(find_entry_, "search-changed",
        G_CALLBACK(+[](GtkSearchEntry* e, gpointer ud) {
            auto* self = static_cast<BrowserWindow*>(ud);
            if (!self->active_tab_ || !self->active_tab_->webview) return;
            const char* q = gtk_editable_get_text(GTK_EDITABLE(e));
            WebKitFindController* fc = webkit_web_view_get_find_controller(
                WEBKIT_WEB_VIEW(self->active_tab_->webview));
            if (!q || !*q) {
                webkit_find_controller_search_finish(fc);
                gtk_label_set_text(GTK_LABEL(self->find_count_lbl_), "");
                return;
            }
            webkit_find_controller_search(fc, q,
                static_cast<WebKitFindOptions>(
                    WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE |
                    WEBKIT_FIND_OPTIONS_WRAP_AROUND),
                512);
        }), this);
    // Escape kapatır
    GtkEventController* find_key = gtk_event_controller_key_new();
    g_signal_connect(find_key, "key-pressed",
        G_CALLBACK(+[](GtkEventControllerKey*, guint keyval, guint,
                       GdkModifierType, gpointer ud) -> gboolean {
            auto* self = static_cast<BrowserWindow*>(ud);
            if (keyval == GDK_KEY_Escape) { self->HideFindBar(); return TRUE; }
            if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
                self->FindNext(); return TRUE;
            }
            return FALSE;
        }), this);
    gtk_widget_add_controller(find_entry_, find_key);

    gtk_revealer_set_child(GTK_REVEALER(find_bar_), find_box);
    gtk_box_append(GTK_BOX(content_box), find_bar_);

    // Status bar (fare hover URL)
    status_bar_ = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(status_bar_), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(status_bar_), 0.0f);
    gtk_widget_add_css_class(status_bar_, "status-bar");
    gtk_widget_set_hexpand(status_bar_, TRUE);
    gtk_widget_set_visible(status_bar_, FALSE);
    gtk_box_append(GTK_BOX(content_box), status_bar_);

    gtk_window_set_child(GTK_WINDOW(window_), content_box);

    // ── Signals ──
    g_signal_connect(url_entry_,  "activate", G_CALLBACK(OnUrlActivateCb), this);
    g_signal_connect(new_tab_btn_,"clicked",  G_CALLBACK(OnNewTabCb),    this);

    // CSS provider
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, kTabCSS);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    // ── Klavye kısayolları (Chromium benzeri) ──
    GtkEventController* sc = gtk_shortcut_controller_new();
    gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(sc), GTK_SHORTCUT_SCOPE_MANAGED);
    auto add_shortcut = [&](const char* trigger_str, const char* action_str) {
        GtkShortcut* s = gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string(trigger_str),
            gtk_named_action_new(action_str));
        gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(sc), s);
    };
    add_shortcut("<Control>t",       "win.new-tab");
    add_shortcut("<Control>n",       "win.new-window");
    add_shortcut("<Control>r",       "win.reload");
    add_shortcut("F5",               "win.reload");
    add_shortcut("<Alt>Left",        "win.go-back");
    add_shortcut("<Alt>Right",       "win.go-forward");
    add_shortcut("F11",              "win.fullscreen");
    add_shortcut("<Control>p",       "win.print-page");
    add_shortcut("<Control>s",       "win.save-page");
    add_shortcut("<Control>equal",   "win.zoom-in");
    add_shortcut("<Control>plus",    "win.zoom-in");
    add_shortcut("<Control>minus",   "win.zoom-out");
    add_shortcut("<Control>0",       "win.zoom-reset");
    gtk_widget_add_controller(window_, sc);

    // Ctrl+L: URL bar'a odaklan (GtkEntry direkt)
    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed",
        G_CALLBACK(+[](GtkEventControllerKey*, guint keyval, guint,
                       GdkModifierType state, gpointer ud) -> gboolean {
            auto* self = static_cast<BrowserWindow*>(ud);
            if ((state & GDK_CONTROL_MASK) && keyval == GDK_KEY_l) {
                gtk_widget_grab_focus(self->url_entry_);
                gtk_editable_select_region(GTK_EDITABLE(self->url_entry_), 0, -1);
                return TRUE;
            }
            if ((state & GDK_CONTROL_MASK) && keyval == GDK_KEY_w) {
                if (self->active_tab_) self->CloseTab(self->active_tab_);
                return TRUE;
            }
            if ((state & GDK_CONTROL_MASK) && keyval == GDK_KEY_f) {
                self->ShowFindBar();
                return TRUE;
            }
            if (keyval == GDK_KEY_F3) {
                if (state & GDK_SHIFT_MASK) self->FindPrev();
                else                        self->FindNext();
                return TRUE;
            }
            if (keyval == GDK_KEY_Escape && self->find_visible_) {
                self->HideFindBar();
                return TRUE;
            }
            return FALSE;
        }), this);
    gtk_widget_add_controller(window_, key_ctrl);

    // Pencere kapanırken sekmeleri kaydet
    g_signal_connect(window_, "close-request",
        G_CALLBACK(+[](GtkWindow*, gpointer ud) -> gboolean {
            auto* self = static_cast<BrowserWindow*>(ud);
            if (SettingsManager::Get().Prefs().restore_tabs)
                self->SaveTabSession();
            return FALSE; // pencereyi kapat
        }), this);

    // İlk tab: kurulum kontrolü → restore_tabs → ana sayfa
    if (SettingsManager::Get().IsFirstRun()) {
        // İLK ÇALIŞTIRMA - Kurulum ekranı göster
        NewTab("ferman://setup");
    } else if (SettingsManager::Get().Prefs().restore_tabs) {
        RestoreTabSession();
        // Hiç sekme açılmadıysa (ilk çalıştırma veya boş session) ana sayfa aç
        if (tabs_.empty())
            NewTab(kHomePage);
    } else {
        NewTab(kHomePage);
    }

    gtk_window_present(GTK_WINDOW(window_));
}

// ── Tab yönetimi ─────────────────────────────────────────────────────────────

Tab* BrowserWindow::NewTab(const std::string& url, bool load, bool switch_to) {
    // Maksimum sekme limiti kontrolü
    const int max_t = SettingsManager::Get().Prefs().max_tabs;
    if (max_t > 0 && (int)tabs_.size() >= max_t) {
        if (!tabs_.empty()) SwitchToTab(tabs_.back());
        return tabs_.empty() ? nullptr : tabs_.back();
    }
    auto* tab = new Tab();
    tab->id   = next_tab_id_++;
    tab->url  = url;

    // WebView — kalıcı network session ile
    tab->webview = GTK_WIDGET(g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "network-session", SessionManager::Get().GetSession(),
        nullptr));
    WebKitSettings* wk_settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(tab->webview));
    // Chrome UA — Google ve modern siteler için (tam platform bilgisi dahil)
    webkit_settings_set_user_agent(wk_settings,
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36");
    const auto& prefs = SettingsManager::Get().Prefs();
    webkit_settings_set_enable_javascript(wk_settings, prefs.javascript_enabled);
    webkit_settings_set_enable_media(wk_settings, TRUE);
    webkit_settings_set_javascript_can_open_windows_automatically(wk_settings, TRUE);
    webkit_settings_set_allow_modal_dialogs(wk_settings, TRUE);
    webkit_settings_set_enable_smooth_scrolling(wk_settings, TRUE);
    webkit_settings_set_media_playback_requires_user_gesture(wk_settings, FALSE);
    webkit_settings_set_enable_webgl(wk_settings, TRUE);
    webkit_settings_set_enable_webaudio(wk_settings, TRUE);
    webkit_settings_set_enable_page_cache(wk_settings, TRUE);
    webkit_settings_set_enable_dns_prefetching(wk_settings, TRUE);
    webkit_settings_set_hardware_acceleration_policy(wk_settings,
        prefs.hardware_accel
            ? WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS
            : WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
    webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(tab->webview), prefs.default_zoom);
    webkit_settings_set_default_font_size(wk_settings, (guint32)prefs.font_size);
    webkit_settings_set_minimum_font_size(wk_settings, (guint32)prefs.min_font_size);
    gtk_widget_set_hexpand(tab->webview, TRUE);
    gtk_widget_set_vexpand(tab->webview, TRUE);
    g_object_set_data(G_OBJECT(tab->webview), "tab", tab);

    g_signal_connect(tab->webview, "load-changed",        G_CALLBACK(OnLoadChangedCb),        this);
    g_signal_connect(tab->webview, "notify::uri",         G_CALLBACK(OnUriChangedCb),         this);
    g_signal_connect(tab->webview, "notify::title",       G_CALLBACK(OnTitleChangedCb),       this);
    g_signal_connect(tab->webview, "notify::favicon",     G_CALLBACK(OnFaviconChangedCb),     this);
    g_signal_connect(tab->webview, "mouse-target-changed",G_CALLBACK(OnMouseTargetChangedCb), this);
    g_signal_connect(tab->webview, "decide-policy",       G_CALLBACK(OnDecidePolicyCb),       this);
    g_signal_connect(tab->webview, "context-menu",        G_CALLBACK(OnContextMenuCb),        this);
    g_signal_connect(tab->webview, "create",              G_CALLBACK(OnCreateWebViewCb),      this);
    g_signal_connect(tab->webview, "print",               G_CALLBACK(OnPrintCb),              this);

    // Stack'e ekle
    std::string page_name = "tab_" + std::to_string(tab->id);
    gtk_stack_add_named(GTK_STACK(stack_), tab->webview, page_name.c_str());
    g_object_set_data(G_OBJECT(tab->webview), "page_name",
                      g_strdup(page_name.c_str()));

    // Tab satırı: [#id] [favicon] [başlık] [kapat]
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(row, "tab-row");
    gtk_widget_set_margin_start(row, 2);
    gtk_widget_set_margin_end(row, 2);
    gtk_widget_set_margin_top(row, 0);
    gtk_widget_set_margin_bottom(row, 0);
    gtk_widget_set_hexpand(row, FALSE);
    gtk_widget_set_halign(row, GTK_ALIGN_START);
    gtk_widget_set_vexpand(row, TRUE);
    gtk_widget_set_valign(row, GTK_ALIGN_FILL);

    // #id etiketi (küçük, soluk)
    std::string id_str = "#" + std::to_string(tab->id);
    GtkWidget* id_label = gtk_label_new(id_str.c_str());
    gtk_widget_add_css_class(id_label, "tab-id-label");
    gtk_widget_set_hexpand(id_label, FALSE);
    gtk_widget_set_valign(id_label, GTK_ALIGN_CENTER);

    // Favicon (başlangıçta gizli — gerçek favicon gelince gösterilir)
    tab->favicon = gtk_image_new();
    gtk_image_set_pixel_size(GTK_IMAGE(tab->favicon), 16);
    gtk_widget_set_valign(tab->favicon, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(tab->favicon, FALSE);
    gtk_widget_set_visible(tab->favicon, FALSE);

    // Tab başlığı etiketi — sola yaslı, hexpand ile kapat butonu sağa itilir
    tab->label = gtk_label_new("Yeni Sekme");
    gtk_label_set_width_chars(GTK_LABEL(tab->label), 1);
    gtk_label_set_max_width_chars(GTK_LABEL(tab->label), 16);
    gtk_label_set_ellipsize(GTK_LABEL(tab->label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(tab->label, TRUE);
    gtk_widget_set_halign(tab->label, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(tab->label), 0.0f);

    GtkWidget* close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(close_btn, "flat");
    gtk_widget_add_css_class(close_btn, "circular");
    gtk_widget_set_focus_on_click(close_btn, FALSE);
    gtk_widget_set_hexpand(close_btn, FALSE);
    gtk_widget_set_halign(close_btn, GTK_ALIGN_END);
    gtk_widget_set_tooltip_text(close_btn, "Sekmeyi kapat");
    g_object_set_data(G_OBJECT(close_btn), "tab", tab);
    g_signal_connect(close_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer ud) {
        auto* self = static_cast<BrowserWindow*>(ud);
        auto* t    = static_cast<Tab*>(g_object_get_data(G_OBJECT(btn), "tab"));
        self->CloseTab(t);
    }), this);

    gtk_box_append(GTK_BOX(row), id_label);
    gtk_box_append(GTK_BOX(row), tab->favicon);
    gtk_box_append(GTK_BOX(row), tab->label);
    gtk_box_append(GTK_BOX(row), close_btn);

    // row üzerindeki gesture — tıklanan widget close_btn değilse tab seç
    g_object_set_data(G_OBJECT(row), "tab",       tab);
    g_object_set_data(G_OBJECT(row), "close_btn", close_btn);

    GtkGesture* gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_PRIMARY);
    // BUBBLE fazı: close_btn kendi clicked'ini işledikten sonra row gesture tetiklenir.
    // gesture pressed callback'te close_btn'e tıklanıp tıklanmadığını kontrol ediyoruz.
    gtk_event_controller_set_propagation_phase(
        GTK_EVENT_CONTROLLER(gesture), GTK_PHASE_BUBBLE);
    g_signal_connect(gesture, "pressed",
        G_CALLBACK(+[](GtkGestureClick* g, int, double x, double y, gpointer ud) {
            GtkWidget* row_w   = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
            GtkWidget* close_w = static_cast<GtkWidget*>(
                g_object_get_data(G_OBJECT(row_w), "close_btn"));
            GtkWidget* picked  = gtk_widget_pick(row_w, x, y, GTK_PICK_DEFAULT);
            // Eğer tıklanan widget close_btn hiyerarşisindeyse, SwitchToTab çağırma
            GtkWidget* ancestor = picked;
            while (ancestor) {
                if (ancestor == close_w) return;
                ancestor = gtk_widget_get_parent(ancestor);
            }
            auto* self = static_cast<BrowserWindow*>(ud);
            auto* t    = static_cast<Tab*>(g_object_get_data(G_OBJECT(row_w), "tab"));
            if (t) self->SwitchToTab(t);
        }), this);
    gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(gesture));

    gtk_box_append(GTK_BOX(tab_box_), row);
    tab->tab_btn = row;

    tabs_.push_back(tab);
    if (load) {
        if (url == kHomePage) {
            std::string home_html = BuildHomeHTML();
            webkit_web_view_load_html(WEBKIT_WEB_VIEW(tab->webview), home_html.c_str(), nullptr);
        } else {
            std::string load_url = url;
            if (load_url.find("://") == std::string::npos)
                load_url = "https://" + load_url;
            webkit_web_view_load_uri(WEBKIT_WEB_VIEW(tab->webview), load_url.c_str());
        }
    }
    if (switch_to) SwitchToTab(tab);
    return tab;
}

void BrowserWindow::CloseTab(Tab* tab) {
    auto it = std::find(tabs_.begin(), tabs_.end(), tab);
    if (it == tabs_.end()) return;

    // Son tab kapanıyorsa: yeni home tab aç, uygulamayı kapatma
    if (tabs_.size() == 1) {
        gtk_box_remove(GTK_BOX(tab_box_), tab->tab_btn);
        gtk_stack_remove(GTK_STACK(stack_), tab->webview);
        tabs_.erase(it);
        active_tab_ = nullptr;
        delete tab;
        NewTab(kHomePage);
        return;
    }

    // En yüksek id'li diğer taba geç
    Tab* next_tab = nullptr;
    int  best_id  = -1;
    for (auto* t : tabs_) {
        if (t != tab && t->id > best_id) {
            best_id  = t->id;
            next_tab = t;
        }
    }

    gtk_box_remove(GTK_BOX(tab_box_), tab->tab_btn);
    gtk_stack_remove(GTK_STACK(stack_), tab->webview);
    tabs_.erase(it);
    delete tab;

    if (next_tab) SwitchToTab(next_tab);
}

void BrowserWindow::SwitchToTab(Tab* tab) {
    active_tab_ = tab;

    const char* page_name = static_cast<const char*>(
        g_object_get_data(G_OBJECT(tab->webview), "page_name"));
    gtk_stack_set_visible_child_name(GTK_STACK(stack_), page_name);

    // Aktif tab stili: inline CSS ile zemin + yazı rengi kontrast
    for (auto* t : tabs_) {
        if (!t->tab_btn) continue;
        if (t == tab) {
            gtk_widget_add_css_class(t->tab_btn, "tab-active");
            gtk_widget_remove_css_class(t->tab_btn, "tab-inactive");
        } else {
            gtk_widget_add_css_class(t->tab_btn, "tab-inactive");
            gtk_widget_remove_css_class(t->tab_btn, "tab-active");
        }
    }

    UpdateUrlEntry();
    UpdateNavButtons();
    UpdateFavButton();

    // Zoom göstergesini güncelle
    if (zoom_reset_btn_) {
        double z = webkit_web_view_get_zoom_level(WEBKIT_WEB_VIEW(tab->webview));
        char buf[12]; snprintf(buf, sizeof(buf), "%.0f%%", z * 100.0);
        gtk_button_set_label(GTK_BUTTON(zoom_reset_btn_), buf);
    }

    const char* title = webkit_web_view_get_title(WEBKIT_WEB_VIEW(tab->webview));
    std::string wt;
    if (!title || std::string(title).empty() || std::string(title) == kAppName) {
        wt = kAppName;
    } else {
        wt = std::string(title) + " — " + kAppName;
    }
    gtk_window_set_title(GTK_WINDOW(window_), wt.c_str());
}

void BrowserWindow::UpdateNavButtons() {
    if (!active_tab_) return;
    auto* wv = WEBKIT_WEB_VIEW(active_tab_->webview);
    gtk_widget_set_sensitive(back_btn_, webkit_web_view_can_go_back(wv));
    gtk_widget_set_sensitive(fwd_btn_,  webkit_web_view_can_go_forward(wv));
}

void BrowserWindow::UpdateUrlEntry() {
    if (!active_tab_) return;
    const char* uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(active_tab_->webview));
    // Ana sayfa URI'sini gizle
    if (!uri || active_tab_->url == kHomePage || g_str_has_prefix(uri, "about:"))
        gtk_editable_set_text(GTK_EDITABLE(url_entry_), "");
    else
        gtk_editable_set_text(GTK_EDITABLE(url_entry_), uri);
}

void BrowserWindow::UpdateFavButton() {
    if (!fav_btn_ || !active_tab_) return;
    bool bookmarked = BookmarkManager::Get().IsBookmarked(active_tab_->url);
    gtk_button_set_icon_name(GTK_BUTTON(fav_btn_),
        bookmarked ? "starred-symbolic" : "non-starred-symbolic");
    if (bookmarked) {
        gtk_widget_add_css_class(fav_btn_, "bookmarked");
    } else {
        gtk_widget_remove_css_class(fav_btn_, "bookmarked");
    }
}

void BrowserWindow::ShowUrlSuggestions(const std::string& text) {
    if (text.size() < 2) { HideUrlSuggestions(); return; }

    // Geçmiş + bookmark birleştir (URL, başlık, ikon)
    struct SuggestEntry { std::string url, title; bool is_bookmark; };
    std::vector<SuggestEntry> entries;

    // Önce bookmark'lardan eşleşenleri ekle (max 4)
    const std::string tl = [&]{ std::string s=text; for(auto& c:s) c=tolower((unsigned char)c); return s; }();
    for (const auto& bm : BookmarkManager::Get().All()) {
        std::string ul = bm.url, ttl = bm.title;
        for (auto& c : ul) c = tolower((unsigned char)c);
        for (auto& c : ttl) c = tolower((unsigned char)c);
        if (ul.find(tl) != std::string::npos || ttl.find(tl) != std::string::npos)
            entries.push_back({bm.url, bm.title, true});
        if (entries.size() >= 4) break;
    }

    // Sonra geçmişten ekle (max 8 toplam)
    auto hist = HistoryManager::Get().Query(text, 8);
    for (const auto& e : hist) {
        bool dup = false;
        for (const auto& ex : entries) if (ex.url == e.url) { dup = true; break; }
        if (!dup) entries.push_back({e.url, e.title, false});
        if (entries.size() >= 8) break;
    }

    if (entries.empty()) { HideUrlSuggestions(); return; }

    if (!suggest_pop_) {
        suggest_pop_ = gtk_popover_new();
        gtk_popover_set_has_arrow(GTK_POPOVER(suggest_pop_), FALSE);
        gtk_popover_set_autohide(GTK_POPOVER(suggest_pop_), TRUE);
        gtk_widget_set_parent(suggest_pop_, url_entry_);

        suggest_list_ = gtk_list_box_new();
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(suggest_list_), GTK_SELECTION_BROWSE);
        gtk_widget_set_size_request(suggest_list_, 520, -1);

        GtkWidget* scroll = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
            GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_widget_set_size_request(scroll, -1, 320);
        gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), suggest_list_);
        gtk_popover_set_child(GTK_POPOVER(suggest_pop_), scroll);

        g_signal_connect(suggest_list_, "row-activated",
            G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer ud) {
                auto* self = static_cast<BrowserWindow*>(ud);
                const char* url = static_cast<const char*>(
                    g_object_get_data(G_OBJECT(row), "url"));
                if (url && *url && self->active_tab_) {
                    gtk_editable_set_text(GTK_EDITABLE(self->url_entry_), url);
                    webkit_web_view_load_uri(
                        WEBKIT_WEB_VIEW(self->active_tab_->webview), url);
                    self->HideUrlSuggestions();
                }
            }), this);
    }

    // Listeyi temizle
    GtkWidget* child = gtk_widget_get_first_child(suggest_list_);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(suggest_list_), child);
        child = next;
    }

    for (const auto& e : entries) {
        GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(row_box, 8);
        gtk_widget_set_margin_end(row_box, 8);
        gtk_widget_set_margin_top(row_box, 5);
        gtk_widget_set_margin_bottom(row_box, 5);

        // İkon: bookmark yıldız, geçmiş saat
        GtkWidget* icon = gtk_image_new_from_icon_name(
            e.is_bookmark ? "starred-symbolic" : "document-open-recent-symbolic");
        gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(row_box), icon);

        GtkWidget* text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
        gtk_widget_set_hexpand(text_box, TRUE);

        if (!e.title.empty()) {
            GtkWidget* title_lbl = gtk_label_new(e.title.c_str());
            gtk_label_set_ellipsize(GTK_LABEL(title_lbl), PANGO_ELLIPSIZE_END);
            gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
            gtk_box_append(GTK_BOX(text_box), title_lbl);
        }

        GtkWidget* url_lbl = gtk_label_new(e.url.c_str());
        gtk_label_set_ellipsize(GTK_LABEL(url_lbl), PANGO_ELLIPSIZE_MIDDLE);
        gtk_widget_set_halign(url_lbl, GTK_ALIGN_START);
        gtk_widget_add_css_class(url_lbl, "dim-label");
        gtk_box_append(GTK_BOX(text_box), url_lbl);

        gtk_box_append(GTK_BOX(row_box), text_box);

        GtkWidget* row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
        g_object_set_data_full(G_OBJECT(row), "url", g_strdup(e.url.c_str()), g_free);
        gtk_list_box_append(GTK_LIST_BOX(suggest_list_), row);
    }

    gtk_popover_popup(GTK_POPOVER(suggest_pop_));
}

void BrowserWindow::HideUrlSuggestions() {
    if (suggest_pop_)
        gtk_popover_popdown(GTK_POPOVER(suggest_pop_));
}

Tab* BrowserWindow::TabForWebView(WebKitWebView* wv) {
    return static_cast<Tab*>(g_object_get_data(G_OBJECT(wv), "tab"));
}

Tab* BrowserWindow::ActiveTab() { return active_tab_; }

// ── Event handlers ───────────────────────────────────────────────────────────

void BrowserWindow::OnLoadChanged(WebKitWebView* wv, WebKitLoadEvent event) {
    Tab* tab = TabForWebView(wv);
    if (!tab) return;

    bool loading = (event == WEBKIT_LOAD_STARTED || event == WEBKIT_LOAD_COMMITTED);

    if (tab == active_tab_) {
        gtk_button_set_icon_name(GTK_BUTTON(reload_btn_),
            loading ? "process-stop-symbolic" : "view-refresh-symbolic");
        UpdateNavButtons();
    }

    // Favicon yükleme bittikten sonra tekrar dene (ilk notify::favicon null dönebilir)
    if (event == WEBKIT_LOAD_FINISHED) {
        OnFaviconChanged(wv);
        // Geçmişe kaydet
        const char* uri   = webkit_web_view_get_uri(wv);
        const char* title = webkit_web_view_get_title(wv);
        if (uri && *uri &&
            strncmp(uri, "ferman://", 9) != 0 &&
            strncmp(uri, "about:",   6) != 0) {
            HistoryManager::Get().AddVisit(
                uri, title ? title : "");
        }
        // Favori butonunu güncelle
        UpdateFavButton();
    }
}

void BrowserWindow::OnUriChanged(WebKitWebView* wv) {
    Tab* tab = TabForWebView(wv);
    if (!tab) return;
    const char* uri = webkit_web_view_get_uri(wv);
    tab->url = uri ? uri : "";
    UpdateTabLabel(tab);
    if (tab == active_tab_) {
        UpdateUrlEntry();
        UpdateFavButton();
    }
}

void BrowserWindow::UpdateTabLabel(Tab* tab) {
    if (!tab || !tab->label) return;

    // Sadece başlık (id ayrı etiket, favicon ayrı widget)
    std::string display;
    if (!tab->title.empty()) {
        display = tab->title;
    } else if (!tab->url.empty() && tab->url != kHomePage) {
        std::string u = tab->url;
        auto s = u.find("://");
        if (s != std::string::npos) u = u.substr(s + 3);
        auto slash = u.find('/');
        if (slash != std::string::npos) u = u.substr(0, slash);
        if (u.size() > 4 && u.substr(0, 4) == "www.") u = u.substr(4);
        display = u;
    } else {
        display = "Yeni Sekme";
    }
    gtk_label_set_text(GTK_LABEL(tab->label), display.c_str());
}

void BrowserWindow::OnTitleChanged(WebKitWebView* wv) {
    Tab* tab = TabForWebView(wv);
    if (!tab) return;
    const char* title = webkit_web_view_get_title(wv);
    tab->title = title ? title : "";
    UpdateTabLabel(tab);

    if (tab == active_tab_) {
        std::string wt;
        if (tab->title.empty() || tab->title == kAppName) {
            wt = kAppName;
        } else {
            wt = tab->title + " — " + kAppName;
        }
        gtk_window_set_title(GTK_WINDOW(window_), wt.c_str());
    }
}

void BrowserWindow::OnFaviconChanged(WebKitWebView* wv) {
    Tab* tab = TabForWebView(wv);
    if (!tab || !tab->favicon) return;

    // Önce sync API'yi dene (cache'den gelir)
    GdkTexture* texture = webkit_web_view_get_favicon(wv);
    if (texture) {
        gtk_image_set_from_paintable(GTK_IMAGE(tab->favicon), GDK_PAINTABLE(texture));
        gtk_image_set_pixel_size(GTK_IMAGE(tab->favicon), 16);
        gtk_widget_set_visible(tab->favicon, TRUE);
        return;
    }

    // Sync null — FaviconDatabase ile async sorgula
    const char* page_uri = webkit_web_view_get_uri(wv);
    if (!page_uri || !*page_uri) {
        gtk_widget_set_visible(tab->favicon, FALSE);
        return;
    }

    WebKitNetworkSession* ns = SessionManager::Get().GetSession();
    WebKitWebsiteDataManager* dm = webkit_network_session_get_website_data_manager(ns);
    WebKitFaviconDatabase* fdb = webkit_website_data_manager_get_favicon_database(dm);
    if (!fdb) { gtk_widget_set_visible(tab->favicon, FALSE); return; }

    struct Ctx { GtkWidget* img; };
    auto* ctx = new Ctx{ tab->favicon };
    webkit_favicon_database_get_favicon(fdb, page_uri, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer ud) {
            auto* ctx = static_cast<Ctx*>(ud);
            GError* err = nullptr;
            GdkTexture* tex = webkit_favicon_database_get_favicon_finish(
                WEBKIT_FAVICON_DATABASE(src), res, &err);
            if (GTK_IS_IMAGE(ctx->img)) {
                if (tex) {
                    gtk_image_set_from_paintable(GTK_IMAGE(ctx->img), GDK_PAINTABLE(tex));
                    gtk_image_set_pixel_size(GTK_IMAGE(ctx->img), 16);
                    gtk_widget_set_visible(ctx->img, TRUE);
                    g_object_unref(tex);
                } else {
                    // favicon yok — gizle
                    gtk_widget_set_visible(ctx->img, FALSE);
                }
            }
            if (err) g_error_free(err);
            delete ctx;
        }, ctx);
}

void BrowserWindow::OnMouseTargetChanged(WebKitWebView* wv, WebKitHitTestResult* hit) {
    // Sadece aktif sekmenin hover bilgisini göster
    Tab* tab = TabForWebView(wv);
    if (!tab || tab != active_tab_) return;

    const char* link_uri = webkit_hit_test_result_get_link_uri(hit);
    if (status_bar_) {
        if (link_uri && *link_uri) {
            gtk_label_set_text(GTK_LABEL(status_bar_), link_uri);
            gtk_widget_set_visible(status_bar_, TRUE);
        } else {
            gtk_widget_set_visible(status_bar_, FALSE);
            gtk_label_set_text(GTK_LABEL(status_bar_), "");
        }
    }
    // DNS prefetch: hover'da hedef host'u önceden çöz
    if (link_uri && *link_uri) {
        const char* after_scheme = strstr(link_uri, "://");
        if (after_scheme) {
            after_scheme += 3;
            std::string host;
            while (*after_scheme && *after_scheme != '/' &&
                   *after_scheme != ':' && *after_scheme != '?')
                host += *after_scheme++;
            if (!host.empty())
                webkit_network_session_prefetch_dns(
                    SessionManager::Get().GetSession(), host.c_str());
        }
    }
}

void BrowserWindow::OnUrlActivate() {
    if (!active_tab_) return;
    std::string input = gtk_editable_get_text(GTK_EDITABLE(url_entry_));
    if (input.empty()) return;

    std::string url;
    // Arama sorgusu mu, URL mi?
    // Kural: boşluk yok + (protokol var VEYA www. ile başlıyor VEYA .com/.net/.org/.io vb. içeriyor)
    bool has_proto = (input.find("://") != std::string::npos);
    bool has_www   = (input.rfind("www.", 0) == 0);  // "www." ile başlıyor
    bool has_dot   = (input.find('.') != std::string::npos);
    bool has_space = (input.find(' ')  != std::string::npos);
    bool looks_like_url = !has_space && (has_proto || has_www || has_dot);

    if (looks_like_url) {
        url = has_proto ? input : "https://" + input;
    } else {
        // Google arama
        std::string enc;
        for (unsigned char c : input) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                enc += c;
            } else {
                char buf[4];
                snprintf(buf, sizeof(buf), "%%%02X", c);
                enc += buf;
            }
        }
        const std::string& se = SettingsManager::Get().Prefs().search_engine;
        url = SearchEngineUrl(se) + enc;
    }
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(active_tab_->webview), url.c_str());
}

void BrowserWindow::OnBack() {
    if (active_tab_) webkit_web_view_go_back(WEBKIT_WEB_VIEW(active_tab_->webview));
}

void BrowserWindow::OnForward() {
    if (active_tab_) webkit_web_view_go_forward(WEBKIT_WEB_VIEW(active_tab_->webview));
}

void BrowserWindow::OnReload() {
    if (!active_tab_) return;
    auto* wv = WEBKIT_WEB_VIEW(active_tab_->webview);
    if (webkit_web_view_is_loading(wv))
        webkit_web_view_stop_loading(wv);
    else
        webkit_web_view_reload(wv);
}

// ── Static callbacks ─────────────────────────────────────────────────────────

void BrowserWindow::OnLoadChangedCb(WebKitWebView* wv, WebKitLoadEvent ev, gpointer ud) {
    static_cast<BrowserWindow*>(ud)->OnLoadChanged(wv, ev);
}
void BrowserWindow::OnUriChangedCb(WebKitWebView* wv, GParamSpec*, gpointer ud) {
    static_cast<BrowserWindow*>(ud)->OnUriChanged(wv);
}
void BrowserWindow::OnTitleChangedCb(WebKitWebView* wv, GParamSpec*, gpointer ud) {
    static_cast<BrowserWindow*>(ud)->OnTitleChanged(wv);
}
void BrowserWindow::OnBackCb(GtkButton*, gpointer ud) {
    static_cast<BrowserWindow*>(ud)->OnBack();
}
void BrowserWindow::OnForwardCb(GtkButton*, gpointer ud) {
    static_cast<BrowserWindow*>(ud)->OnForward();
}
void BrowserWindow::OnReloadCb(GtkButton*, gpointer ud) {
    static_cast<BrowserWindow*>(ud)->OnReload();
}
void BrowserWindow::OnUrlActivateCb(GtkEntry*, gpointer ud) {
    static_cast<BrowserWindow*>(ud)->OnUrlActivate();
}
void BrowserWindow::OnNewTabCb(GtkButton*, gpointer ud) {
    auto* self = static_cast<BrowserWindow*>(ud);
    const auto& hp = SettingsManager::Get().Prefs().homepage;
    self->NewTab(hp.empty() ? self->kHomePage : hp);
}
void BrowserWindow::OnClearUrlCb(GtkButton*, gpointer ud) {
    auto* self = static_cast<BrowserWindow*>(ud);
    gtk_editable_set_text(GTK_EDITABLE(self->url_entry_), "");
    gtk_widget_grab_focus(self->url_entry_);
}
void BrowserWindow::OnCopyUrlCb(GtkButton*, gpointer ud) {
    auto* self = static_cast<BrowserWindow*>(ud);
    const char* txt = gtk_editable_get_text(GTK_EDITABLE(self->url_entry_));
    if (txt && *txt) {
        GdkClipboard* clip = gtk_widget_get_clipboard(self->url_entry_);
        gdk_clipboard_set_text(clip, txt);
    }
}
void BrowserWindow::OnFaviconChangedCb(WebKitWebView* wv, GParamSpec*, gpointer ud) {
    static_cast<BrowserWindow*>(ud)->OnFaviconChanged(wv);
}
void BrowserWindow::OnMenuActionWithParamCb(GSimpleAction* action, GVariant* param, gpointer ud) {
    auto* self = static_cast<BrowserWindow*>(ud);
    const char* name = g_action_get_name(G_ACTION(action));
    if (!g_strcmp0(name, "open-new-window") && param) {
        const char* uri = g_variant_get_string(param, nullptr);
        if (uri && *uri) self->OpenInNewWindow(uri);
    }
}
void BrowserWindow::OnMenuActionCb(GSimpleAction* action, GVariant*, gpointer ud) {
    auto* self = static_cast<BrowserWindow*>(ud);
    const char* name = g_action_get_name(G_ACTION(action));
    if      (!g_strcmp0(name, "new-tab")) {
        const auto& hp = SettingsManager::Get().Prefs().homepage;
        self->NewTab(hp.empty() ? self->kHomePage : hp);
    }
    else if (!g_strcmp0(name, "new-window")) {
        const auto& hp = SettingsManager::Get().Prefs().homepage;
        self->OpenInNewWindow(hp.empty() ? self->kHomePage : hp);
    }
    else if (!g_strcmp0(name, "close-tab"))     { if (self->active_tab_) self->CloseTab(self->active_tab_); }
    else if (!g_strcmp0(name, "go-home")) {
        if (self->active_tab_) {
            const auto& hp = SettingsManager::Get().Prefs().homepage;
            std::string url = hp.empty() ? self->kHomePage : hp;
            if (url == self->kHomePage) {
                std::string html = self->BuildHomeHTML();
                webkit_web_view_load_html(WEBKIT_WEB_VIEW(self->active_tab_->webview), html.c_str(), nullptr);
            } else {
                if (url.find("://") == std::string::npos) url = "https://" + url;
                webkit_web_view_load_uri(WEBKIT_WEB_VIEW(self->active_tab_->webview), url.c_str());
            }
        }
    }
    else if (!g_strcmp0(name, "reload"))        self->OnReload();
    else if (!g_strcmp0(name, "go-back"))       self->OnBack();
    else if (!g_strcmp0(name, "go-forward"))    self->OnForward();
    else if (!g_strcmp0(name, "zoom-in")) {
        if (self->active_tab_) {
            auto* wv = WEBKIT_WEB_VIEW(self->active_tab_->webview);
            double z = std::min(webkit_web_view_get_zoom_level(wv) + 0.1, 5.0);
            webkit_web_view_set_zoom_level(wv, z);
            if (self->zoom_reset_btn_) {
                char buf[12]; snprintf(buf, sizeof(buf), "%.0f%%", z * 100.0);
                gtk_button_set_label(GTK_BUTTON(self->zoom_reset_btn_), buf);
            }
        }
    }
    else if (!g_strcmp0(name, "zoom-out")) {
        if (self->active_tab_) {
            auto* wv = WEBKIT_WEB_VIEW(self->active_tab_->webview);
            double z = std::max(webkit_web_view_get_zoom_level(wv) - 0.1, 0.25);
            webkit_web_view_set_zoom_level(wv, z);
            if (self->zoom_reset_btn_) {
                char buf[12]; snprintf(buf, sizeof(buf), "%.0f%%", z * 100.0);
                gtk_button_set_label(GTK_BUTTON(self->zoom_reset_btn_), buf);
            }
        }
    }
    else if (!g_strcmp0(name, "zoom-reset")) {
        if (self->active_tab_) {
            webkit_web_view_set_zoom_level(
                WEBKIT_WEB_VIEW(self->active_tab_->webview), 1.0);
            if (self->zoom_reset_btn_)
                gtk_button_set_label(GTK_BUTTON(self->zoom_reset_btn_), "100%");
        }
    }
    else if (!g_strcmp0(name, "show-history"))
        self->NewTab("ferman://gecmis");
    else if (!g_strcmp0(name, "show-downloads"))
        DownloadManager::Get().ShowPanel();
    else if (!g_strcmp0(name, "toggle-bookmarks-bar"))
        self->ToggleBookmarksBar();
    else if (!g_strcmp0(name, "find-in-page"))
        self->ShowFindBar();
    else if (!g_strcmp0(name, "clear-history")) {
        GtkAlertDialog* dlg = gtk_alert_dialog_new("Geçmişi temizle");
        gtk_alert_dialog_set_detail(dlg, "Tarama geçmişi ve önbellek temizlensin mi? Oturum bilgileri (çerezler) korunur.");
        const char* btns[] = { "İptal", "Temizle", nullptr };
        gtk_alert_dialog_set_buttons(dlg, btns);
        gtk_alert_dialog_set_cancel_button(dlg, 0);
        gtk_alert_dialog_set_default_button(dlg, 1);
        gtk_alert_dialog_choose(dlg, GTK_WINDOW(self->window_), nullptr,
            [](GObject* src, GAsyncResult* res, gpointer) {
                int btn = gtk_alert_dialog_choose_finish(
                    GTK_ALERT_DIALOG(src), res, nullptr);
                if (btn == 1) {
                    WebKitNetworkSession* ns = SessionManager::Get().GetSession();
                    WebKitWebsiteDataManager* dm =
                        webkit_network_session_get_website_data_manager(ns);
                    webkit_website_data_manager_clear(dm,
                        static_cast<WebKitWebsiteDataTypes>(
                            WEBKIT_WEBSITE_DATA_MEMORY_CACHE |
                            WEBKIT_WEBSITE_DATA_DISK_CACHE),
                        0, nullptr, nullptr, nullptr);
                    HistoryManager::Get().Clear();
                }
                g_object_unref(src);
            }, nullptr);
        g_object_unref(dlg);
    }
    else if (!g_strcmp0(name, "print-page")) {
        if (self->active_tab_) {
            WebKitPrintOperation* op = webkit_print_operation_new(
                WEBKIT_WEB_VIEW(self->active_tab_->webview));
            webkit_print_operation_run_dialog(op, GTK_WINDOW(self->window_));
            g_object_unref(op);
        }
    }
    else if (!g_strcmp0(name, "save-page")) {
        if (self->active_tab_) {
            webkit_web_view_save(
                WEBKIT_WEB_VIEW(self->active_tab_->webview),
                WEBKIT_SAVE_MODE_MHTML, nullptr,
                [](GObject* src, GAsyncResult* res, gpointer ud) {
                    GError* err = nullptr;
                    GInputStream* is = webkit_web_view_save_finish(
                        WEBKIT_WEB_VIEW(src), res, &err);
                    if (is) {
                        GtkWindow* win = GTK_WINDOW(ud);
                        GtkFileDialog* fd = gtk_file_dialog_new();
                        gtk_file_dialog_set_title(fd, "Sayfayı Kaydet");
                        GtkFileFilter* ff = gtk_file_filter_new();
                        gtk_file_filter_add_suffix(ff, "mhtml");
                        gtk_file_filter_set_name(ff, "Web Arşivi (*.mhtml)");
                        GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
                        g_list_store_append(filters, ff);
                        gtk_file_dialog_set_filters(fd, G_LIST_MODEL(filters));
                        g_object_unref(filters);
                        g_object_unref(ff);
                        gtk_file_dialog_save(fd, win, nullptr,
                            [](GObject* fdobj, GAsyncResult* r, gpointer isp) {
                                GError* e = nullptr;
                                GFile* file = gtk_file_dialog_save_finish(
                                    GTK_FILE_DIALOG(fdobj), r, &e);
                                GInputStream* stream = static_cast<GInputStream*>(isp);
                                if (file) {
                                    GFileOutputStream* fos = g_file_replace(
                                        file, nullptr, FALSE,
                                        G_FILE_CREATE_REPLACE_DESTINATION,
                                        nullptr, nullptr);
                                    if (fos) {
                                        g_output_stream_splice(
                                            G_OUTPUT_STREAM(fos), stream,
                                            static_cast<GOutputStreamSpliceFlags>(
                                                G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
                                                G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET),
                                            nullptr, nullptr);
                                        g_object_unref(fos);
                                    }
                                    g_object_unref(file);
                                }
                                g_object_unref(stream);
                                if (e) g_error_free(e);
                            }, is);
                        g_object_unref(fd);
                    }
                    if (err) g_error_free(err);
                }, self->window_);
        }
    }
    else if (!g_strcmp0(name, "fullscreen")) {
        if (gtk_window_is_fullscreen(GTK_WINDOW(self->window_)))
            gtk_window_unfullscreen(GTK_WINDOW(self->window_));
        else
            gtk_window_fullscreen(GTK_WINDOW(self->window_));
    }
    else if (!g_strcmp0(name, "settings"))  self->NewTab("ferman://ayarlar");
    else if (!g_strcmp0(name, "about"))     self->NewTab("ferman://ayarlar/hakkimizda");
}

// ── Yeni pencere aç ─────────────────────────────────────────────────────────

void BrowserWindow::OpenInNewWindow(const std::string& url) {
    new BrowserWindow(app_);
    // Yeni penceredeki ilk tab URL'yi zaten kHomePage ile açıyor.
    // URL farklıysa: yeni pencereyi bulduktan sonra navigate et.
    // Basit yol: uygulamanın son eklenen penceresine eriş.
    GList* wins = gtk_application_get_windows(app_);
    if (!wins) return;
    GtkWindow* new_win = GTK_WINDOW(wins->data);  // en son eklenen başta
    // Yeni pencerenin child'ı BrowserWindow's stack — URL'yi yükle
    // BrowserWindow constructor'ı NewTab(kHomePage) çağırıyor.
    // Eğer hedef URL farklıysa pencereye mesaj gönder:
    if (url != kHomePage) {
        auto* bw = static_cast<BrowserWindow*>(
            g_object_get_data(G_OBJECT(new_win), "browser-window"));
        if (bw && !bw->tabs_.empty()) {
            Tab* t = bw->tabs_.back();
            if (t) {
                std::string load_url = url;
                if (load_url.find("://") == std::string::npos)
                    load_url = "https://" + load_url;
                webkit_web_view_load_uri(WEBKIT_WEB_VIEW(t->webview), load_url.c_str());
                t->url = load_url;
            }
        }
    }
}

// ── Karar politikası: yeni pencere/sekme yönlendirmesi ───────────────────────

bool BrowserWindow::OnDecidePolicy(WebKitWebView* wv,
                                    WebKitPolicyDecision* dec,
                                    WebKitPolicyDecisionType type) {
    // NEW_WINDOW_ACTION: "create" sinyali zaten yeni sekme açıyor;
    if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        webkit_policy_decision_use(dec);
        return true;
    }

    // RESPONSE: İndirilebilir içerik → download'a yönlendir, boş sekme açma
    if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
        WebKitResponsePolicyDecision* resp = WEBKIT_RESPONSE_POLICY_DECISION(dec);
        if (!webkit_response_policy_decision_is_mime_type_supported(resp)) {
            webkit_policy_decision_download(dec);
            return true;
        }
        return false;
    }

    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) return false;

    // ferman:// dahili sayfaları intercept et
    {
        WebKitNavigationPolicyDecision* nd = WEBKIT_NAVIGATION_POLICY_DECISION(dec);
        WebKitNavigationAction* na = webkit_navigation_policy_decision_get_navigation_action(nd);
        WebKitURIRequest* req = webkit_navigation_action_get_request(na);
        const char* req_uri = webkit_uri_request_get_uri(req);
        if (req_uri && g_str_has_prefix(req_uri, "ferman://") &&
            !g_str_has_prefix(req_uri, "ferman://home")) {
            webkit_policy_decision_ignore(dec);
            // idle'da çağır: tab yaratıldıktan sonra wv active_tab ile eşleşsin
            struct Ctx { BrowserWindow* self; std::string uri; };
            auto* ctx = new Ctx{ this, req_uri };
            g_idle_add([](gpointer ud) -> gboolean {
                auto* c = static_cast<Ctx*>(ud);
                // TabForWebView ile doğru tabı bul
                c->self->HandlefermanScheme(c->uri);
                delete c;
                return G_SOURCE_REMOVE;
            }, ctx);
            return true;
        }
    }

    WebKitNavigationPolicyDecision* navdec = WEBKIT_NAVIGATION_POLICY_DECISION(dec);
    WebKitNavigationAction* action =
        webkit_navigation_policy_decision_get_navigation_action(navdec);

    // Orta-tık veya Ctrl+tık → yeni sekmede aç
    guint btn  = webkit_navigation_action_get_mouse_button(action);
    guint mods = webkit_navigation_action_get_modifiers(action);
    bool mid_click  = (btn == GDK_BUTTON_MIDDLE);
    bool ctrl_click = (mods & GDK_CONTROL_MASK) != 0;

    if (mid_click || ctrl_click) {
        WebKitURIRequest* req = webkit_navigation_action_get_request(action);
        const char* uri = webkit_uri_request_get_uri(req);
        if (uri && *uri) {
            NewTab(uri);
            webkit_policy_decision_ignore(dec);
            return true;
        }
    }
    return false;
}

// ── Bağlam menüsu: sağ tık ─────────────────────────────────────────────

bool BrowserWindow::OnContextMenu(WebKitWebView* wv,
                                   WebKitContextMenu* menu,
                                   WebKitHitTestResult* hit) {
    // Kaldırılacak stock öğeler: tekrarlananlar veya istemediğimiz eylemler
    static const WebKitContextMenuAction kRemove[] = {
        WEBKIT_CONTEXT_MENU_ACTION_OPEN_LINK_IN_NEW_WINDOW,
        WEBKIT_CONTEXT_MENU_ACTION_OPEN_IMAGE_IN_NEW_WINDOW,
        WEBKIT_CONTEXT_MENU_ACTION_OPEN_FRAME_IN_NEW_WINDOW,
        WEBKIT_CONTEXT_MENU_ACTION_OPEN_VIDEO_IN_NEW_WINDOW,
        WEBKIT_CONTEXT_MENU_ACTION_OPEN_AUDIO_IN_NEW_WINDOW,
    };
    {
        GList* items = webkit_context_menu_get_items(menu);
        GList* to_rm = nullptr;
        for (GList* l = items; l; l = l->next) {
            auto* it = static_cast<WebKitContextMenuItem*>(l->data);
            WebKitContextMenuAction act = webkit_context_menu_item_get_stock_action(it);
            for (auto bad : kRemove)
                if (act == bad) { to_rm = g_list_prepend(to_rm, it); break; }
        }
        for (GList* l = to_rm; l; l = l->next)
            webkit_context_menu_remove(menu, static_cast<WebKitContextMenuItem*>(l->data));
        g_list_free(to_rm);
    }

    // ── Bağlantı üzerindeyse: Yeni Sekmede / Yeni Pencerede Aç ──────────────
    if (webkit_hit_test_result_context_is_link(hit)) {
        const char* link_uri = webkit_hit_test_result_get_link_uri(hit);
        if (link_uri && *link_uri) {
            if (webkit_context_menu_get_n_items(menu) > 0)
                webkit_context_menu_append(menu, webkit_context_menu_item_new_separator());

            GSimpleAction* act_tab = g_simple_action_new("ctx-open-tab", G_VARIANT_TYPE_STRING);
            g_signal_connect(act_tab, "activate",
                G_CALLBACK(+[](GSimpleAction*, GVariant* p, gpointer ud) {
                    const char* u = g_variant_get_string(p, nullptr);
                    if (u && *u) static_cast<BrowserWindow*>(ud)->NewTab(u);
                }), this);
            webkit_context_menu_append(menu,
                webkit_context_menu_item_new_from_gaction(
                    G_ACTION(act_tab), "Yeni Sekmede Aç",
                    g_variant_new_string(link_uri)));
            g_object_unref(act_tab);

            GSimpleAction* act_win = g_simple_action_new("ctx-open-win", G_VARIANT_TYPE_STRING);
            g_signal_connect(act_win, "activate",
                G_CALLBACK(+[](GSimpleAction*, GVariant* p, gpointer ud) {
                    const char* u = g_variant_get_string(p, nullptr);
                    if (u && *u) static_cast<BrowserWindow*>(ud)->OpenInNewWindow(u);
                }), this);
            webkit_context_menu_append(menu,
                webkit_context_menu_item_new_from_gaction(
                    G_ACTION(act_win), "Yeni Pencerede Aç",
                    g_variant_new_string(link_uri)));
            g_object_unref(act_win);
        }
    }

    // ── Seçili metin üzerindeyse: Yapay Zeka ile Çevir ───────────────────────
    if (webkit_hit_test_result_context_is_selection(hit)) {
        // Seçili metni JS ile al, AI'ya gönder, popup'ta göster
        if (webkit_context_menu_get_n_items(menu) > 0)
            webkit_context_menu_append(menu, webkit_context_menu_item_new_separator());

        GSimpleAction* act_tr = g_simple_action_new("ctx-ai-translate", nullptr);
        g_signal_connect(act_tr, "activate",
            G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer ud) {
                auto* self = static_cast<BrowserWindow*>(ud);
                if (!self->active_tab_ || !self->active_tab_->webview) return;
                webkit_web_view_evaluate_javascript(
                    WEBKIT_WEB_VIEW(self->active_tab_->webview),
                    "window.getSelection().toString()", -1,
                    nullptr, nullptr, nullptr,
                    [](GObject* obj, GAsyncResult* res, gpointer ud2) {
                        auto* self2 = static_cast<BrowserWindow*>(ud2);
                        GError* err = nullptr;
                        JSCValue* val = webkit_web_view_evaluate_javascript_finish(
                            WEBKIT_WEB_VIEW(obj), res, &err);
                        if (err) { g_error_free(err); return; }
                        char* sel_text = jsc_value_to_string(val);
                        g_object_unref(val);
                        if (!sel_text || sel_text[0] == '\0') { g_free(sel_text); return; }
                        std::string prompt = std::string(
                            "Aşağıdaki metni Türkçe'ye çevir. "
                            "Sadece çeviriyi yaz, başka hiçbir şey ekleme:\n\n") + sel_text;
                        g_free(sel_text);
                        self2->ShowAiQuickPopup("Çeviri", prompt);
                    }, self);
            }), this);
        webkit_context_menu_append(menu,
            webkit_context_menu_item_new_from_gaction(
                G_ACTION(act_tr), "Yapay Zeka ile Çevir", nullptr));
        g_object_unref(act_tr);
    }

    // ── Her durumda: Sayfayı Özetle ──────────────────────────────────────────
    if (webkit_context_menu_get_n_items(menu) > 0)
        webkit_context_menu_append(menu, webkit_context_menu_item_new_separator());

    GSimpleAction* act_sum = g_simple_action_new("ctx-ai-summarize", nullptr);
    g_signal_connect(act_sum, "activate",
        G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer ud) {
            auto* self = static_cast<BrowserWindow*>(ud);
            if (!self->active_tab_ || !self->active_tab_->webview) return;
            // Reklamları ve gereksiz öğeleri soyarak sayfa metnini al
            const char* js =
                "(function(){"
                "  var sel=['script','style','nav','header','footer',"
                "    'aside','iframe','noscript','form','button',"
                "    '[class*=\"ad\"]','[id*=\"ad\"]','[class*=\"banner\"]',"
                "    '[class*=\"cookie\"]','[class*=\"popup\"]','[class*=\"modal\"]'];"
                "  var clone=document.body.cloneNode(true);"
                "  sel.forEach(function(s){"
                "    clone.querySelectorAll(s).forEach(function(e){e.remove();});"
                "  });"
                "  return (clone.innerText||clone.textContent||'').trim().slice(0,6000);"
                "})()";
            webkit_web_view_evaluate_javascript(
                WEBKIT_WEB_VIEW(self->active_tab_->webview),
                js, -1, nullptr, nullptr, nullptr,
                [](GObject* obj, GAsyncResult* res, gpointer ud2) {
                    auto* self2 = static_cast<BrowserWindow*>(ud2);
                    GError* err = nullptr;
                    JSCValue* val = webkit_web_view_evaluate_javascript_finish(
                        WEBKIT_WEB_VIEW(obj), res, &err);
                    if (err) { g_error_free(err); return; }
                    char* txt = jsc_value_to_string(val);
                    g_object_unref(val);
                    if (!txt || txt[0] == '\0') { g_free(txt); return; }
                    std::string prompt = std::string(
                        "Aşağıdaki sayfa içeriğini Türkçe olarak kısa ve öz şekilde özetle. "
                        "Maddeler halinde, en fazla 8 madde:\n\n") + txt;
                    g_free(txt);
                    self2->ShowAiQuickPopup("Sayfa Özeti", prompt);
                }, self);
        }), this);
    webkit_context_menu_append(menu,
        webkit_context_menu_item_new_from_gaction(
            G_ACTION(act_sum), "Sayfayı Özetle (AI)", nullptr));
    g_object_unref(act_sum);

    return false;
}

// ── Yeni pencere isteği: yeni sekme olarak aç ─────────────────────────────

WebKitWebView* BrowserWindow::OnCreateWebView(WebKitWebView* source_wv,
                                               WebKitNavigationAction*) {
    // WebKit create sinyali: dönen view MUTLAKA "related-view" property'si ile
    // kaynak view'e bağlı olmalı — aksi hâlde WindowFeatures std::optional
    // assertion crash yapar (WebCore::WebPage::createNewPage).
    //
    // g_object_new ile related-view set ediyoruz; bu WebKit'in aynı web process
    // içinde yeni bir sayfa açmasını sağlar.
    GtkWidget* new_wv = GTK_WIDGET(
        g_object_new(WEBKIT_TYPE_WEB_VIEW,
                     "related-view", source_wv,
                     nullptr));

    // Yeni view için tam bir Tab kaydı oluştur (UI dahil), yükleme/switch yok.
    // WebKit kendi navigation'ını yapacak; biz sadece UI tarafını hazırlıyoruz.
    auto* tab = new Tab();
    tab->id      = next_tab_id_++;
    tab->url     = kHomePage;
    tab->webview = new_wv;

    // Aynı ayarları uygula
    WebKitSettings* wk_settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(new_wv));
    webkit_settings_set_user_agent(wk_settings,
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36");
    webkit_settings_set_enable_javascript(wk_settings, TRUE);
    webkit_settings_set_enable_media(wk_settings, TRUE);
    webkit_settings_set_javascript_can_open_windows_automatically(wk_settings, TRUE);
    webkit_settings_set_allow_modal_dialogs(wk_settings, TRUE);
    webkit_settings_set_enable_smooth_scrolling(wk_settings, TRUE);
    webkit_settings_set_media_playback_requires_user_gesture(wk_settings, FALSE);
    webkit_settings_set_enable_webgl(wk_settings, TRUE);
    webkit_settings_set_enable_webaudio(wk_settings, TRUE);
    webkit_settings_set_enable_page_cache(wk_settings, TRUE);
    webkit_settings_set_enable_dns_prefetching(wk_settings, TRUE);
    webkit_settings_set_hardware_acceleration_policy(
        wk_settings, WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);

    gtk_widget_set_hexpand(new_wv, TRUE);
    gtk_widget_set_vexpand(new_wv, TRUE);
    g_object_set_data(G_OBJECT(new_wv), "tab", tab);

    g_signal_connect(new_wv, "load-changed",         G_CALLBACK(OnLoadChangedCb),        this);
    g_signal_connect(new_wv, "notify::uri",          G_CALLBACK(OnUriChangedCb),         this);
    g_signal_connect(new_wv, "notify::title",        G_CALLBACK(OnTitleChangedCb),       this);
    g_signal_connect(new_wv, "notify::favicon",      G_CALLBACK(OnFaviconChangedCb),     this);
    g_signal_connect(new_wv, "mouse-target-changed", G_CALLBACK(OnMouseTargetChangedCb), this);
    g_signal_connect(new_wv, "decide-policy",        G_CALLBACK(OnDecidePolicyCb),       this);
    g_signal_connect(new_wv, "context-menu",         G_CALLBACK(OnContextMenuCb),        this);
    g_signal_connect(new_wv, "create",               G_CALLBACK(OnCreateWebViewCb),      this);
    g_signal_connect(new_wv, "print",                G_CALLBACK(OnPrintCb),              this);

    // Stack'e ekle
    std::string page_name = "tab_" + std::to_string(tab->id);
    gtk_stack_add_named(GTK_STACK(stack_), new_wv, page_name.c_str());
    g_object_set_data(G_OBJECT(new_wv), "page_name", g_strdup(page_name.c_str()));

    // Tab UI satırı
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(row, "tab-row");
    gtk_widget_set_margin_start(row, 2); gtk_widget_set_margin_end(row, 2);
    gtk_widget_set_margin_top(row, 0);   gtk_widget_set_margin_bottom(row, 0);
    gtk_widget_set_hexpand(row, TRUE);
    gtk_widget_set_vexpand(row, TRUE);
    gtk_widget_set_valign(row, GTK_ALIGN_FILL);

    std::string id_str2 = "#" + std::to_string(tab->id);
    GtkWidget* id_label2 = gtk_label_new(id_str2.c_str());
    gtk_widget_add_css_class(id_label2, "tab-id-label");
    gtk_widget_set_hexpand(id_label2, FALSE);
    gtk_widget_set_valign(id_label2, GTK_ALIGN_CENTER);

    tab->favicon = gtk_image_new();
    gtk_image_set_pixel_size(GTK_IMAGE(tab->favicon), 16);
    gtk_widget_set_valign(tab->favicon, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(tab->favicon, FALSE);
    gtk_widget_set_visible(tab->favicon, FALSE);

    tab->label = gtk_label_new("Yeni Sekme");
    gtk_label_set_width_chars(GTK_LABEL(tab->label), 1);
    gtk_label_set_max_width_chars(GTK_LABEL(tab->label), 16);
    gtk_label_set_ellipsize(GTK_LABEL(tab->label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(tab->label, TRUE);
    gtk_widget_set_halign(tab->label, GTK_ALIGN_FILL);
    gtk_label_set_xalign(GTK_LABEL(tab->label), 0.0f);

    GtkWidget* close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(close_btn, "flat");
    gtk_widget_add_css_class(close_btn, "circular");
    gtk_widget_set_focus_on_click(close_btn, FALSE);
    gtk_widget_set_hexpand(close_btn, FALSE);
    gtk_widget_set_halign(close_btn, GTK_ALIGN_END);
    gtk_widget_set_tooltip_text(close_btn, "Sekmeyi kapat");
    g_object_set_data(G_OBJECT(close_btn), "tab", tab);
    g_signal_connect(close_btn, "clicked",
        G_CALLBACK(+[](GtkButton* btn, gpointer ud) {
            static_cast<BrowserWindow*>(ud)->CloseTab(
                static_cast<Tab*>(g_object_get_data(G_OBJECT(btn), "tab")));
        }), this);

    gtk_box_append(GTK_BOX(row), id_label2);
    gtk_box_append(GTK_BOX(row), tab->favicon);
    gtk_box_append(GTK_BOX(row), tab->label);
    gtk_box_append(GTK_BOX(row), close_btn);

    g_object_set_data(G_OBJECT(row), "tab",       tab);
    g_object_set_data(G_OBJECT(row), "close_btn", close_btn);

    GtkGesture* gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_PRIMARY);
    gtk_event_controller_set_propagation_phase(
        GTK_EVENT_CONTROLLER(gesture), GTK_PHASE_BUBBLE);
    g_signal_connect(gesture, "pressed",
        G_CALLBACK(+[](GtkGestureClick* g, int, double x, double y, gpointer ud) {
            GtkWidget* row_w   = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
            GtkWidget* close_w = static_cast<GtkWidget*>(
                g_object_get_data(G_OBJECT(row_w), "close_btn"));
            GtkWidget* picked  = gtk_widget_pick(row_w, x, y, GTK_PICK_DEFAULT);
            GtkWidget* ancestor = picked;
            while (ancestor) {
                if (ancestor == close_w) return;
                ancestor = gtk_widget_get_parent(ancestor);
            }
            auto* self = static_cast<BrowserWindow*>(ud);
            auto* t    = static_cast<Tab*>(g_object_get_data(G_OBJECT(row_w), "tab"));
            if (t) self->SwitchToTab(t);
        }), this);
    gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(gesture));

    gtk_box_append(GTK_BOX(tab_box_), row);
    tab->tab_btn = row;
    tabs_.push_back(tab);

    // SwitchToTab'ı create sinyali döndükten sonra yap (idle)
    struct Ctx { BrowserWindow* self; Tab* tab; };
    auto* ctx = new Ctx{ this, tab };
    g_idle_add([](gpointer ud) -> gboolean {
        auto* ctx = static_cast<Ctx*>(ud);
        auto& tabs = ctx->self->tabs_;
        if (std::find(tabs.begin(), tabs.end(), ctx->tab) != tabs.end())
            ctx->self->SwitchToTab(ctx->tab);
        delete ctx;
        return G_SOURCE_REMOVE;
    }, ctx);

    return WEBKIT_WEB_VIEW(new_wv);
}

// ── Yazdır sinyali ──────────────────────────────────────────────────

void BrowserWindow::OnPrint(WebKitWebView*, WebKitPrintOperation* op) {
    webkit_print_operation_run_dialog(op, GTK_WINDOW(window_));
}

// ── Dahili sayfa HTML oluşturucular ──────────────────────────────────

std::string BrowserWindow::BuildHomeHTML() {
    const std::string& se  = SettingsManager::Get().Prefs().search_engine;
    std::string search_url = SearchEngineUrl(se);

    // Arama motoru görünen adı
    std::string se_name = "Google";
    if      (se == "bing")       se_name = "Bing";
    else if (se == "yahoo")      se_name = "Yahoo";
    else if (se == "yandex")     se_name = "Yandex";
    else if (se == "duckduckgo") se_name = "DuckDuckGo";
    else if (se == "baidu")      se_name = "Baidu";

    std::string placeholder = se_name + "'da ara veya adres gir...";

    return R"html(<!DOCTYPE html><html lang="tr"><head><meta charset="UTF-8">
<title>Ferman Browser</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
background:linear-gradient(135deg,#1a1a2e 0%,#16213e 50%,#0f3460 100%);
min-height:100vh;display:flex;flex-direction:column;align-items:center;
justify-content:center;color:#e0e0e0}
.logo{font-size:2.8rem;font-weight:700;margin-bottom:8px;
background:linear-gradient(90deg,#e94560,#53a8ff);
-webkit-background-clip:text;-webkit-text-fill-color:transparent}
.tagline{font-size:.9rem;color:rgba(255,255,255,.4);margin-bottom:36px}
.search-box{display:flex;align-items:center;
background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.15);
border-radius:50px;padding:0 20px;width:540px;max-width:90vw;transition:all .2s}
.search-box:focus-within{background:rgba(255,255,255,.13);
border-color:rgba(233,69,96,.6);box-shadow:0 0 0 3px rgba(233,69,96,.15)}
.si{font-size:1.1rem;color:rgba(255,255,255,.4);margin-right:12px;flex-shrink:0}
#q{flex:1;background:transparent;border:none;outline:none;color:#fff;
font-size:1rem;padding:15px 0;caret-color:#e94560}
#q::placeholder{color:rgba(255,255,255,.35)}
.btn{background:#e94560;border:none;border-radius:50px;color:#fff;
font-size:.85rem;font-weight:600;padding:8px 18px;cursor:pointer;
margin-left:8px;transition:background .15s;flex-shrink:0}
.btn:hover{background:#c73652}
.sc{display:flex;gap:18px;margin-top:44px;flex-wrap:wrap;justify-content:center}
.sc a{display:flex;flex-direction:column;align-items:center;gap:7px;
text-decoration:none;color:rgba(255,255,255,.65);transition:transform .15s}
.sc a:hover{transform:translateY(-3px);color:#fff}
.ic{width:52px;height:52px;background:rgba(255,255,255,.08);
border-radius:14px;display:flex;align-items:center;justify-content:center;
font-size:1.4rem;border:1px solid rgba(255,255,255,.1)}
.sc span{font-size:.75rem}
</style></head><body>
<div class="logo">Ferman Browser</div>
<div class="tagline">H&#305;zl&#305;, hafif, &#246;zg&#252;r</div>
<form class="search-box" onsubmit="doSearch(event)">
<span class="si">&#128269;</span>
<input id="q" type="text" placeholder=")html" + placeholder + R"html(" autocomplete="off" autofocus>
<button class="btn" type="submit">Ara</button>
</form>
<div class="sc">
<a href="https://google.com"><div class="ic">&#9654;</div><span>Google</span></a>
<a href="https://github.com"><div class="ic">&#128025;</div><span>GitHub</span></a>
<a href="https://youtube.com"><div class="ic">&#128309;</div><span>Youtube</span></a>
<a href="https://wikipedia.org"><div class="ic">&#128214;</div><span>Wikipedia</span></a>
</div>
<script>
var searchUrl=')html" + search_url + R"html(';
function doSearch(e){e.preventDefault();
var q=document.getElementById('q').value.trim();if(!q)return;
if(/^https?:\/\//i.test(q)||/^[a-z0-9-]+\.[a-z]{2,}/i.test(q))
  window.location.href=q.startsWith('http')?q:'https://'+q;
else window.location.href=searchUrl+encodeURIComponent(q);}
</script></body></html>)html";
}

// Ortak ayarlar sidebar CSS + wrapper
static std::string SettingsSidebarCSS() {
    return R"css(
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
background:#f0f2f5;color:#1a1a1a;min-height:100vh;display:flex}
.sidebar{width:220px;background:#1e1e2e;color:#cdd6f4;padding:0;flex-shrink:0;
display:flex;flex-direction:column;box-shadow:2px 0 8px rgba(0,0,0,.15)}
.sidebar h1{font-size:.95rem;font-weight:700;padding:22px 20px 10px;
color:#cba6f7;letter-spacing:.6px;text-transform:uppercase}
.sidebar nav a{display:flex;align-items:center;gap:8px;padding:11px 20px;
color:rgba(205,214,244,.65);text-decoration:none;font-size:.9rem;
border-left:3px solid transparent;transition:all .15s}
.sidebar nav a:hover{background:rgba(255,255,255,.06);color:#cdd6f4;
border-left-color:#6c7086}
.sidebar nav a.active{background:rgba(203,166,247,.12);color:#cba6f7;
border-left-color:#cba6f7;font-weight:600}
.sidebar .divider{height:1px;background:rgba(255,255,255,.06);margin:8px 16px}
.main{flex:1;padding:36px 48px;max-width:760px}
.main h2{font-size:1.35rem;font-weight:700;margin-bottom:5px;color:#11111b}
.main .subtitle{color:#888;font-size:.87rem;margin-bottom:28px}
.card{background:#fff;border-radius:12px;box-shadow:0 1px 3px rgba(0,0,0,.07),
0 4px 12px rgba(0,0,0,.04);padding:22px 24px;margin-bottom:18px}
.card h3{font-size:.75rem;font-weight:700;text-transform:uppercase;
letter-spacing:.9px;color:#aaa;margin-bottom:14px}
.row{display:flex;align-items:center;justify-content:space-between;
padding:10px 0;border-bottom:1px solid #f5f5f5}
.row:last-child{border-bottom:none}
.row label{font-size:.92rem;color:#333}
.row input[type=text]{border:1.5px solid #e0e0e0;border-radius:8px;
padding:7px 12px;font-size:.88rem;width:240px;outline:none;
transition:border-color .15s;background:#fafafa}
.row input[type=text]:focus{border-color:#cba6f7;background:#fff}
.row input[type=number]{border:1.5px solid #e0e0e0;border-radius:8px;
padding:7px 10px;font-size:.88rem;width:80px;outline:none;background:#fafafa}
.toggle{position:relative;width:44px;height:24px;flex-shrink:0}
.toggle input{opacity:0;width:0;height:0}
.slider{position:absolute;inset:0;background:#d0d0d0;border-radius:24px;
transition:.25s;cursor:pointer}
.slider:before{position:absolute;content:'';height:18px;width:18px;
left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.25s;
box-shadow:0 1px 3px rgba(0,0,0,.2)}
input:checked+.slider{background:#cba6f7}
input:checked+.slider:before{transform:translateX(20px)}
.save-btn{background:#cba6f7;color:#11111b;border:none;border-radius:10px;
padding:10px 28px;font-size:.92rem;font-weight:700;cursor:pointer;
transition:background .15s;margin-top:14px}
.save-btn:hover{background:#b48ae4}
.saved-msg{color:#40c057;font-size:.86rem;margin-left:12px;opacity:0;
transition:opacity .3s}
)css";
}

static std::string SettingsSidebarNav(const std::string& active) {
    auto a = [&](const std::string& id) -> std::string {
        return active == id ? " class=\"active\"" : "";
    };
    return
        "<a href=\"ferman://ayarlar/genel\""     + a("genel")     + ">🔧 Genel</a>"
        "<a href=\"ferman://ayarlar/gorunum\""    + a("gorunum")   + ">🎨 Görünüm</a>"
        "<a href=\"ferman://ayarlar/sekmeler\""   + a("sekmeler")  + ">📑 Sekmeler</a>"
        "<a href=\"ferman://ayarlar/gizlilik\""   + a("gizlilik")   + ">🔒 Gizlilik</a>"
        "<a href=\"ferman://ayarlar/gelismis\""   + a("gelismis")   + ">⚙ Gelişmiş</a>"
        "<a href=\"ferman://ayarlar/yapay-zeka\"" + a("yapay-zeka") + ">🤖 Yapay Zeka</a>"
        "<div class=\"divider\"></div>"
        "<a href=\"ferman://ayarlar/hakkimizda\"" + a("hakkimizda") + ">ℹ Hakkımızda</a>";
}

std::string BrowserWindow::BuildSettingsHTML(const std::string& page) {
    const auto& p = SettingsManager::Get().Prefs();
    std::string hp = p.homepage;
    int zoom = (int)(p.default_zoom * 100);
    std::string js_chk  = p.javascript_enabled ? "checked" : "";
    std::string hw_chk  = p.hardware_accel     ? "checked" : "";
    const std::string& cur_se = p.search_engine;

    // Arama motoru <option selected> helper
    auto sel = [&](const std::string& val) -> std::string {
        return (cur_se == val) ? " selected" : "";
    };

    return "<!DOCTYPE html><html lang=\"tr\"><head><meta charset=\"UTF-8\">"
           "<title>Ayarlar</title><style>" + SettingsSidebarCSS() + R"css(
</style></head><body>
<div class="sidebar">
  <h1>⚙ Ayarlar</h1>
  <nav>)css" + SettingsSidebarNav("genel") + R"css(
  </nav>
</div>
<div class="main">
  <h2>Genel Ayarlar</h2>
  <p class="subtitle">Tarayıcı davranışını ve görünümünü özelleştirin.</p>
  <form onsubmit="save(event)">
  <div class="card">
    <h3>Başlangıç</h3>
    <div class="row">
      <label>Ana Sayfa</label>
      <input type="text" id="homepage" value=")css" + hp + R"css(">
    </div>
    <div class="row">
      <label>Arama Motoru</label>
      <select id="se" style="border:1.5px solid #e0e0e0;border-radius:8px;padding:7px 10px;
        font-size:.88rem;background:#fafafa;outline:none;cursor:pointer">
        <option value="google")css" + sel("google") + R"css(>Google</option>
        <option value="bing")css"   + sel("bing")   + R"css(>Bing</option>
        <option value="yahoo")css"  + sel("yahoo")  + R"css(>Yahoo</option>
        <option value="yandex")css" + sel("yandex") + R"css(>Yandex</option>
        <option value="duckduckgo")css" + sel("duckduckgo") + R"css(>DuckDuckGo</option>
        <option value="baidu")css"  + sel("baidu")  + R"css(>Baidu</option>
      </select>
    </div>
  </div>
  <div class="card">
    <h3>Görünüm</h3>
    <div class="row">
      <label>Varsayılan Yakınlaştırma (%)</label>
      <input type="number" id="zoom" min="25" max="500" step="25" value=")css" +
        std::to_string(zoom) + R"css(">
    </div>
  </div>
  <div class="card">
    <h3>İndirmeler</h3>
    <div class="row">
      <label>İndirme Klasörü</label>
      <div style="display:flex;gap:6px;align-items:center;flex:1">
        <input type="text" id="dldir" placeholder="~/İndirilenler (varsayılan)"
          style="flex:1" value=")css" + p.download_dir + R"css(">
        <button type="button" onclick="pickFolder()"
          style="padding:7px 14px;border:1.5px solid #e0e0e0;border-radius:8px;
          background:#fafafa;cursor:pointer;font-size:.84rem;white-space:nowrap">
          &#128193; Seç
        </button>
      </div>
    </div>
  </div>
  <div class="card">
    <h3>Geçmiş</h3>
    <div class="row">
      <label>Geçmişi Sakla (gün)</label>
      <input type="number" id="histdays" min="0" max="365" step="1"
        title="0 = sınırsız" value=")css" + std::to_string(p.history_days) + R"css(">
    </div>
  </div>
  <div class="card">
    <h3>Sekmeler</h3>
    <div class="row">
      <label>Maksimum Sekme Sayısı</label>
      <input type="number" id="maxtabs" min="1" max="100" step="1"
        value=")css" + std::to_string(p.max_tabs) + R"css(">
    </div>
    <div class="row">
      <label>Kapanışta Sekmeleri Hatırla</label>
      <label class="toggle"><input type="checkbox" id="restore" )css" +
        std::string(p.restore_tabs ? "checked" : "") + R"css(>
      <span class="slider"></span></label>
    </div>
  </div>
  <div class="card">
    <h3>Performans</h3>
    <div class="row">
      <label>JavaScript</label>
      <label class="toggle"><input type="checkbox" id="js" )css" + js_chk + R"css(>
      <span class="slider"></span></label>
    </div>
    <div class="row">
      <label>Donanım Hızlandırma</label>
      <label class="toggle"><input type="checkbox" id="hw" )css" + hw_chk + R"css(>
      <span class="slider"></span></label>
    </div>
  </div>
  <button class="save-btn" type="submit">💾 Kaydet</button>
  </form>
</div>

<!-- Toast bildirimi -->
<div id="toast" style="
  position:fixed;bottom:28px;right:28px;z-index:9999;
  background:#1e1e2e;color:#cdd6f4;
  padding:13px 22px;border-radius:12px;
  box-shadow:0 4px 24px rgba(0,0,0,.28);
  font-size:.88rem;font-weight:600;
  display:flex;align-items:center;gap:10px;
  opacity:0;transform:translateY(12px);
  transition:opacity .28s,transform .28s;
  pointer-events:none;min-width:200px">
  <span id="toast-icon">✓</span>
  <span id="toast-msg">Ayarlar kaydedildi.</span>
</div>

<script>
function showToast(msg, ok){
  var t=document.getElementById('toast');
  var ic=document.getElementById('toast-icon');
  var ms=document.getElementById('toast-msg');
  ic.textContent = ok ? '✓' : '✗';
  ic.style.color = ok ? '#a6e3a1' : '#f38ba8';
  ms.textContent = msg;
  t.style.opacity='1'; t.style.transform='translateY(0)';
  setTimeout(function(){
    t.style.opacity='0'; t.style.transform='translateY(12px)';
  }, 3000);
}
function pickFolder(){
  var cur=document.getElementById('dldir').value||'';
  window.location.href='ferman://pick-download-dir?cur='+encodeURIComponent(cur);
}
function save(e){e.preventDefault();
  try {
    var hp=document.getElementById('homepage').value;
    var zoom=document.getElementById('zoom').value;
    var js=document.getElementById('js').checked?1:0;
    var hw=document.getElementById('hw').checked?1:0;
    var se=document.getElementById('se').value;
    var dldir=document.getElementById('dldir').value;
    var histdays=document.getElementById('histdays').value;
    var maxtabs=document.getElementById('maxtabs').value;
    var restore=document.getElementById('restore').checked?1:0;
    window.location.href='ferman://ayarlar-kaydet?hp='+encodeURIComponent(hp)
      +'&zoom='+zoom+'&js='+js+'&hw='+hw+'&se='+se
      +'&dldir='+encodeURIComponent(dldir)
      +'&histdays='+histdays+'&maxtabs='+maxtabs+'&restore='+restore;
    showToast('Ayarlar başarıyla kaydedildi.', true);
  } catch(err) {
    showToast('Kayıt sırasında hata oluştu!', false);
  }
}
</script></body></html>)css";
}

std::string BrowserWindow::BuildPrivacyHTML() {
    return "<!DOCTYPE html><html lang=\"tr\"><head><meta charset=\"UTF-8\">"
           "<title>Gizlilik</title><style>" + SettingsSidebarCSS() + R"css(
.info-box{background:#fff;border-radius:12px;box-shadow:0 1px 3px rgba(0,0,0,.07),
0 4px 12px rgba(0,0,0,.04);padding:22px 24px;margin-bottom:18px}
.info-box h3{font-size:.75rem;font-weight:700;text-transform:uppercase;
letter-spacing:.9px;color:#aaa;margin-bottom:14px}
.info-item{display:flex;align-items:flex-start;gap:12px;padding:12px 0;
border-bottom:1px solid #f5f5f5}
.info-item:last-child{border-bottom:none}
.info-icon{font-size:1.3rem;flex-shrink:0;margin-top:1px}
.info-text strong{display:block;font-size:.92rem;color:#222;margin-bottom:2px}
.info-text span{font-size:.82rem;color:#888}
.clear-btn{background:#fa5252;color:#fff;border:none;border-radius:10px;
padding:10px 24px;font-size:.9rem;font-weight:700;cursor:pointer;transition:background .15s}
.clear-btn:hover{background:#e03131}
</style></head><body>
<div class="sidebar">
  <h1>⚙ Ayarlar</h1>
  <nav>)css" + SettingsSidebarNav("gizlilik") + R"css(</nav>
</div>
<div class="main">
  <h2>Gizlilik ve Güvenlik</h2>
  <p class="subtitle">Verilerinizin nasıl işlendiğini kontrol edin.</p>

  <div class="info-box">
    <h3>Veri Depolama</h3>
    <div class="info-item">
      <span class="info-icon">📜</span>
      <div class="info-text">
        <strong>Gezinti Geçmişi</strong>
        <span>Ziyaret edilen sayfalar yerel SQLite veritabanında saklanır.</span>
      </div>
    </div>
    <div class="info-item">
      <span class="info-icon">🍪</span>
      <div class="info-text">
        <strong>Çerezler</strong>
        <span>Oturum çerezleri kalıcı profilde saklanır; üçüncü taraf takip çerezi engellenmez.</span>
      </div>
    </div>
    <div class="info-item">
      <span class="info-icon">💾</span>
      <div class="info-text">
        <strong>Önbellek</strong>
        <span>Sayfa önbelleği <code>~/.cache/ferman-browser</code> konumunda tutulur.</span>
      </div>
    </div>
    <div class="info-item">
      <span class="info-icon">⭐</span>
      <div class="info-text">
        <strong>Favoriler</strong>
        <span>JSON dosyasında yerel olarak saklanır, hiçbir sunucuya gönderilmez.</span>
      </div>
    </div>
  </div>

  <div class="info-box">
    <h3>Verileri Temizle</h3>
    <div class="info-item">
      <span class="info-icon">🗑</span>
      <div class="info-text">
        <strong>Geçmişi ve Önbelleği Sil</strong>
        <span>Tüm gezinti geçmişi, çerezler ve sayfa önbelleği temizlenir.</span>
      </div>
    </div>
    <div style="padding-top:14px">
      <button class="clear-btn" onclick="showConfirm()">Geçmişi ve Önbelleği Temizle</button>
    </div>
  </div>
</div>

<div style="display:none;position:fixed;inset:0;background:rgba(0,0,0,.45);
align-items:center;justify-content:center;z-index:100" id="overlay">
  <div style="background:#fff;border-radius:14px;padding:32px 36px;
max-width:400px;width:90%;text-align:center;box-shadow:0 8px 32px rgba(0,0,0,.18)">
    <h2 style="font-size:1.1rem;font-weight:700;margin-bottom:10px">Verileri Temizle</h2>
    <p style="color:#666;font-size:.9rem;margin-bottom:24px;line-height:1.5">
      Geçmiş, çerezler ve önbellek silinecek.<br>Bu işlem geri alınamaz.</p>
    <div style="display:flex;gap:12px;justify-content:center">
      <button onclick="hideConfirm()"
        style="background:#f0f0f0;color:#333;border:none;border-radius:8px;
padding:9px 24px;font-size:.9rem;font-weight:600;cursor:pointer">İptal</button>
      <button onclick="doClean()"
        style="background:#fa5252;color:#fff;border:none;border-radius:8px;
padding:9px 24px;font-size:.9rem;font-weight:600;cursor:pointer">Temizle</button>
    </div>
  </div>
</div>
<script>
function showConfirm(){document.getElementById('overlay').style.display='flex';}
function hideConfirm(){document.getElementById('overlay').style.display='none';}
function doClean(){window.location.href='ferman://gecmis-temizle';}
</script>
</body></html>)css";
}

std::string BrowserWindow::BuildHistoryHTML() {
    auto entries = HistoryManager::Get().Recent(200);
    std::string rows;
    for (auto& e : entries) {
        std::string safe_url, safe_title;
        for (char c : e.url)   { if(c=='<') safe_url+="&lt;"; else if(c=='>') safe_url+="&gt;"; else if(c=='&') safe_url+="&amp;"; else if(c=='"') safe_url+="&quot;"; else safe_url+=c; }
        for (char c : e.title) { if(c=='<') safe_title+="&lt;"; else if(c=='>') safe_title+="&gt;"; else if(c=='&') safe_title+="&amp;"; else safe_title+=c; }
        rows += "<div class='row'><a class='title' href='" + safe_url + "'>" +
                (safe_title.empty() ? safe_url : safe_title) + "</a>" +
                "<span class='url'>" + safe_url + "</span></div>\n";
    }
    if (rows.empty()) rows = "<p class='empty'>Henüz geçmiş yok.</p>";
    return R"html(
<!DOCTYPE html><html lang="tr"><head><meta charset="UTF-8">
<title>Geçmiş</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
background:#f5f5f5;color:#1a1a1a;padding:40px 60px;max-width:900px;margin:0 auto}
.header{display:flex;align-items:center;justify-content:space-between;margin-bottom:6px}
h1{font-size:1.6rem;font-weight:700}
.subtitle{color:#888;font-size:.88rem;margin-bottom:28px}
.card{background:#fff;border-radius:10px;box-shadow:0 1px 4px rgba(0,0,0,.08);overflow:hidden}
.row{display:flex;flex-direction:column;padding:12px 20px;
border-bottom:1px solid #f0f0f0;transition:background .1s}
.row:last-child{border-bottom:none}
.row:hover{background:#f8f9fa}
.title{font-size:.95rem;color:#2563eb;text-decoration:none;font-weight:500}
.title:hover{text-decoration:underline}
.url{font-size:.78rem;color:#aaa;margin-top:2px;overflow:hidden;
text-overflow:ellipsis;white-space:nowrap}
.empty{padding:40px;text-align:center;color:#aaa;font-size:1rem}
.clear-btn{background:#fa5252;color:#fff;border:none;border-radius:8px;
padding:9px 22px;font-size:.88rem;font-weight:600;cursor:pointer;transition:background .15s}
.clear-btn:hover{background:#e03131}
/* Onay kutusu */
.overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,.45);
align-items:center;justify-content:center;z-index:100}
.overlay.show{display:flex}
.confirm-box{background:#fff;border-radius:14px;padding:32px 36px;
max-width:400px;width:90%;text-align:center;box-shadow:0 8px 32px rgba(0,0,0,.18)}
.confirm-box h2{font-size:1.1rem;font-weight:700;margin-bottom:10px}
.confirm-box p{color:#666;font-size:.9rem;margin-bottom:24px;line-height:1.5}
.confirm-btns{display:flex;gap:12px;justify-content:center}
.btn-cancel{background:#f0f0f0;color:#333;border:none;border-radius:8px;
padding:9px 24px;font-size:.9rem;font-weight:600;cursor:pointer}
.btn-confirm{background:#fa5252;color:#fff;border:none;border-radius:8px;
padding:9px 24px;font-size:.9rem;font-weight:600;cursor:pointer}
</style></head><body>
<div class="header">
  <h1>Geçmiş</h1>
  <button class="clear-btn" onclick="showConfirm()">🗑 Geçmişi Temizle</button>
</div>
<p class="subtitle">Son ziyaret edilen sayfalar</p>
<div class="card">
)html" + rows + R"html(
</div>

<div class="overlay" id="overlay">
  <div class="confirm-box">
    <h2>Geçmişi Temizle</h2>
    <p>Tüm gezinti geçmişi, çerezler ve önbellek silinecek.<br>Bu işlem geri alınamaz.</p>
    <div class="confirm-btns">
      <button class="btn-cancel" onclick="hideConfirm()">İptal</button>
      <button class="btn-confirm" onclick="doClean()">Temizle</button>
    </div>
  </div>
</div>
<script>
function showConfirm(){document.getElementById('overlay').classList.add('show');}
function hideConfirm(){document.getElementById('overlay').classList.remove('show');}
function doClean(){window.location.href='ferman://gecmis-temizle';}
</script>
</body></html>)html";
}

std::string BrowserWindow::BuildAboutHTML() {
    return R"html(
<!DOCTYPE html><html lang="tr"><head><meta charset="UTF-8">
<title>Hakkımızda</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
background:linear-gradient(135deg,#1a1a2e 0%,#16213e 50%,#0f3460 100%);
min-height:100vh;display:flex;align-items:center;justify-content:center;color:#e0e0e0}
.card{background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.12);
border-radius:20px;padding:52px 64px;text-align:center;max-width:540px;
backdrop-filter:blur(10px)}
.logo{font-size:3.2rem;font-weight:800;margin-bottom:6px;
background:linear-gradient(90deg,#e94560,#4dabf7);
-webkit-background-clip:text;-webkit-text-fill-color:transparent}
.version{font-size:.92rem;color:rgba(255,255,255,.45);margin-bottom:36px}
.badges{display:flex;gap:12px;justify-content:center;margin-bottom:36px;flex-wrap:wrap}
.badge{background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.14);
border-radius:8px;padding:8px 18px;font-size:.82rem;color:rgba(255,255,255,.75)}
.desc{font-size:.95rem;color:rgba(255,255,255,.6);line-height:1.7;margin-bottom:36px}
.links{display:flex;gap:16px;justify-content:center}
.link{color:#4dabf7;text-decoration:none;font-size:.88rem;border-bottom:1px solid rgba(77,171,247,.3);padding-bottom:2px}
.link:hover{color:#74c0fc;border-bottom-color:#74c0fc}
.copy{font-size:.78rem;color:rgba(255,255,255,.28);margin-top:32px}
</style></head><body>
<div class="card">
  <div class="logo">Ferman Browser</div>
  <div class="version">Sürüm 0.3.0 — Şubat 2026</div>
  <div class="badges">
    <span class="badge">GTK4</span>
    <span class="badge">WebKitGTK 6.0</span>
    <span class="badge">C++17</span>
    <span class="badge">SQLite3</span>
  </div>
  <p class="desc">Hızlı, hafif ve özgür bir masaüstü web tarayıcısı.<br>
  Kalıcı oturum, akıllı önbellek, geçmiş, yer imleri<br>ve özelleştirilebilir ayarlar ile geliştirilmiştir.</p>
  <div class="links">
    <a class="link" href="https://github.com">Kaynak Kodu</a>
    <a class="link" href="ferman://ayarlar">Ayarlar</a>
  </div>
  <div class="copy">&copy; 2026 ferman Project — MIT Lisansı</div>
</div>
</body></html>)html";
}

// ── Kurulum Ekranı ───────────────────────────────────────────────────────────
std::string BrowserWindow::BuildSetupHTML(const std::string& error) {
    std::string error_html;
    if (!error.empty()) {
        error_html = "<div id=\"error-msg\" style=\"background:#fee;border:1px solid #fcc;"
            "border-radius:8px;padding:12px 16px;margin-bottom:20px;color:#c00;"
            "font-size:.88rem;display:flex;align-items:center;gap:8px\">"
            "<span style=\"font-size:1.2rem\">⚠</span><span>" + error + "</span></div>";
    }
    
    return R"html(<!DOCTYPE html><html lang="tr"><head><meta charset="UTF-8">
<title>Kurulum — Ferman Browser</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
background:linear-gradient(135deg,#1a1a2e 0%,#16213e 50%,#0f3460 100%);
min-height:100vh;display:flex;flex-direction:column;align-items:center;
justify-content:center;color:#e0e0e0;padding:20px}
.setup-card{background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.12);
border-radius:20px;padding:48px 56px;text-align:center;max-width:480px;width:100%;
backdrop-filter:blur(10px);box-shadow:0 8px 32px rgba(0,0,0,.3)}
.logo{font-size:2.8rem;font-weight:800;margin-bottom:8px;
background:linear-gradient(90deg,#e94560,#4dabf7);
-webkit-background-clip:text;-webkit-text-fill-color:transparent}
.subtitle{font-size:.95rem;color:rgba(255,255,255,.55);margin-bottom:32px;line-height:1.6}
.form-group{margin-bottom:18px;text-align:left}
.form-group label{display:block;font-size:.85rem;color:rgba(255,255,255,.7);
margin-bottom:6px;font-weight:500}
.form-group input{width:100%;padding:12px 16px;border:1.5px solid rgba(255,255,255,.15);
border-radius:10px;font-size:.92rem;background:rgba(255,255,255,.08);
color:#fff;outline:none;transition:all .2s}
.form-group input:focus{border-color:#4dabf7;background:rgba(255,255,255,.12);
box-shadow:0 0 0 3px rgba(77,171,247,.15)}
.form-group input::placeholder{color:rgba(255,255,255,.35)}
.btn-primary{width:100%;background:#4dabf7;border:none;border-radius:10px;
color:#fff;font-size:.95rem;font-weight:600;padding:14px;cursor:pointer;
transition:background .15s;margin-top:8px}
.btn-primary:hover{background:#3a9ae0}
.btn-primary:disabled{background:#555;cursor:not-allowed;opacity:.6}
.btn-secondary{background:transparent;border:1.5px solid rgba(255,255,255,.2);
color:rgba(255,255,255,.7);margin-top:12px}
.btn-secondary:hover{background:rgba(255,255,255,.05);border-color:rgba(255,255,255,.3)}
.spinner{display:none;width:20px;height:20px;border:3px solid rgba(255,255,255,.3);
border-top-color:#fff;border-radius:50%;animation:spin .8s linear infinite;
margin:0 auto 12px}
@keyframes spin{to{transform:rotate(360deg)}}
.loading .spinner{display:block}
.loading .btn-primary{opacity:.6;pointer-events:none}
</style></head><body>
<div class="setup-card">
  <div class="logo">Ferman Browser</div>
  <p class="subtitle">Hoş geldiniz! Başlamak için lütfen bilgilerinizi girin.</p>
  )html" + error_html + R"html(
  <div class="spinner"></div>
  <form id="setup-form" onsubmit="return submitSetup(event)">
    <div class="form-group">
      <label>Email Adresi</label>
      <input type="email" id="email" placeholder="ornek@email.com" required autocomplete="email">
    </div>
    <div class="form-group">
      <label>Şifre</label>
      <input type="password" id="password" placeholder="En az 6 karakter" required 
        autocomplete="new-password" minlength="6">
    </div>
    <div class="form-group">
      <label>İsim</label>
      <input type="text" id="name" placeholder="Adınız Soyadınız" required autocomplete="name">
    </div>
    <button type="submit" class="btn-primary">Kurulumu Başlat</button>
    <button type="button" class="btn-primary btn-secondary" onclick="skipSetup()">
      Kurulumu Atla
    </button>
  </form>
</div>
<script>
function submitSetup(e){
  e.preventDefault();
  var email=document.getElementById('email').value.trim();
  var password=document.getElementById('password').value;
  var name=document.getElementById('name').value.trim();
  if(!email||!password||!name){alert('Tüm alanları doldurun');return false;}
  document.querySelector('.setup-card').classList.add('loading');
  var url='ferman://setup-submit?email='+encodeURIComponent(email)
    +'&password='+encodeURIComponent(password)
    +'&name='+encodeURIComponent(name);
  window.location.href=url;
  return false;
}
function skipSetup(){
  if(confirm('Kurulumu atlamak istediğinizden emin misiniz?\n\n'+
    'Yapay zeka özelliklerini kullanmak için daha sonra ferman.net.tr, '+
    'openai.com, claude.com veya deepseek.com adreslerinden API anahtarı almanız gerekecek.')){
    window.location.href='ferman://setup-skip';
  }
}
</script>
</body></html>)html";
}

// ── Görünüm ──────────────────────────────────────────────────────────────
std::string BrowserWindow::BuildAppearanceSettingsHTML() {
    return "<!DOCTYPE html><html lang=\"tr\"><head><meta charset=\"UTF-8\">"
        "<title>Görünüm — Ayarlar</title><style>" + SettingsSidebarCSS() + R"css(</style></head><body>
<div class="sidebar"><h1>⚙ Ayarlar</h1><nav>)css" +
        SettingsSidebarNav("gorunum") +
        R"css(</nav></div>
<div class="main">
  <h2>Görünüm</h2>
  <p class="subtitle">Tarayıcının görsel ayarlarını özelleştirin.</p>
  <form onsubmit="saveApp(event)">
  <div class="card">
    <h3>Yazı Tipi</h3>
    <div class="row">
      <label>Varsayılan Yazı Boyutu (px)</label>
      <input type="number" id="fontsize" min="8" max="32" step="1" value=")css" +
        std::to_string(SettingsManager::Get().Prefs().font_size) + R"css(">
    </div>
    <div class="row">
      <label>Minimum Yazı Boyutu (px)</label>
      <input type="number" id="minfont" min="6" max="24" step="1" value=")css" +
        std::to_string(SettingsManager::Get().Prefs().min_font_size) + R"css(">
    </div>
  </div>
  <div class="card">
    <h3>Yeni Sekme Sayfası</h3>
    <div class="row">
      <label>Arka Plan Rengi</label>
      <input type="color" id="bgcolor" value="#f8f8f8"
        style="width:48px;height:36px;border:1.5px solid #e0e0e0;border-radius:8px;
        cursor:pointer;padding:2px">
    </div>
  </div>
  <div class="card">
    <h3>Yakınlaştırma</h3>
    <div class="row">
      <label>Varsayılan Yakınlaştırma (%)</label>
      <input type="number" id="zoom" min="25" max="500" step="25" value=")css" +
        std::to_string((int)(SettingsManager::Get().Prefs().default_zoom * 100)) + R"css(">
    </div>
  </div>
  <button class="save-btn" type="submit">💾 Kaydet</button>
  </form>
</div>
)css" + R"html(
<div id="toast" style="position:fixed;bottom:28px;right:28px;z-index:9999;
  background:#1e1e2e;color:#cdd6f4;padding:13px 22px;border-radius:12px;
  box-shadow:0 4px 24px rgba(0,0,0,.28);font-size:.88rem;font-weight:600;
  display:flex;align-items:center;gap:10px;opacity:0;transform:translateY(12px);
  transition:opacity .28s,transform .28s;pointer-events:none;min-width:200px">
  <span id="ti" style="color:#a6e3a1">✓</span><span id="tm">Kaydedildi.</span>
</div>
<script>
function toast(msg,ok){
  var t=document.getElementById('toast');
  document.getElementById('ti').style.color=ok?'#a6e3a1':'#f38ba8';
  document.getElementById('ti').textContent=ok?'✓':'✗';
  document.getElementById('tm').textContent=msg;
  t.style.opacity='1';t.style.transform='translateY(0)';
  setTimeout(function(){t.style.opacity='0';t.style.transform='translateY(12px)';},3000);
}
function saveApp(e){
  e.preventDefault();
  try{
    var zoom=document.getElementById('zoom').value;
    var fontsize=document.getElementById('fontsize').value;
    var minfont=document.getElementById('minfont').value;
    window.location.href='ferman://ayarlar-kaydet?zoom='+zoom
      +'&fontsize='+fontsize+'&minfont='+minfont;
    toast('Görünüm ayarları kaydedildi.',true);
  }catch(err){toast('Hata oluştu!',false);}
}
</script>
</body></html>)html";
}

// ── Sekmeler ─────────────────────────────────────────────────────────────
std::string BrowserWindow::BuildTabsSettingsHTML() {
    const auto& p = SettingsManager::Get().Prefs();
    std::string rt = p.restore_tabs ? "checked" : "";
    return "<!DOCTYPE html><html lang=\"tr\"><head><meta charset=\"UTF-8\">"
        "<title>Sekmeler — Ayarlar</title><style>" + SettingsSidebarCSS() + R"css(</style></head><body>
<div class="sidebar"><h1>⚙ Ayarlar</h1><nav>)css" +
        SettingsSidebarNav("sekmeler") +
        R"css(</nav></div>
<div class="main">
  <h2>Sekme Ayarları</h2>
  <p class="subtitle">Sekme limitleri ve başlangıç davranışını ayarlayın.</p>
  <form onsubmit="saveTab(event)">
  <div class="card">
    <h3>Limitler</h3>
    <div class="row">
      <label>Maksimum Sekme Sayısı</label>
      <input type="number" id="maxtabs" min="1" max="100" step="1"
        value=")css" + std::to_string(p.max_tabs) + R"css(">
    </div>
  </div>
  <div class="card">
    <h3>Başlangıç</h3>
    <div class="row">
      <label>Kapanışta Sekmeleri Hatırla</label>
      <label class="toggle"><input type="checkbox" id="restore" )css" + rt + R"css(>
        <span class="slider"></span></label>
    </div>
    <div class="row">
      <label>Yeni Sekme Sayfası</label>
      <input type="text" id="newtab" placeholder="ferman://home"
        value=")css" + p.homepage + R"css(">
    </div>
  </div>
  <button class="save-btn" type="submit">💾 Kaydet</button>
  </form>
</div>
)css" + R"html(
<div id="toast" style="position:fixed;bottom:28px;right:28px;z-index:9999;
  background:#1e1e2e;color:#cdd6f4;padding:13px 22px;border-radius:12px;
  box-shadow:0 4px 24px rgba(0,0,0,.28);font-size:.88rem;font-weight:600;
  display:flex;align-items:center;gap:10px;opacity:0;transform:translateY(12px);
  transition:opacity .28s,transform .28s;pointer-events:none;min-width:200px">
  <span id="ti" style="color:#a6e3a1">✓</span><span id="tm">Kaydedildi.</span>
</div>
<script>
function toast(msg,ok){
  var t=document.getElementById('toast');
  document.getElementById('ti').style.color=ok?'#a6e3a1':'#f38ba8';
  document.getElementById('ti').textContent=ok?'✓':'✗';
  document.getElementById('tm').textContent=msg;
  t.style.opacity='1';t.style.transform='translateY(0)';
  setTimeout(function(){t.style.opacity='0';t.style.transform='translateY(12px)';},3000);
}
function saveTab(e){
  e.preventDefault();
  try{
    var maxtabs=document.getElementById('maxtabs').value;
    var restore=document.getElementById('restore').checked?1:0;
    var hp=document.getElementById('newtab').value;
    window.location.href='ferman://ayarlar-kaydet?maxtabs='+maxtabs
      +'&restore='+restore+'&hp='+encodeURIComponent(hp);
    toast('Sekme ayarları kaydedildi.',true);
  }catch(err){toast('Hata oluştu!',false);}
}
</script>
</body></html>)html";
}

// ── Gelişmiş ─────────────────────────────────────────────────────────────
std::string BrowserWindow::BuildAdvancedSettingsHTML() {
    const auto& p = SettingsManager::Get().Prefs();
    std::string js_chk = p.javascript_enabled ? "checked" : "";
    std::string hw_chk = p.hardware_accel     ? "checked" : "";
    return "<!DOCTYPE html><html lang=\"tr\"><head><meta charset=\"UTF-8\">"
        "<title>Gelişmiş — Ayarlar</title><style>" + SettingsSidebarCSS() + R"css(</style></head><body>
<div class="sidebar"><h1>⚙ Ayarlar</h1><nav>)css" +
        SettingsSidebarNav("gelismis") +
        R"css(</nav></div>
<div class="main">
  <h2>Gelişmiş Ayarlar</h2>
  <p class="subtitle">Performans ve gizlilik ile ilgili teknik ayarlar.</p>
  <form onsubmit="saveAdv(event)">
  <div class="card">
    <h3>Performans</h3>
    <div class="row">
      <label>JavaScript</label>
      <label class="toggle"><input type="checkbox" id="js" )css" + js_chk + R"css(>
        <span class="slider"></span></label>
    </div>
    <div class="row">
      <label>Donanım Hızlandırma</label>
      <label class="toggle"><input type="checkbox" id="hw" )css" + hw_chk + R"css(>
        <span class="slider"></span></label>
    </div>
  </div>
  <div class="card">
    <h3>İndirmeler</h3>
    <div class="row">
      <label>İndirme Klasörü</label>
      <div style="display:flex;gap:6px;align-items:center;flex:1">
        <input type="text" id="dldir" placeholder="~/İndirilenler (varsayılan)"
          style="flex:1" value=")css" + p.download_dir + R"css(">
        <button type="button" onclick="pickFolder()"
          style="padding:7px 14px;border:1.5px solid #e0e0e0;border-radius:8px;
          background:#fafafa;cursor:pointer;font-size:.84rem;white-space:nowrap">
          &#128193; Seç
        </button>
      </div>
    </div>
  </div>
  <div class="card">
    <h3>Geçmiş</h3>
    <div class="row">
      <label>Geçmişi Sakla (gün, 0=sınırsız)</label>
      <input type="number" id="histdays" min="0" max="365" step="1"
        value=")css" + std::to_string(p.history_days) + R"css(">
    </div>
  </div>
  <div class="card">
    <h3>Kullanıcı Ajanı</h3>
    <div class="row" style="flex-direction:column;align-items:flex-start;gap:8px">
      <label>Tarayıcı Kimliği (User-Agent)</label>
      <input type="text" id="ua" style="width:100%"
        value="Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36"
        readonly style="background:#f5f5f5;color:#888;cursor:not-allowed">
      <span style="font-size:.78rem;color:#aaa">Chrome 146 kimliği kullanılıyor (değiştirilemez).</span>
    </div>
  </div>
  <button class="save-btn" type="submit">💾 Kaydet</button>
  </form>
</div>
)css" + R"html(
<div id="toast" style="position:fixed;bottom:28px;right:28px;z-index:9999;
  background:#1e1e2e;color:#cdd6f4;padding:13px 22px;border-radius:12px;
  box-shadow:0 4px 24px rgba(0,0,0,.28);font-size:.88rem;font-weight:600;
  display:flex;align-items:center;gap:10px;opacity:0;transform:translateY(12px);
  transition:opacity .28s,transform .28s;pointer-events:none;min-width:200px">
  <span id="ti" style="color:#a6e3a1">✓</span><span id="tm">Kaydedildi.</span>
</div>
<script>
function toast(msg,ok){
  var t=document.getElementById('toast');
  document.getElementById('ti').style.color=ok?'#a6e3a1':'#f38ba8';
  document.getElementById('ti').textContent=ok?'✓':'✗';
  document.getElementById('tm').textContent=msg;
  t.style.opacity='1';t.style.transform='translateY(0)';
  setTimeout(function(){t.style.opacity='0';t.style.transform='translateY(12px)';},3000);
}
function saveAdv(e){
  e.preventDefault();
  try{
    var js=document.getElementById('js').checked?1:0;
    var hw=document.getElementById('hw').checked?1:0;
    var dldir=document.getElementById('dldir').value;
    var histdays=document.getElementById('histdays').value;
    window.location.href='ferman://ayarlar-kaydet?js='+js+'&hw='+hw
      +'&dldir='+encodeURIComponent(dldir)+'&histdays='+histdays;
    toast('Gelişmiş ayarlar kaydedildi.',true);
  }catch(err){toast('Hata oluştu!',false);}
}
</script>
</body></html>)html";
}

std::string BrowserWindow::BuildSettingsAiHTML() {
    const auto& agents = AiAgentStore::Get().Agents();

    std::string css = SettingsSidebarCSS();
    css +=
        ".agent-tbl{width:100%;border-collapse:collapse;font-size:.85rem}"
        ".agent-tbl th{text-align:left;padding:8px 10px;border-bottom:2px solid #e8e8e8;color:#555;font-weight:600}"
        ".agent-tbl td{padding:7px 10px;border-bottom:1px solid #f2f2f2;vertical-align:middle}"
        ".agent-tbl tr:hover td{background:#f5f9ff}"
        ".badge{display:inline-block;padding:2px 8px;border-radius:99px;font-size:.72rem;font-weight:600}"
        ".b-openai{background:#d4edda;color:#155724}"
        ".b-anthropic{background:#fff3cd;color:#856404}"
        ".b-deepseek{background:#cce5ff;color:#004085}"
        ".b-groq{background:#f8d7da;color:#721c24}"
        ".b-openrouter{background:#e2d9f3;color:#432874}"
        ".b-other{background:#e8e8e8;color:#555}"
        ".row-btns{display:flex;gap:5px}"
        ".ebtn{background:#f0f0f0;border:1px solid #d5d5d5;border-radius:5px;"
        "padding:3px 10px;font-size:.78rem;cursor:pointer;color:#333}"
        ".ebtn:hover{background:#ddeeff;border-color:#aad0f5;color:#1a6fcc}"
        ".dbtn{background:#fff0f0;border:1px solid #fcc;border-radius:5px;"
        "padding:3px 10px;font-size:.78rem;cursor:pointer;color:#c00}"
        ".dbtn:hover{background:#fdd}"
        ".fcard{background:#fafafa;border:1px solid #e5e5e5;border-radius:10px;"
        "padding:18px 20px;margin-top:14px}"
        ".fcard h3{margin:0 0 14px;font-size:.93rem;color:#333}"
        ".fr{display:flex;flex-direction:column;gap:4px;margin-bottom:11px}"
        ".fr label{font-size:.81rem;color:#555;font-weight:500}"
        ".fr input{padding:7px 10px;border:1px solid #d0d0d0;border-radius:6px;"
        "font-size:.87rem;color:#222;background:#fff}"
        ".fr input:focus{outline:none;border-color:#7ab0f0;box-shadow:0 0 0 2px #ddeeff}"
        ".fr .hint{font-size:.74rem;color:#aaa;margin-top:2px}"
        ".tipbox{background:#f5f9ff;border:1px solid #d8eaff;border-radius:8px;padding:12px 16px;margin-top:12px}"
        ".tipbox code{background:#e8f0fe;padding:1px 5px;border-radius:3px;font-size:.82rem}";

    std::string html;
    html  = "<!DOCTYPE html><html lang=\"tr\"><head><meta charset=\"UTF-8\">";
    html += "<title>Yapay Zeka \xe2\x80\x94 Ayarlar</title><style>" + css + "</style></head><body>\n";
    html += "<div class=\"sidebar\"><h1>\xe2\x9a\x99 Ayarlar</h1><nav>";
    html += SettingsSidebarNav("yapay-zeka");
    html += "</nav></div>\n<div class=\"main\">\n";
    html += "<h2>Yapay Zeka Ajanlar\xc4\xb1</h2>\n";
    html += "<p class=\"subtitle\">Her ajan kendi API anahtarı ve modeliyle çalışır. "
            "Provider, API anahtarından otomatik belirlenir.</p>\n";

    // Ajan tablosu
    html += "<div class=\"card\" style=\"padding:0;overflow:hidden\">\n";
    if (agents.empty()) {
        html += "<p style=\"padding:18px;color:#aaa;font-size:.88rem\">"
                "Henüz ajan eklenmemiş. Aşağıdaki formdan ekleyebilirsiniz.</p>\n";
    } else {
        html += "<table class=\"agent-tbl\"><thead><tr>"
                "<th>Ajan \xc4\xb0smi</th><th>Provider</th><th>Model</th><th>API URL</th><th></th>"
                "</tr></thead><tbody>\n";
        for (const auto& a : agents) {
            std::string prov = AiAgent::DetectProvider(a.api_key);
            std::string bc = "b-other";
            if      (prov == "openai")     bc = "b-openai";
            else if (prov == "anthropic")  bc = "b-anthropic";
            else if (prov == "deepseek")   bc = "b-deepseek";
            else if (prov == "groq")       bc = "b-groq";
            else if (prov == "openrouter") bc = "b-openrouter";
            std::string url_disp = a.api_url.empty()
                ? "<span style=\"color:#bbb\">varsay\xc4\xb1lan</span>" : a.api_url;
            html += "<tr>"
                    "<td><strong>" + a.name + "</strong></td>"
                    "<td><span class=\"badge " + bc + "\">" + prov + "</span></td>"
                    "<td><code style=\"font-size:.79rem\">" + a.model + "</code></td>"
                    "<td style=\"font-size:.78rem;color:#888\">" + url_disp + "</td>"
                    "<td><div class=\"row-btns\">"
                    "<button class=\"ebtn\" onclick=\"editAgent('" + a.id + "','"
                        + a.name + "','" + a.model + "','" + a.api_url + "')\">D\xc3\xbczenle</button>"
                    "<button class=\"dbtn\" onclick=\"delAgent('" + a.id + "')\">Sil</button>"
                    "</div></td></tr>\n";
        }
        html += "</tbody></table>\n";
    }
    html += "</div>\n";

    // Ekle/Düzenle formu
    html += "<div class=\"fcard\" id=\"aform\">\n"
            "<h3 id=\"ftitle\">Yeni Ajan Ekle</h3>\n"
            "<input type=\"hidden\" id=\"aid\" value=\"\">\n"
            "<div class=\"fr\"><label>Ajan \xc4\xb0smi</label>"
            "<input id=\"aname\" placeholder=\"Örn: Benim GPT Asistanım\"></div>\n"
            "<div class=\"fr\"><label>API Anahtarı</label>"
            "<input type=\"password\" id=\"akey\" autocomplete=\"off\" "
            "placeholder=\"sk-...  /  sk-ant-...  /  ds-...\" oninput=\"detectProv(this.value)\">"
            "<span class=\"hint\" id=\"pdet\">Provider: otomatik belirlenecek</span></div>\n"
            "<div class=\"fr\"><label>Model</label>"
            "<input id=\"amodel\" placeholder=\"gpt-4o, deepseek-chat, claude-3-5-sonnet-20241022\"></div>\n"
            "<div class=\"fr\"><label>API URL "
            "<span style=\"font-size:.74rem;color:#bbb\">(isteğe bağlı, "
            "boşsa varsayılan endpoint kullanılır)</span></label>"
            "<input id=\"aurl\" placeholder=\"https://...\"></div>\n"
            "<div style=\"display:flex;gap:8px;margin-top:6px\">"
            "<button class=\"save-btn\" onclick=\"saveAgent()\">Kaydet</button>"
            "<button type=\"button\" style=\"background:#f0f0f0;border:1px solid #d5d5d5;"
            "border-radius:7px;padding:6px 14px;cursor:pointer;font-size:.85rem\" "
            "onclick=\"resetForm()\">Temizle</button>"
            "</div>\n</div>\n";

    // Token ipuçları
    html += "<div class=\"tipbox\">\n"
            "<strong style=\"font-size:.85rem;color:#1a6fcc\">Token İpuçları</strong>\n"
            "<ul style=\"margin:6px 0 0;padding-left:18px;font-size:.83rem;color:#555;line-height:1.9\">\n"
            "<li><code>@ajan-ismi</code> \xe2\x80\x94 sohbette ajan seç</li>\n"
            "<li><code>#1</code> \xe2\x80\x94 sekme #1 URL içeriğini dahil et</li>\n"
            "<li><code>*5</code> \xe2\x80\x94 sohbet #5'i bağlam olarak ekle</li>\n"
            "<li>\xf0\x9f\x93\x8e butonu ile PDF, DOCX, XLSX, resim ekleyebilirsiniz</li>\n"
            "</ul>\n</div>\n";

    // Toast + Script
    html += "<div id=\"toast\" style=\"position:fixed;bottom:24px;right:24px;z-index:9999;"
            "background:#222;color:#fff;padding:11px 18px;border-radius:8px;"
            "box-shadow:0 3px 16px rgba(0,0,0,.18);font-size:.85rem;"
            "display:flex;align-items:center;gap:9px;opacity:0;transform:translateY(8px);"
            "transition:opacity .22s,transform .22s;pointer-events:none\">"
            "<span id=\"ti\">\xe2\x9c\x93</span><span id=\"tm\">Kaydedildi.</span></div>\n";
    html += "<script>\n"
        "function detectProv(k){\n"
        "  var p='deepseek';\n"
        "  if(k.indexOf('sk-ant-')===0)p='anthropic';\n"
        "  else if(k.indexOf('sk-or-')===0)p='openrouter';\n"
        "  else if(k.indexOf('gsk_')===0)p='groq';\n"
        "  else if(k.indexOf('ds-')===0)p='deepseek';\n"
        "  else if(k.indexOf('sk-')===0)p='openai';\n"
        "  document.getElementById('pdet').textContent='Provider: '+p;\n"
        "}\n"
        "function toast(msg,ok){\n"
        "  var t=document.getElementById('toast');\n"
        "  document.getElementById('ti').textContent=ok?'\\u2713':'\\u2717';\n"
        "  document.getElementById('tm').textContent=msg;\n"
        "  t.style.opacity='1';t.style.transform='translateY(0)';\n"
        "  setTimeout(function(){t.style.opacity='0';t.style.transform='translateY(8px)';},2600);\n"
        "}\n"
        "function saveAgent(){\n"
        "  var id=document.getElementById('aid').value;\n"
        "  var name=document.getElementById('aname').value.trim();\n"
        "  var key=document.getElementById('akey').value.trim();\n"
        "  var model=document.getElementById('amodel').value.trim();\n"
        "  var url=document.getElementById('aurl').value.trim();\n"
        "  if(!name){toast('Ajan ismi gerekli!',false);return;}\n"
        "  window.location.href='ferman://ai-ajan-kaydet'"
        "    +'?id='+encodeURIComponent(id)"
        "    +'&name='+encodeURIComponent(name)"
        "    +'&key='+encodeURIComponent(key)"
        "    +'&model='+encodeURIComponent(model)"
        "    +'&url='+encodeURIComponent(url);\n"
        "}\n"
        "function editAgent(id,name,model,url){\n"
        "  document.getElementById('aid').value=id;\n"
        "  document.getElementById('aname').value=name;\n"
        "  document.getElementById('akey').value='';\n"
        "  document.getElementById('amodel').value=model;\n"
        "  document.getElementById('aurl').value=url;\n"
        "  document.getElementById('ftitle').textContent='Ajan D\\u00fczenle';\n"
        "  document.getElementById('aform').scrollIntoView({behavior:'smooth'});\n"
        "}\n"
        "function delAgent(id){\n"
        "  if(confirm('Bu ajan\\u0131 silmek istedi\\u011finizden emin misiniz?'))\n"
        "    window.location.href='ferman://ai-ajan-sil?id='+encodeURIComponent(id);\n"
        "}\n"
        "function resetForm(){\n"
        "  ['aid','aname','akey','amodel','aurl'].forEach(function(x){"
        "document.getElementById(x).value='';});\n"
        "  document.getElementById('pdet').textContent='Provider: otomatik belirlenecek';\n"
        "  document.getElementById('ftitle').textContent='Yeni Ajan Ekle';\n"
        "}\n"
        "</script>\n</div></body></html>";
    return html;
}

void BrowserWindow::ShowSettingsPage() {
    NewTab("ferman://ayarlar");
}

// ── Favori sağ-tık bağlam menüsü ─────────────────────────────────────────────
namespace ferman {

struct BmRenameCtx   { BrowserWindow* self; std::string url; std::string cur_title; };
struct BmDelCtx      { BrowserWindow* self; std::string url; };
struct BmSaveCtx     { BrowserWindow* self; std::string url; };
struct BmMoveToCtx   { BrowserWindow* self; std::string url; std::string folder; };
struct BmNewFolderCtx{ BrowserWindow* self; std::string url; };  // url boş = sadece klasör oluştur
struct BmBarCtx      { BrowserWindow* self; };

// ── Yardımcı: mevcut klasör listesini topla ──────────────────────────────────
static std::vector<std::string> BmGetFolders() {
    std::vector<std::string> folders;
    for (const auto& bm : BookmarkManager::Get().All()) {
        if (bm.folder.empty()) continue;
        bool found = false;
        for (auto& f : folders) if (f == bm.folder) { found = true; break; }
        if (!found) folders.push_back(bm.folder);
    }
    return folders;
}

// ── Kaydet (isim değiştir) ───────────────────────────────────────────────────
static void BmSaveCb(GtkButton* sb, gpointer) {
    auto* sc  = static_cast<BmSaveCtx*>(g_object_get_data(G_OBJECT(sb), "sctx"));
    GtkWidget* ep  = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(sb), "epop"));
    GtkWidget* ent = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(ep), "entry_w"));
    const char* v = gtk_editable_get_text(GTK_EDITABLE(ent));
    BookmarkManager::Get().Rename(sc->url, v ? v : "");
    gtk_popover_popdown(GTK_POPOVER(ep));
    sc->self->RebuildBookmarksBar();
}
static void BmSaveEnterCb(GtkEntry* e, gpointer) {
    auto* sc  = static_cast<BmSaveCtx*>(g_object_get_data(G_OBJECT(e), "sctx"));
    GtkWidget* ep  = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(e), "epop"));
    const char* v = gtk_editable_get_text(GTK_EDITABLE(e));
    BookmarkManager::Get().Rename(sc->url, v ? v : "");
    gtk_popover_popdown(GTK_POPOVER(ep));
    sc->self->RebuildBookmarksBar();
}

// ── Klasöre taşı (seçilmiş klasör adıyla) ───────────────────────────────────
static void BmMoveToFolderCb(GtkButton* b, gpointer) {
    auto* ctx = static_cast<BmMoveToCtx*>(g_object_get_data(G_OBJECT(b), "mtctx"));
    GtkWidget* pp = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(b), "fpop"));
    BookmarkManager::Get().MoveToFolder(ctx->url, ctx->folder);
    if (pp) gtk_popover_popdown(GTK_POPOVER(pp));
    ctx->self->RebuildBookmarksBar();
}

// ── Yeni klasör (entry + kaydet) ─────────────────────────────────────────────
static void BmNewFolderSaveCb(GtkButton* sb, gpointer) {
    auto* ctx = static_cast<BmNewFolderCtx*>(g_object_get_data(G_OBJECT(sb), "nfctx"));
    GtkWidget* pp  = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(sb), "fpop"));
    GtkWidget* ent = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(pp), "entry_w"));
    const char* v = gtk_editable_get_text(GTK_EDITABLE(ent));
    if (v && v[0] != '\0') {
        if (!ctx->url.empty())
            BookmarkManager::Get().MoveToFolder(ctx->url, v);
    }
    gtk_popover_popdown(GTK_POPOVER(pp));
    ctx->self->RebuildBookmarksBar();
}
static void BmNewFolderEnterCb(GtkEntry* e, gpointer) {
    auto* ctx = static_cast<BmNewFolderCtx*>(g_object_get_data(G_OBJECT(e), "nfctx"));
    GtkWidget* pp  = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(e), "fpop"));
    const char* v = gtk_editable_get_text(GTK_EDITABLE(e));
    if (v && v[0] != '\0') {
        if (!ctx->url.empty())
            BookmarkManager::Get().MoveToFolder(ctx->url, v);
    }
    gtk_popover_popdown(GTK_POPOVER(pp));
    ctx->self->RebuildBookmarksBar();
}

// ── Sil ──────────────────────────────────────────────────────────────────────
static void BmDeleteClickCb(GtkButton* db, gpointer) {
    auto* dc = static_cast<BmDelCtx*>(g_object_get_data(G_OBJECT(db), "dctx"));
    GtkWidget* cp = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(db), "ctx_pop"));
    BookmarkManager::Get().Remove(dc->url);
    gtk_popover_popdown(GTK_POPOVER(cp));
    dc->self->RebuildBookmarksBar();
}

// ── İsim değiştir popover ─────────────────────────────────────────────────────
static void BmRenameClickCb(GtkButton* rb, gpointer ud) {
    auto* self2 = static_cast<BrowserWindow*>(ud);
    auto* rc    = static_cast<BmRenameCtx*>(g_object_get_data(G_OBJECT(rb), "rctx"));
    GtkWidget* cp = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(rb), "ctx_pop"));
    gtk_popover_popdown(GTK_POPOVER(cp));

    GtkWidget* epop = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(epop), FALSE);
    gtk_widget_set_parent(epop, gtk_widget_get_parent(cp));

    GtkWidget* ebox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(ebox, 10); gtk_widget_set_margin_end(ebox, 10);
    gtk_widget_set_margin_top(ebox, 10);   gtk_widget_set_margin_bottom(ebox, 10);

    GtkWidget* lbl = gtk_label_new("Yeni isim:");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    GtkWidget* entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), rc->cur_title.c_str());
    gtk_widget_set_size_request(entry, 200, -1);

    auto* sc = new BmSaveCtx{ self2, rc->url };
    g_object_set_data_full(G_OBJECT(entry), "sctx", sc,
        [](gpointer p){ delete static_cast<BmSaveCtx*>(p); });
    g_object_set_data(G_OBJECT(entry), "epop", epop);

    GtkWidget* save_btn = gtk_button_new_with_label("Kaydet");
    gtk_widget_add_css_class(save_btn, "suggested-action");
    g_object_set_data(G_OBJECT(save_btn), "sctx", sc);
    g_object_set_data(G_OBJECT(save_btn), "epop", epop);
    g_object_set_data(G_OBJECT(epop), "entry_w", entry);
    g_signal_connect(save_btn, "clicked", G_CALLBACK(BmSaveCb),      nullptr);
    g_signal_connect(entry,    "activate",G_CALLBACK(BmSaveEnterCb), nullptr);

    gtk_box_append(GTK_BOX(ebox), lbl);
    gtk_box_append(GTK_BOX(ebox), entry);
    gtk_box_append(GTK_BOX(ebox), save_btn);
    gtk_popover_set_child(GTK_POPOVER(epop), ebox);
    gtk_popover_popup(GTK_POPOVER(epop));
}

// ── Klasöre taşı popover (mevcut klasörler listesi + yeni klasör) ─────────────
static void BmMoveClickCb(GtkButton* mb, gpointer ud) {
    auto* self2 = static_cast<BrowserWindow*>(ud);
    auto* mc_url_ptr = static_cast<std::string*>(g_object_get_data(G_OBJECT(mb), "mc-url"));
    GtkWidget* cp = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(mb), "ctx_pop"));
    std::string bm_url = mc_url_ptr ? *mc_url_ptr : "";
    gtk_popover_popdown(GTK_POPOVER(cp));

    GtkWidget* fpop = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(fpop), FALSE);
    gtk_widget_set_parent(fpop, gtk_widget_get_parent(cp));

    GtkWidget* fbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(fbox, 8); gtk_widget_set_margin_end(fbox, 8);
    gtk_widget_set_margin_top(fbox, 8);   gtk_widget_set_margin_bottom(fbox, 8);

    auto folders = BmGetFolders();

    if (!folders.empty()) {
        GtkWidget* sec_lbl = gtk_label_new("Klasör seç:");
        gtk_widget_add_css_class(sec_lbl, "dim-label");
        gtk_widget_set_halign(sec_lbl, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(fbox), sec_lbl);

        // Kök (klasörsüz) seçeneği
        auto* root_ctx = new BmMoveToCtx{ self2, bm_url, "" };
        GtkWidget* root_btn = gtk_button_new_with_label("— Kök (klasörsüz)");
        gtk_widget_add_css_class(root_btn, "flat");
        gtk_widget_set_halign(root_btn, GTK_ALIGN_FILL);
        g_object_set_data_full(G_OBJECT(root_btn), "mtctx", root_ctx,
            [](gpointer p){ delete static_cast<BmMoveToCtx*>(p); });
        g_object_set_data(G_OBJECT(root_btn), "fpop", fpop);
        g_signal_connect(root_btn, "clicked", G_CALLBACK(BmMoveToFolderCb), nullptr);
        gtk_box_append(GTK_BOX(fbox), root_btn);

        for (const auto& f : folders) {
            auto* fctx = new BmMoveToCtx{ self2, bm_url, f };
            std::string flabel = "📁 " + f;
            GtkWidget* fb = gtk_button_new_with_label(flabel.c_str());
            gtk_widget_add_css_class(fb, "flat");
            gtk_widget_set_halign(fb, GTK_ALIGN_FILL);
            g_object_set_data_full(G_OBJECT(fb), "mtctx", fctx,
                [](gpointer p){ delete static_cast<BmMoveToCtx*>(p); });
            g_object_set_data(G_OBJECT(fb), "fpop", fpop);
            g_signal_connect(fb, "clicked", G_CALLBACK(BmMoveToFolderCb), nullptr);
            gtk_box_append(GTK_BOX(fbox), fb);
        }
        gtk_box_append(GTK_BOX(fbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    }

    // Yeni klasör oluştur
    GtkWidget* new_lbl = gtk_label_new("Yeni klasör adı:");
    gtk_widget_set_halign(new_lbl, GTK_ALIGN_START);
    GtkWidget* new_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(new_entry), "Klasör adı…");
    gtk_widget_set_size_request(new_entry, 190, -1);

    auto* nfctx = new BmNewFolderCtx{ self2, bm_url };
    g_object_set_data_full(G_OBJECT(new_entry), "nfctx", nfctx,
        [](gpointer p){ delete static_cast<BmNewFolderCtx*>(p); });
    g_object_set_data(G_OBJECT(new_entry), "fpop", fpop);
    g_object_set_data(G_OBJECT(fpop), "entry_w", new_entry);

    GtkWidget* new_save = gtk_button_new_with_label(bm_url.empty() ? "Oluştur" : "Oluştur ve Taşı");
    gtk_widget_add_css_class(new_save, "suggested-action");
    g_object_set_data(G_OBJECT(new_save), "nfctx", nfctx);
    g_object_set_data(G_OBJECT(new_save), "fpop",  fpop);
    g_signal_connect(new_save,  "clicked",  G_CALLBACK(BmNewFolderSaveCb),  nullptr);
    g_signal_connect(new_entry, "activate", G_CALLBACK(BmNewFolderEnterCb), nullptr);

    gtk_box_append(GTK_BOX(fbox), new_lbl);
    gtk_box_append(GTK_BOX(fbox), new_entry);
    gtk_box_append(GTK_BOX(fbox), new_save);
    gtk_popover_set_child(GTK_POPOVER(fpop), fbox);
    gtk_popover_popup(GTK_POPOVER(fpop));
}

static void BmNewFolderClickCb(GtkButton*, gpointer);  // forward declaration

// ── Ortak: bağlam menüsü popup oluştur (url + title + parent widget) ─────────
static void BmShowContextMenu(BrowserWindow* self, GtkWidget* parent_w,
                               const std::string& url, const std::string& title) {
    GtkWidget* pop = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(pop), FALSE);
    gtk_widget_set_parent(pop, parent_w);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(vbox, 6); gtk_widget_set_margin_end(vbox, 6);
    gtk_widget_set_margin_top(vbox, 6);   gtk_widget_set_margin_bottom(vbox, 6);

    std::string hdr = title.empty() ? url : title;
    if (hdr.size() > 30) hdr = hdr.substr(0, 28) + "\u2026";
    GtkWidget* hdr_lbl = gtk_label_new(hdr.c_str());
    gtk_widget_add_css_class(hdr_lbl, "dim-label");
    gtk_widget_set_halign(hdr_lbl, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(hdr_lbl, 2);
    gtk_box_append(GTK_BOX(vbox), hdr_lbl);
    gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // İsim değiştir
    auto* rctx = new BmRenameCtx{ self, url, title.empty() ? url : title };
    GtkWidget* rename_btn = gtk_button_new_with_label("İsim değiştir");
    gtk_widget_add_css_class(rename_btn, "flat");
    gtk_widget_set_halign(rename_btn, GTK_ALIGN_START);
    g_object_set_data_full(G_OBJECT(rename_btn), "rctx", rctx,
        [](gpointer p){ delete static_cast<BmRenameCtx*>(p); });
    g_object_set_data(G_OBJECT(rename_btn), "ctx_pop", pop);
    g_signal_connect(rename_btn, "clicked", G_CALLBACK(BmRenameClickCb), self);
    gtk_box_append(GTK_BOX(vbox), rename_btn);

    // Klasöre taşı (mevcut klasörler listesi)
    auto* mc_url = new std::string(url);
    GtkWidget* move_btn = gtk_button_new_with_label("Klasöre taşı");
    gtk_widget_add_css_class(move_btn, "flat");
    gtk_widget_set_halign(move_btn, GTK_ALIGN_START);
    g_object_set_data_full(G_OBJECT(move_btn), "mc-url", mc_url,
        [](gpointer p){ delete static_cast<std::string*>(p); });
    g_object_set_data(G_OBJECT(move_btn), "ctx_pop", pop);
    g_signal_connect(move_btn, "clicked", G_CALLBACK(BmMoveClickCb), self);
    gtk_box_append(GTK_BOX(vbox), move_btn);

    // Klasör oluştur (yeni klasör → url ile)
    auto* mc_url2 = new std::string(url);
    GtkWidget* newfolder_btn = gtk_button_new_with_label("Yeni klasör oluştur");
    gtk_widget_add_css_class(newfolder_btn, "flat");
    gtk_widget_set_halign(newfolder_btn, GTK_ALIGN_START);
    g_object_set_data_full(G_OBJECT(newfolder_btn), "mc-url", mc_url2,
        [](gpointer p){ delete static_cast<std::string*>(p); });
    g_object_set_data(G_OBJECT(newfolder_btn), "ctx_pop", pop);
    // Yeni klasör: BmMoveClickCb'yi kullan — folders boşsa direkt yeni klasör alanı gösterir
    g_signal_connect(newfolder_btn, "clicked", G_CALLBACK(BmNewFolderClickCb), self);
    gtk_box_append(GTK_BOX(vbox), newfolder_btn);

    // Sil
    auto* dctx = new BmDelCtx{ self, url };
    GtkWidget* del_btn = gtk_button_new_with_label("Sil");
    gtk_widget_add_css_class(del_btn, "flat");
    gtk_widget_add_css_class(del_btn, "destructive-action");
    gtk_widget_set_halign(del_btn, GTK_ALIGN_START);
    g_object_set_data_full(G_OBJECT(del_btn), "dctx", dctx,
        [](gpointer p){ delete static_cast<BmDelCtx*>(p); });
    g_object_set_data(G_OBJECT(del_btn), "ctx_pop", pop);
    g_signal_connect(del_btn, "clicked", G_CALLBACK(BmDeleteClickCb), nullptr);
    gtk_box_append(GTK_BOX(vbox), del_btn);

    gtk_popover_set_child(GTK_POPOVER(pop), vbox);
    gtk_popover_popup(GTK_POPOVER(pop));
}

// ── Yeni klasör oluştur butonu (BmShowContextMenu'den çağrılır) ───────────────
static void BmNewFolderClickCb(GtkButton* b, gpointer ud2) {
    auto* self2 = static_cast<BrowserWindow*>(ud2);
    auto* url2  = static_cast<std::string*>(g_object_get_data(G_OBJECT(b), "mc-url"));
    GtkWidget* cp = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(b), "ctx_pop"));
    gtk_popover_popdown(GTK_POPOVER(cp));

    GtkWidget* fpop = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(fpop), FALSE);
    gtk_widget_set_parent(fpop, gtk_widget_get_parent(cp));

    GtkWidget* fbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(fbox, 8); gtk_widget_set_margin_end(fbox, 8);
    gtk_widget_set_margin_top(fbox, 8);   gtk_widget_set_margin_bottom(fbox, 8);

    GtkWidget* flbl = gtk_label_new("Yeni klasör adı:");
    gtk_widget_set_halign(flbl, GTK_ALIGN_START);
    GtkWidget* fentry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(fentry), "Klasör adı…");
    gtk_widget_set_size_request(fentry, 190, -1);

    std::string bm_url2 = url2 ? *url2 : "";
    auto* nfctx = new BmNewFolderCtx{ self2, bm_url2 };
    g_object_set_data_full(G_OBJECT(fentry), "nfctx", nfctx,
        [](gpointer p){ delete static_cast<BmNewFolderCtx*>(p); });
    g_object_set_data(G_OBJECT(fentry), "fpop", fpop);
    g_object_set_data(G_OBJECT(fpop), "entry_w", fentry);

    GtkWidget* fsave = gtk_button_new_with_label("Oluştur ve Taşı");
    gtk_widget_add_css_class(fsave, "suggested-action");
    g_object_set_data(G_OBJECT(fsave), "nfctx", nfctx);
    g_object_set_data(G_OBJECT(fsave), "fpop",  fpop);
    g_signal_connect(fsave,  "clicked",  G_CALLBACK(BmNewFolderSaveCb),  nullptr);
    g_signal_connect(fentry, "activate", G_CALLBACK(BmNewFolderEnterCb), nullptr);

    gtk_box_append(GTK_BOX(fbox), flbl);
    gtk_box_append(GTK_BOX(fbox), fentry);
    gtk_box_append(GTK_BOX(fbox), fsave);
    gtk_popover_set_child(GTK_POPOVER(fpop), fbox);
    gtk_popover_popup(GTK_POPOVER(fpop));
}

// ── Kök favori sağ tık ───────────────────────────────────────────────────────
static void BmRightClickCb(GtkGestureClick* g, int, double, double, gpointer ud) {
    auto* self = static_cast<BrowserWindow*>(ud);
    GtkWidget* w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
    const char* url   = static_cast<const char*>(g_object_get_data(G_OBJECT(w), "bm-url"));
    const char* title = static_cast<const char*>(g_object_get_data(G_OBJECT(w), "bm-title"));
    if (!url) return;
    BmShowContextMenu(self, w, std::string(url), title ? title : "");
}

// ── Klasör içi site sağ tık ──────────────────────────────────────────────────
static void BmFolderItemRightClickCb(GtkGestureClick* g, int, double, double, gpointer ud) {
    auto* self = static_cast<BrowserWindow*>(ud);
    GtkWidget* w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
    const char* url   = static_cast<const char*>(g_object_get_data(G_OBJECT(w), "bm-url"));
    const char* title = static_cast<const char*>(g_object_get_data(G_OBJECT(w), "bm-title"));
    if (!url) return;
    // Klasör popover'ı kapat
    GtkWidget* p = gtk_widget_get_parent(w);
    while (p && !GTK_IS_POPOVER(p)) p = gtk_widget_get_parent(p);
    if (p) gtk_popover_popdown(GTK_POPOVER(p));
    // Buton bar'daki klasör menu butonunu bul (grandparent of popover)
    GtkWidget* anchor = p ? gtk_widget_get_parent(p) : w;
    BmShowContextMenu(self, anchor ? anchor : w, std::string(url), title ? title : "");
}

// ── Favoriler bar'ı boş alan sağ tık → klasör oluştur ───────────────────────
static void BmBarRightClickCb(GtkGestureClick* g, int, double, double, gpointer ud) {
    auto* self = static_cast<BrowserWindow*>(ud);
    GtkWidget* bar = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));

    GtkWidget* pop = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(pop), FALSE);
    gtk_widget_set_parent(pop, bar);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(vbox, 8); gtk_widget_set_margin_end(vbox, 8);
    gtk_widget_set_margin_top(vbox, 8);   gtk_widget_set_margin_bottom(vbox, 8);

    GtkWidget* title_lbl = gtk_label_new("Yeni klasör oluştur:");
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Klasör adı…");
    gtk_widget_set_size_request(entry, 190, -1);

    // url="" → sadece klasör adı kaydedilecek (ilerki siteler bu klasöre taşınabilir)
    auto* nfctx = new BmNewFolderCtx{ self, "" };
    g_object_set_data_full(G_OBJECT(entry), "nfctx", nfctx,
        [](gpointer p){ delete static_cast<BmNewFolderCtx*>(p); });
    g_object_set_data(G_OBJECT(entry), "fpop", pop);
    g_object_set_data(G_OBJECT(pop), "entry_w", entry);

    GtkWidget* save_btn = gtk_button_new_with_label("Oluştur");
    gtk_widget_add_css_class(save_btn, "suggested-action");
    g_object_set_data(G_OBJECT(save_btn), "nfctx", nfctx);
    g_object_set_data(G_OBJECT(save_btn), "fpop",  pop);
    g_signal_connect(save_btn, "clicked",  G_CALLBACK(BmNewFolderSaveCb),  nullptr);
    g_signal_connect(entry,    "activate", G_CALLBACK(BmNewFolderEnterCb), nullptr);

    gtk_box_append(GTK_BOX(vbox), title_lbl);
    gtk_box_append(GTK_BOX(vbox), entry);
    gtk_box_append(GTK_BOX(vbox), save_btn);
    gtk_popover_set_child(GTK_POPOVER(pop), vbox);
    gtk_popover_popup(GTK_POPOVER(pop));
}

} // namespace ferman

void BrowserWindow::RebuildBookmarksBar() {
    if (!bookmarks_box_) return;

    // Mevcut widget'ları temizle
    GtkWidget* ch = gtk_widget_get_first_child(bookmarks_box_);
    while (ch) {
        GtkWidget* next = gtk_widget_get_next_sibling(ch);
        gtk_box_remove(GTK_BOX(bookmarks_box_), ch);
        ch = next;
    }

    const auto& bmarks = BookmarkManager::Get().All();

    // Klasörleri topla
    std::vector<std::string> folders;
    for (const auto& bm : bmarks) {
        if (!bm.folder.empty()) {
            bool found = false;
            for (auto& f : folders) if (f == bm.folder) { found = true; break; }
            if (!found) folders.push_back(bm.folder);
        }
    }

    // Klasör butonları (açılır popover)
    for (const auto& folder : folders) {
        std::string flabel = "📁 " + folder;
        GtkWidget* folder_btn = gtk_menu_button_new();
        gtk_menu_button_set_label(GTK_MENU_BUTTON(folder_btn), flabel.c_str());
        gtk_widget_add_css_class(folder_btn, "flat");
        gtk_widget_set_tooltip_text(folder_btn, folder.c_str());

        GtkWidget* pop = gtk_popover_new();
        gtk_popover_set_has_arrow(GTK_POPOVER(pop), TRUE);
        GtkWidget* pop_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_margin_start(pop_box, 6);
        gtk_widget_set_margin_end(pop_box, 6);
        gtk_widget_set_margin_top(pop_box, 6);
        gtk_widget_set_margin_bottom(pop_box, 6);

        for (const auto& bm : bmarks) {
            if (bm.folder != folder) continue;
            std::string bl = bm.title.empty() ? bm.url : bm.title;
            if (bl.size() > 32) bl = bl.substr(0, 30) + "\u2026";
            GtkWidget* item_btn = gtk_button_new_with_label(bl.c_str());
            gtk_widget_add_css_class(item_btn, "flat");
            gtk_widget_set_tooltip_text(item_btn, bm.url.c_str());
            g_object_set_data_full(G_OBJECT(item_btn), "bm-url",
                g_strdup(bm.url.c_str()), g_free);
            g_object_set_data_full(G_OBJECT(item_btn), "bm-title",
                g_strdup((bm.title.empty() ? bm.url : bm.title).c_str()), g_free);
            g_signal_connect(item_btn, "clicked",
                G_CALLBACK(+[](GtkButton* b, gpointer ud) {
                    auto* self = static_cast<BrowserWindow*>(ud);
                    const char* url = static_cast<const char*>(
                        g_object_get_data(G_OBJECT(b), "bm-url"));
                    if (url && self->active_tab_) {
                        webkit_web_view_load_uri(
                            WEBKIT_WEB_VIEW(self->active_tab_->webview), url);
                    }
                    // Popover'ı kapat
                    GtkWidget* w = GTK_WIDGET(b);
                    while (w && !GTK_IS_POPOVER(w)) w = gtk_widget_get_parent(w);
                    if (w) gtk_popover_popdown(GTK_POPOVER(w));
                }), this);
            // Sağ tık: düzenleme menüsü
            GtkGesture* fi_rclick = gtk_gesture_click_new();
            gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(fi_rclick), GDK_BUTTON_SECONDARY);
            g_signal_connect(fi_rclick, "pressed",
                G_CALLBACK(ferman::BmFolderItemRightClickCb), this);
            gtk_widget_add_controller(item_btn, GTK_EVENT_CONTROLLER(fi_rclick));
            gtk_box_append(GTK_BOX(pop_box), item_btn);
        }

        gtk_popover_set_child(GTK_POPOVER(pop), pop_box);
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(folder_btn), pop);
        gtk_box_append(GTK_BOX(bookmarks_box_), folder_btn);
    }

    // Favoriler bar'ına sağ tık → klasör oluştur
    {
        GtkGesture* bar_rclick = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(bar_rclick), GDK_BUTTON_SECONDARY);
        g_signal_connect(bar_rclick, "pressed",
            G_CALLBACK(ferman::BmBarRightClickCb), this);
        gtk_widget_add_controller(bookmarks_box_, GTK_EVENT_CONTROLLER(bar_rclick));
    }

    // Kök seviyesindeki favoriler (klasörsüz)
    for (const auto& bm : bmarks) {
        if (!bm.folder.empty()) continue;

        std::string label = bm.title.empty() ? bm.url : bm.title;
        if (label.size() > 28) label = label.substr(0, 26) + "…";

        GtkWidget* btn = gtk_button_new_with_label(label.c_str());
        gtk_widget_add_css_class(btn, "flat");
        gtk_widget_set_tooltip_text(btn, bm.url.c_str());
        g_object_set_data_full(G_OBJECT(btn), "bm-url",
            g_strdup(bm.url.c_str()), g_free);
        g_object_set_data_full(G_OBJECT(btn), "bm-title",
            g_strdup((bm.title.empty() ? bm.url : bm.title).c_str()), g_free);

        g_signal_connect(btn, "clicked",
            G_CALLBACK(+[](GtkButton* b, gpointer ud) {
                auto* self = static_cast<BrowserWindow*>(ud);
                const char* url = static_cast<const char*>(
                    g_object_get_data(G_OBJECT(b), "bm-url"));
                if (url && self->active_tab_)
                    webkit_web_view_load_uri(
                        WEBKIT_WEB_VIEW(self->active_tab_->webview), url);
            }), this);

        // Sağ tık: bağlam menüsü (BmRightClickCb)
        GtkGesture* rclick = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), GDK_BUTTON_SECONDARY);
        g_signal_connect(rclick, "pressed", G_CALLBACK(ferman::BmRightClickCb), this);
        gtk_widget_add_controller(btn, GTK_EVENT_CONTROLLER(rclick));
        gtk_box_append(GTK_BOX(bookmarks_box_), btn);

    }
}

void BrowserWindow::ToggleBookmarksBar() {
    bookmarks_visible_ = !bookmarks_visible_;
    gtk_revealer_set_reveal_child(
        GTK_REVEALER(bookmarks_bar_), bookmarks_visible_);
}

// ── AI Panel CSS ──────────────────────────────────────────────────────────────
static const char* kAiCSS = R"css(
.ai-btn{background:#f0f0f0;color:#333;border:1px solid #d0d0d0;
border-radius:6px;font-weight:600;font-size:.82rem;padding:2px 10px;margin-left:4px}
.ai-btn:hover{background:#e4e4e4}
.ai-btn.suggested-action{background:#ddeeff;color:#1a6fcc;border-color:#aad0f5}
.ai-panel{background:#ffffff;border-left:1px solid #e0e0e0}
.ai-history-panel{background:#f8f8f8;border-right:1px solid #e0e0e0;min-width:230px}
.ai-history-item{padding:8px 12px;border-bottom:1px solid #efefef;color:#333;font-size:.83rem}
.ai-history-item:hover{background:#eef4ff}
.ai-history-item.selected{background:#ddeeff}
.ai-history-title{font-weight:600;font-size:.83rem;color:#222}
.ai-history-date{font-size:.72rem;color:#999;margin-top:2px}
.ai-bubble-user{background:#ddeeff;border-radius:14px 14px 4px 14px;
padding:9px 14px;margin:4px 8px 4px 48px;color:#1a3a5c;font-size:.88rem;line-height:1.5}
.ai-bubble-ai{background:#f3f3f3;border-radius:14px 14px 14px 4px;
padding:9px 14px;margin:4px 48px 4px 8px;color:#222;font-size:.88rem;line-height:1.5}
.ai-input-area{background:#fafafa;border-top:1px solid #e0e0e0;padding:8px}
.ai-input-view{background:#fff;border:1px solid #d0d0d0;border-radius:8px;
color:#222;font-size:.88rem;padding:6px;caret-color:#333}
.ai-input-view text{color:#222}
.ai-send-btn{background:#1a6fcc;color:#fff;border:none;border-radius:7px;
font-weight:600;padding:6px 16px;font-size:.84rem}
.ai-send-btn:hover{background:#155bb5}
.ai-header{background:#f5f5f5;padding:7px 10px;border-bottom:1px solid #e0e0e0}
.ai-title-lbl{color:#333;font-weight:700;font-size:.88rem}
.ai-chip{background:#e8f0fe;color:#1a4fa0;border:1px solid #c5d8fc;
border-radius:12px;padding:2px 8px;font-size:.78rem;margin:2px}
.ai-chip-remove{background:transparent;border:none;color:#999;
font-size:.75rem;padding:0 3px;min-width:0;border-radius:99px}
.ai-chip-remove:hover{background:#fde8e8;color:#c00}
.ai-filter-bar{background:#f8f8f8;padding:6px 8px;border-bottom:1px solid #e0e0e0}
.ai-loading{color:#888;font-size:.84rem;font-style:italic;padding:6px 12px}
.ai-new-chat-btn{background:#fff;color:#1a6fcc;border:1px solid #c5d8fc;
border-radius:7px;font-size:.82rem;padding:4px 12px}
.ai-new-chat-btn:hover{background:#eef4ff}
.ai-combo-name{font-size:.86rem;font-weight:600;color:#222}
.ai-combo-sub{font-size:.72rem;color:#999;margin-top:0}
)css";

void BrowserWindow::BuildAiPanel() {
    GtkCssProvider* ai_css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(ai_css, kAiCSS);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(ai_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(ai_css);

    ai_outer_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_visible(ai_outer_, FALSE);

    // ── [A] Geçmiş sidebar ──
    ai_history_revealer_ = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(ai_history_revealer_),
        GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT);
    gtk_revealer_set_transition_duration(GTK_REVEALER(ai_history_revealer_), 200);
    gtk_revealer_set_reveal_child(GTK_REVEALER(ai_history_revealer_), FALSE);

    GtkWidget* hist_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(hist_box, "ai-history-panel");

    GtkWidget* hist_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(hist_toolbar, 8);
    gtk_widget_set_margin_end(hist_toolbar, 8);
    gtk_widget_set_margin_top(hist_toolbar, 8);
    gtk_widget_set_margin_bottom(hist_toolbar, 8);

    GtkWidget* new_chat_btn = gtk_button_new_with_label("✏ Yeni");
    gtk_widget_add_css_class(new_chat_btn, "ai-new-chat-btn");
    gtk_widget_set_hexpand(new_chat_btn, TRUE);
    g_signal_connect(new_chat_btn, "clicked", G_CALLBACK(OnAiNewChatCb), this);

    GtkWidget* filter_toggle_btn = gtk_button_new_from_icon_name("system-search-symbolic");
    gtk_widget_add_css_class(filter_toggle_btn, "flat");
    gtk_widget_set_tooltip_text(filter_toggle_btn, "Filtrele");
    g_signal_connect(filter_toggle_btn, "clicked", G_CALLBACK(OnAiToggleFilterCb), this);
    gtk_box_append(GTK_BOX(hist_toolbar), new_chat_btn);
    gtk_box_append(GTK_BOX(hist_toolbar), filter_toggle_btn);
    gtk_box_append(GTK_BOX(hist_box), hist_toolbar);
    gtk_box_append(GTK_BOX(hist_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // Filtre revealer
    ai_filter_revealer_ = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(ai_filter_revealer_),
        GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(GTK_REVEALER(ai_filter_revealer_), 180);
    gtk_revealer_set_reveal_child(GTK_REVEALER(ai_filter_revealer_), FALSE);

    GtkWidget* filter_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(filter_box, "ai-filter-bar");

    ai_search_entry_ = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(ai_search_entry_), "Sohbetlerde ara...");
    g_signal_connect(ai_search_entry_, "search-changed", G_CALLBACK(OnAiSearchChangedCb), this);

    const char* date_items[] = {"Tümü","Bugün","Bu Hafta","Bu Ay", nullptr};
    GtkStringList* date_model = gtk_string_list_new(date_items);
    ai_date_filter_ = gtk_drop_down_new(G_LIST_MODEL(date_model), nullptr);
    g_signal_connect(ai_date_filter_, "notify::selected", G_CALLBACK(OnAiDateFilterCb), this);

    gtk_box_append(GTK_BOX(filter_box), ai_search_entry_);
    gtk_box_append(GTK_BOX(filter_box), ai_date_filter_);
    gtk_revealer_set_child(GTK_REVEALER(ai_filter_revealer_), filter_box);
    gtk_box_append(GTK_BOX(hist_box), ai_filter_revealer_);
    gtk_box_append(GTK_BOX(hist_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget* list_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(list_scroll, TRUE);
    ai_chat_list_ = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ai_chat_list_), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(ai_chat_list_), TRUE);
    g_signal_connect(ai_chat_list_, "row-activated",
        G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer ud) {
            auto* self = static_cast<BrowserWindow*>(ud);
            int64_t id = (int64_t)(gintptr)g_object_get_data(G_OBJECT(row), "chat-id");
            if (id) self->LoadAiChat(id);
        }), this);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(list_scroll), ai_chat_list_);
    gtk_box_append(GTK_BOX(hist_box), list_scroll);
    gtk_revealer_set_child(GTK_REVEALER(ai_history_revealer_), hist_box);
    gtk_box_append(GTK_BOX(ai_outer_), ai_history_revealer_);
    gtk_box_append(GTK_BOX(ai_outer_), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

    // ── [B] Sohbet alanı ──
    GtkWidget* chat_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(chat_panel, "ai-panel");
    gtk_widget_set_size_request(chat_panel, 380, -1);
    gtk_widget_set_vexpand(chat_panel, TRUE);

    GtkWidget* ai_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(ai_header, "ai-header");

    ai_history_btn_ = gtk_button_new_from_icon_name("view-list-symbolic");
    gtk_widget_add_css_class(ai_history_btn_, "flat");
    gtk_widget_set_tooltip_text(ai_history_btn_, "Sohbet geçmişi");
    g_signal_connect(ai_history_btn_, "clicked", G_CALLBACK(OnAiToggleHistoryCb), this);

    ai_title_label_ = gtk_label_new("AI Asistan");
    gtk_widget_add_css_class(ai_title_label_, "ai-title-lbl");
    gtk_widget_set_hexpand(ai_title_label_, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(ai_title_label_), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(ai_title_label_), 0.0f);

    GtkWidget* close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(close_btn, "flat");
    g_signal_connect(close_btn, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer ud){ static_cast<BrowserWindow*>(ud)->ToggleAiPanel(); }), this);

    gtk_box_append(GTK_BOX(ai_header), ai_history_btn_);
    gtk_box_append(GTK_BOX(ai_header), ai_title_label_);
    gtk_box_append(GTK_BOX(ai_header), close_btn);
    gtk_box_append(GTK_BOX(chat_panel), ai_header);
    gtk_box_append(GTK_BOX(chat_panel), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    ai_chat_scroll_ = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(ai_chat_scroll_),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(ai_chat_scroll_, TRUE);
    ai_chat_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(ai_chat_box_, 8);
    gtk_widget_set_margin_bottom(ai_chat_box_, 8);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(ai_chat_scroll_), ai_chat_box_);
    gtk_box_append(GTK_BOX(chat_panel), ai_chat_scroll_);

    ai_loading_label_ = gtk_label_new("⋯ Yanıt bekleniyor...");
    gtk_widget_add_css_class(ai_loading_label_, "ai-loading");
    gtk_widget_set_halign(ai_loading_label_, GTK_ALIGN_START);
    gtk_widget_set_visible(ai_loading_label_, FALSE);
    gtk_box_append(GTK_BOX(chat_panel), ai_loading_label_);
    gtk_box_append(GTK_BOX(chat_panel), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget* input_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(input_area, "ai-input-area");

    ai_attach_box_ = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(ai_attach_box_), GTK_SELECTION_NONE);
    gtk_widget_set_visible(ai_attach_box_, FALSE);
    gtk_box_append(GTK_BOX(input_area), ai_attach_box_);

    ai_input_buffer_ = GTK_WIDGET(gtk_text_buffer_new(nullptr));
    g_signal_connect(ai_input_buffer_, "changed", G_CALLBACK(OnAiInputChangedCb), this);
    ai_input_ = gtk_text_view_new_with_buffer(GTK_TEXT_BUFFER(ai_input_buffer_));
    gtk_widget_add_css_class(ai_input_, "ai-input-view");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ai_input_), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(ai_input_), FALSE);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(ai_input_), TRUE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(ai_input_), TRUE);
    gtk_widget_set_focusable(ai_input_, TRUE);
    gtk_widget_set_can_focus(ai_input_, TRUE);
    gtk_widget_set_hexpand(ai_input_, TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(ai_input_), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(ai_input_), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(ai_input_), 6);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(ai_input_), 6);
    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(OnAiKeyPressCb), this);
    gtk_widget_add_controller(ai_input_, key_ctrl);
    // ScrolledWindow zorunlu — GTK4'te standalone GtkTextView klavye input almaz
    GtkWidget* input_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(input_scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(input_scroll), TRUE);
    gtk_widget_set_size_request(input_scroll, -1, 90);
    gtk_widget_set_hexpand(input_scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(input_scroll), ai_input_);

    // Dosya sürükle-bırak desteği
    GtkDropTarget* drop_target = gtk_drop_target_new(G_TYPE_INVALID, GDK_ACTION_COPY);
    GType drop_types[] = { GDK_TYPE_FILE_LIST, G_TYPE_FILE };
    gtk_drop_target_set_gtypes(drop_target, drop_types, 2);
    g_object_set_data(G_OBJECT(drop_target), "bw", this);
    g_signal_connect(drop_target, "drop",
        G_CALLBACK(+[](GtkDropTarget* dt, const GValue* val, double, double, gpointer) -> gboolean {
            auto* self = static_cast<BrowserWindow*>(g_object_get_data(G_OBJECT(dt), "bw"));
            if (!self) return FALSE;
            if (G_VALUE_HOLDS(val, GDK_TYPE_FILE_LIST)) {
                auto* list = static_cast<GSList*>(g_value_get_boxed(val));
                for (GSList* l = list; l; l = l->next) {
                    char* path = g_file_get_path(G_FILE(l->data));
                    if (path) { self->AddAttachmentChip(path); g_free(path); }
                }
                return TRUE;
            }
            if (G_VALUE_HOLDS(val, G_TYPE_FILE)) {
                GFile* file = G_FILE(g_value_get_object(val));
                char* path = g_file_get_path(file);
                if (path) { self->AddAttachmentChip(path); g_free(path); }
                g_free(path);
                return TRUE;
            }
            return FALSE;
        }), nullptr);
    gtk_widget_add_controller(input_scroll, GTK_EVENT_CONTROLLER(drop_target));

    gtk_box_append(GTK_BOX(input_area), input_scroll);

    // Ajan combo (btn_row içine taşındı)
    ai_agent_combo_ = gtk_drop_down_new(nullptr, nullptr);
    gtk_widget_set_hexpand(ai_agent_combo_, TRUE);
    gtk_widget_set_size_request(ai_agent_combo_, -1, 28);
    gtk_widget_set_tooltip_text(ai_agent_combo_, "Kullanılacak yapay zeka ajanını seç");
    UpdateAiAgentCombo();

    // Autocomplete popover (@ tamamlama)
    ai_autocomplete_pop_ = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(ai_autocomplete_pop_), FALSE);
    gtk_widget_set_parent(ai_autocomplete_pop_, input_scroll);
    gtk_popover_set_position(GTK_POPOVER(ai_autocomplete_pop_), GTK_POS_TOP);
    ai_autocomplete_list_ = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ai_autocomplete_list_), GTK_SELECTION_BROWSE);
    gtk_popover_set_child(GTK_POPOVER(ai_autocomplete_pop_), ai_autocomplete_list_);
    {
        static auto autocomplete_cb = [](GtkListBox*, GtkListBoxRow* row, gpointer ud) {
            auto* self = static_cast<BrowserWindow*>(ud);
            const char* name = static_cast<const char*>(
                g_object_get_data(G_OBJECT(row), "agent-name"));
            if (!name || !self->ai_input_buffer_) return;
            GtkTextBuffer* buf = GTK_TEXT_BUFFER(self->ai_input_buffer_);
            GtkTextIter s2, e2;
            gtk_text_buffer_get_bounds(buf, &s2, &e2);
            gchar* txt = gtk_text_buffer_get_text(buf, &s2, &e2, FALSE);
            std::string t = txt ? txt : "";
            g_free(txt);
            auto at_pos = t.rfind('@');
            if (at_pos != std::string::npos) {
                std::string nt = t.substr(0, at_pos) + "@" + name + " ";
                gtk_text_buffer_set_text(buf, nt.c_str(), (int)nt.size());
                GtkTextIter ei;
                gtk_text_buffer_get_end_iter(buf, &ei);
                gtk_text_buffer_place_cursor(buf, &ei);
            }
            gtk_popover_popdown(GTK_POPOVER(self->ai_autocomplete_pop_));
            gtk_widget_grab_focus(self->ai_input_);
        };
        g_signal_connect(ai_autocomplete_list_, "row-activated",
            G_CALLBACK(static_cast<void(*)(GtkListBox*, GtkListBoxRow*, gpointer)>(autocomplete_cb)),
            this);
    }

    GtkWidget* btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* attach_btn = gtk_button_new_from_icon_name("mail-attachment-symbolic");
    gtk_widget_add_css_class(attach_btn, "flat");
    gtk_widget_set_tooltip_text(attach_btn, "Dosya ekle");
    g_signal_connect(attach_btn, "clicked", G_CALLBACK(OnAiAttachCb), this);
    GtkWidget* send_btn = gtk_button_new_with_label("\xe2\x9e\xa4 G\xc3\xb6nder");
    gtk_widget_add_css_class(send_btn, "ai-send-btn");
    g_signal_connect(send_btn, "clicked", G_CALLBACK(OnAiSendCb), this);
    gtk_box_append(GTK_BOX(btn_row), attach_btn);
    gtk_box_append(GTK_BOX(btn_row), ai_agent_combo_);
    gtk_box_append(GTK_BOX(btn_row), send_btn);
    gtk_box_append(GTK_BOX(input_area), btn_row);
    gtk_box_append(GTK_BOX(chat_panel), input_area);
    gtk_box_append(GTK_BOX(ai_outer_), chat_panel);

    RefreshAiChatList();
}

void BrowserWindow::ToggleAiPanel() {
    ai_panel_visible_ = !ai_panel_visible_;
    gtk_widget_set_visible(ai_outer_, ai_panel_visible_);
    if (ai_btn_) {
        if (ai_panel_visible_)
            gtk_widget_add_css_class(ai_btn_, "suggested-action");
        else
            gtk_widget_remove_css_class(ai_btn_, "suggested-action");
    }
}

void BrowserWindow::ToggleAiHistory() {
    ai_history_visible_ = !ai_history_visible_;
    gtk_revealer_set_reveal_child(GTK_REVEALER(ai_history_revealer_), ai_history_visible_);
}

void BrowserWindow::ToggleAiFilter() {
    ai_filter_visible_ = !ai_filter_visible_;
    gtk_revealer_set_reveal_child(GTK_REVEALER(ai_filter_revealer_), ai_filter_visible_);
    if (ai_filter_visible_) gtk_widget_grab_focus(ai_search_entry_);
}

void BrowserWindow::RefreshAiChatList() {
    if (!ai_chat_list_) return;
    GtkWidget* child = gtk_widget_get_first_child(ai_chat_list_);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(ai_chat_list_), child);
        child = next;
    }
    AiChatFilter filter;
    if (ai_search_entry_) {
        const char* kw = gtk_editable_get_text(GTK_EDITABLE(ai_search_entry_));
        if (kw) filter.keyword = kw;
    }
    if (ai_date_filter_) {
        guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(ai_date_filter_));
        time_t now = time(nullptr);
        if (sel == 1) {
            struct tm* t = localtime(&now);
            t->tm_hour = 0; t->tm_min = 0; t->tm_sec = 0;
            filter.from_ts = (int64_t)mktime(t);
        } else if (sel == 2) {
            filter.from_ts = (int64_t)(now - 7 * 24 * 3600);
        } else if (sel == 3) {
            filter.from_ts = (int64_t)(now - 30 * 24 * 3600);
        }
    }
    auto chats = AiManager::Get().ListChats(filter);
    int idx = 0;
    for (const auto& chat : chats) {
        // Dış kutu: sol bilgi + sil butonu yan yana
        GtkWidget* outer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_widget_set_margin_start(outer, 4);
        gtk_widget_set_margin_end(outer, 2);
        gtk_widget_set_margin_top(outer, 2);
        gtk_widget_set_margin_bottom(outer, 2);

        // Sol: başlık + tarih + ID
        GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_add_css_class(row_box, "ai-history-item");
        gtk_widget_set_hexpand(row_box, TRUE);

        // Başlık + kısa sıra no aynı satırda
        GtkWidget* title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        GtkWidget* title_lbl = gtk_label_new(chat.title.c_str());
        gtk_widget_add_css_class(title_lbl, "ai-history-title");
        gtk_label_set_ellipsize(GTK_LABEL(title_lbl), PANGO_ELLIPSIZE_END);
        gtk_label_set_xalign(GTK_LABEL(title_lbl), 0.0f);
        gtk_widget_set_hexpand(title_lbl, TRUE);
        std::string id_str = "#" + std::to_string(idx + 1);
        GtkWidget* id_lbl = gtk_label_new(id_str.c_str());
        gtk_widget_add_css_class(id_lbl, "ai-history-date");
        gtk_widget_set_halign(id_lbl, GTK_ALIGN_END);
        gtk_box_append(GTK_BOX(title_row), title_lbl);
        gtk_box_append(GTK_BOX(title_row), id_lbl);

        char date_buf[32];
        struct tm* t = localtime((const time_t*)&chat.created_at);
        strftime(date_buf, sizeof(date_buf), "%d.%m.%Y %H:%M", t);
        std::string date_str = std::string(date_buf) + " \xc2\xb7 " +
            std::to_string(chat.messages.size()) + " mesaj";
        GtkWidget* date_lbl = gtk_label_new(date_str.c_str());
        gtk_widget_add_css_class(date_lbl, "ai-history-date");
        gtk_label_set_xalign(GTK_LABEL(date_lbl), 0.0f);

        gtk_box_append(GTK_BOX(row_box), title_row);
        gtk_box_append(GTK_BOX(row_box), date_lbl);

        // Sil butonu
        GtkWidget* del_btn = gtk_button_new_from_icon_name("user-trash-symbolic");
        gtk_widget_add_css_class(del_btn, "flat");
        gtk_widget_add_css_class(del_btn, "circular");
        gtk_widget_set_tooltip_text(del_btn, "Sohbeti sil");
        gtk_widget_set_valign(del_btn, GTK_ALIGN_CENTER);
        g_object_set_data(G_OBJECT(del_btn), "chat-id", (gpointer)(gintptr)chat.id);
        g_object_set_data(G_OBJECT(del_btn), "bw", this);
        g_signal_connect(del_btn, "clicked",
            G_CALLBACK(+[](GtkButton* b, gpointer) {
                auto* self = static_cast<BrowserWindow*>(g_object_get_data(G_OBJECT(b), "bw"));
                int64_t cid = (int64_t)(gintptr)g_object_get_data(G_OBJECT(b), "chat-id");
                AiManager::Get().DeleteChat(cid);
                if (self->ai_current_chat_id_ == cid) {
                    // Silinen aktif sohbetse yeni sohbet başlat
                    const auto& p = SettingsManager::Get().Prefs();
                    self->ai_current_chat_id_ = AiManager::Get().NewChat(p.ai_provider, p.ai_model);
                    self->ai_current_chat_ = AiManager::Get().LoadChat(self->ai_current_chat_id_);
                    if (self->ai_title_label_)
                        gtk_label_set_text(GTK_LABEL(self->ai_title_label_), "AI Asistan");
                    GtkWidget* ch = gtk_widget_get_first_child(self->ai_chat_box_);
                    while (ch) {
                        GtkWidget* nx = gtk_widget_get_next_sibling(ch);
                        gtk_box_remove(GTK_BOX(self->ai_chat_box_), ch);
                        ch = nx;
                    }
                }
                self->RefreshAiChatList();
            }), nullptr);

        gtk_box_append(GTK_BOX(outer), row_box);
        gtk_box_append(GTK_BOX(outer), del_btn);

        if (chat.id == ai_current_chat_id_)
            gtk_widget_add_css_class(row_box, "selected");
        GtkWidget* row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), outer);
        g_object_set_data(G_OBJECT(row), "chat-id", (gpointer)(gintptr)chat.id);
        gtk_list_box_append(GTK_LIST_BOX(ai_chat_list_), row);
        ++idx;
    }
}

void BrowserWindow::LoadAiChat(int64_t id) {
    ai_current_chat_id_ = id;
    ai_current_chat_ = AiManager::Get().LoadChat(id);
    if (ai_title_label_)
        gtk_label_set_text(GTK_LABEL(ai_title_label_), ai_current_chat_.title.c_str());
    GtkWidget* child = gtk_widget_get_first_child(ai_chat_box_);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(ai_chat_box_), child);
        child = next;
    }
    for (const auto& m : ai_current_chat_.messages) {
        if (m.role == "system") continue;
        AppendAiBubble(m.role, m.content);
    }
    RefreshAiChatList();
    g_idle_add([](gpointer ud) -> gboolean {
        auto* scroll = GTK_SCROLLED_WINDOW(ud);
        GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(scroll);
        gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));
        return G_SOURCE_REMOVE;
    }, ai_chat_scroll_);
}

// Pango markup için özel karakterleri escape et
static std::string pango_esc(const std::string& s) {
    std::string o; o.reserve(s.size() + 8);
    for (char c : s) {
        if      (c == '&')  o += "&amp;";
        else if (c == '<')  o += "&lt;";
        else if (c == '>')  o += "&gt;";
        else                o += c;
    }
    return o;
}

// Satır içi markdown → Pango markup (bold, italic, code)
static std::string inline_md(const std::string& s) {
    std::string o;
    size_t i = 0;
    while (i < s.size()) {
        // ``` inline kod (tek backtick)
        if (s[i] == '`') {
            size_t end = s.find('`', i + 1);
            if (end != std::string::npos) {
                o += "<tt><span background=\"#2a2a2a\" foreground=\"#e0e0e0\">";
                o += pango_esc(s.substr(i + 1, end - i - 1));
                o += "</span></tt>";
                i = end + 1;
                continue;
            }
        }
        // **bold**
        if (i + 1 < s.size() && s[i] == '*' && s[i+1] == '*') {
            size_t end = s.find("**", i + 2);
            if (end != std::string::npos) {
                o += "<b>" + pango_esc(s.substr(i + 2, end - i - 2)) + "</b>";
                i = end + 2;
                continue;
            }
        }
        // *italic* (tek yıldız)
        if (s[i] == '*') {
            size_t end = s.find('*', i + 1);
            if (end != std::string::npos && end > i + 1) {
                o += "<i>" + pango_esc(s.substr(i + 1, end - i - 1)) + "</i>";
                i = end + 1;
                continue;
            }
        }
        // __bold__
        if (i + 1 < s.size() && s[i] == '_' && s[i+1] == '_') {
            size_t end = s.find("__", i + 2);
            if (end != std::string::npos) {
                o += "<b>" + pango_esc(s.substr(i + 2, end - i - 2)) + "</b>";
                i = end + 2;
                continue;
            }
        }
        o += pango_esc(std::string(1, s[i]));
        ++i;
    }
    return o;
}

// Markdown metni → Pango markup
static std::string md_to_pango(const std::string& md) {
    std::istringstream ss(md);
    std::string line, out;
    bool in_code = false;
    while (std::getline(ss, line)) {
        // Kod bloğu ```
        if (line.rfind("```", 0) == 0) {
            in_code = !in_code;
            if (in_code)
                out += "<span background=\"#1e1e2e\" foreground=\"#cdd6f4\" font_family=\"monospace\">";
            else
                out += "</span>";
            out += '\n';
            continue;
        }
        if (in_code) {
            out += pango_esc(line) + '\n';
            continue;
        }
        // Yatay çizgi ---
        if (line == "---" || line == "***" || line == "___") {
            out += "<span color=\"#585b70\">\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80</span>\n";
            continue;
        }
        // ### Başlık
        if (line.rfind("### ", 0) == 0)
            out += "<b><span size=\"medium\">" + inline_md(line.substr(4)) + "</span></b>\n";
        else if (line.rfind("## ", 0) == 0)
            out += "<b><span size=\"large\">" + inline_md(line.substr(3)) + "</span></b>\n";
        else if (line.rfind("# ", 0) == 0)
            out += "<b><span size=\"x-large\">" + inline_md(line.substr(2)) + "</span></b>\n";
        // > alıntı
        else if (line.rfind("> ", 0) == 0)
            out += "<i><span color=\"#a6adc8\">" + inline_md(line.substr(2)) + "</span></i>\n";
        // - liste
        else if ((line.rfind("- ", 0) == 0) || (line.rfind("* ", 0) == 0 && line.size() > 2))
            out += "  \xe2\x80\xa2 " + inline_md(line.substr(2)) + "\n";
        // Numaralı liste
        else if (line.size() > 2 && std::isdigit((unsigned char)line[0]) && line[1] == '.') {
            size_t sp = line.find(". ");
            if (sp != std::string::npos)
                out += "  " + pango_esc(line.substr(0, sp + 2)) + inline_md(line.substr(sp + 2)) + "\n";
            else
                out += inline_md(line) + "\n";
        }
        else
            out += inline_md(line) + "\n";
    }
    if (in_code) out += "</span>"; // kapatılmamış kod bloğu
    // Sondaki fazla newline temizle
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

void BrowserWindow::AppendAiBubble(const std::string& role, const std::string& text) {
    GtkWidget* bubble;
    if (role == "user") {
        bubble = gtk_label_new(text.c_str());
        gtk_label_set_wrap(GTK_LABEL(bubble), TRUE);
        gtk_label_set_wrap_mode(GTK_LABEL(bubble), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_selectable(GTK_LABEL(bubble), TRUE);
        gtk_label_set_xalign(GTK_LABEL(bubble), 0.0f);
        gtk_widget_add_css_class(bubble, "ai-bubble-user");
        gtk_widget_set_halign(bubble, GTK_ALIGN_FILL);
    } else {
        // AI yanıtı: markdown → Pango markup
        std::string markup = md_to_pango(text);
        bubble = gtk_label_new(nullptr);
        gtk_label_set_markup(GTK_LABEL(bubble), markup.c_str());
        gtk_label_set_wrap(GTK_LABEL(bubble), TRUE);
        gtk_label_set_wrap_mode(GTK_LABEL(bubble), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_selectable(GTK_LABEL(bubble), TRUE);
        gtk_label_set_xalign(GTK_LABEL(bubble), 0.0f);
        gtk_widget_add_css_class(bubble, "ai-bubble-ai");
        gtk_widget_set_halign(bubble, GTK_ALIGN_FILL);
    }
    gtk_box_append(GTK_BOX(ai_chat_box_), bubble);
    g_idle_add([](gpointer ud) -> gboolean {
        auto* scroll = GTK_SCROLLED_WINDOW(ud);
        GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(scroll);
        gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));
        return G_SOURCE_REMOVE;
    }, ai_chat_scroll_);
}

void BrowserWindow::ShowAiLoading(bool show) {
    if (ai_loading_label_) gtk_widget_set_visible(ai_loading_label_, show);
}

// ── Hızlı AI popup (Çeviri / Özet) ───────────────────────────────────────────
void BrowserWindow::ShowAiQuickPopup(const std::string& title,
                                      const std::string& prompt) {
    // Aktif ajanı al
    const auto& agents = AiAgentStore::Get().Agents();
    std::string api_key, api_url, model, provider;
    if (!agents.empty()) {
        guint sel = ai_agent_combo_
            ? gtk_drop_down_get_selected(GTK_DROP_DOWN(ai_agent_combo_)) : 0;
        if (sel == GTK_INVALID_LIST_POSITION || sel >= agents.size()) sel = 0;
        api_key  = agents[sel].api_key;
        api_url  = agents[sel].api_url;
        model    = agents[sel].model;
    } else {
        const auto& p = SettingsManager::Get().Prefs();
        api_key = p.ai_api_key;
        api_url = p.ai_base_url;
        model   = p.ai_model;
    }
    provider = AiAgent::DetectProvider(api_key);

    if (api_key.empty()) {
        // Ajan yoksa popup ile uyar
        GtkWidget* dlg = gtk_message_dialog_new(
            GTK_WINDOW(window_), GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "AI özelliği için önce Ayarlar > Yapay Zeka bölümünden bir ajan ekleyin.");
        g_signal_connect(dlg, "response",
            G_CALLBACK(+[](GtkDialog* d, int, gpointer){ gtk_window_destroy(GTK_WINDOW(d)); }),
            nullptr);
        gtk_window_present(GTK_WINDOW(dlg));
        return;
    }

    // Popup pencere: başlık + yükleniyor label → cevap gelince güncellenir
    GtkWidget* win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), title.c_str());
    gtk_window_set_default_size(GTK_WINDOW(win), 520, 400);
    gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(window_));
    gtk_window_set_modal(GTK_WINDOW(win), FALSE);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(vbox, 16); gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 12);   gtk_widget_set_margin_bottom(vbox, 12);

    // Başlık
    GtkWidget* title_lbl = gtk_label_new(title.c_str());
    gtk_widget_add_css_class(title_lbl, "title-4");
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), title_lbl);
    gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // Scroll + text view (cevap buraya yazılacak)
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget* text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_widget_set_margin_start(text_view, 4); gtk_widget_set_margin_end(text_view, 4);
    GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_text_buffer_set_text(buf, "Yapay zeka düşünüyor…", -1);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), text_view);
    gtk_box_append(GTK_BOX(vbox), scroll);

    // Kapat butonu
    GtkWidget* close_btn = gtk_button_new_with_label("Kapat");
    gtk_widget_set_halign(close_btn, GTK_ALIGN_END);
    g_signal_connect(close_btn, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer ud){
            gtk_window_destroy(GTK_WINDOW(ud));
        }), win);
    gtk_box_append(GTK_BOX(vbox), close_btn);

    gtk_window_set_child(GTK_WINDOW(win), vbox);
    gtk_window_present(GTK_WINDOW(win));

    // AI isteği gönder — tmp_chat heap'te, callback bitince silinir
    struct QuickCtx {
        GtkTextBuffer* buf;
        GtkWidget*     win;  // weak ref ile sıfırlanır
        AiChat         chat; // referans kaybolmasın
        std::string    result;
    };
    auto* ctx = new QuickCtx{};
    ctx->buf  = buf;
    ctx->win  = win;
    ctx->chat.provider = provider;
    ctx->chat.model    = model;
    AiMessage qmsg;
    qmsg.role    = "user";
    qmsg.content = prompt;
    ctx->chat.messages.push_back(qmsg);

    g_object_add_weak_pointer(G_OBJECT(win), (gpointer*)&ctx->win);

    AiManager::Get().SendMessage(
        ctx->chat, provider, model, api_key, api_url,
        [ctx](const std::string& content, bool /*done*/, const std::string& err) {
            ctx->result = err.empty() ? content : ("⚠ Hata: " + err);
            // GTK çağrıları main thread'de yap
            g_idle_add([](gpointer ud) -> gboolean {
                auto* c = static_cast<QuickCtx*>(ud);
                if (c->win)
                    gtk_text_buffer_set_text(c->buf, c->result.c_str(), -1);
                delete c;
                return G_SOURCE_REMOVE;
            }, ctx);
        });
}

std::string BrowserWindow::CollectAiInputText() {
    if (!ai_input_buffer_) return {};
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(ai_input_buffer_), &s, &e);
    char* txt = gtk_text_iter_get_text(&s, &e);
    std::string result = txt ? txt : "";
    g_free(txt);
    return result;
}

void BrowserWindow::ParseAndHighlightTokens() {
    if (!ai_input_buffer_) return;
    GtkTextBuffer* buf = GTK_TEXT_BUFFER(ai_input_buffer_);
    GtkTextTagTable* tbl = gtk_text_buffer_get_tag_table(buf);
    if (!gtk_text_tag_table_lookup(tbl, "at-hl"))
        gtk_text_buffer_create_tag(buf, "at-hl",
            "background", "#2b3d52", "foreground", "#89b4fa", nullptr);
    if (!gtk_text_tag_table_lookup(tbl, "hash-hl"))
        gtk_text_buffer_create_tag(buf, "hash-hl",
            "background", "#2a3d2a", "foreground", "#a6e3a1", nullptr);
    if (!gtk_text_tag_table_lookup(tbl, "star-hl"))
        gtk_text_buffer_create_tag(buf, "star-hl",
            "background", "#3d3020", "foreground", "#fab387", nullptr);
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buf, &s, &e);
    gtk_text_buffer_remove_all_tags(buf, &s, &e);
    gchar* raw = gtk_text_iter_get_text(&s, &e);
    if (!raw) return;
    std::string text(raw);
    g_free(raw);
    size_t i = 0;
    while (i < text.size()) {
        char c = text[i];
        if (c == '@' || c == '#' || c == '*') {
            size_t j = i + 1;
            while (j < text.size() && !std::isspace((unsigned char)text[j]) &&
                   text[j] != '@' && text[j] != '#' && text[j] != '*') ++j;
            if (j > i + 1) {
                const char* tag = (c == '@') ? "at-hl" : (c == '#') ? "hash-hl" : "star-hl";
                GtkTextIter ts, te;
                gtk_text_buffer_get_iter_at_offset(buf, &ts, (int)i);
                gtk_text_buffer_get_iter_at_offset(buf, &te, (int)j);
                gtk_text_buffer_apply_tag_by_name(buf, tag, &ts, &te);
            }
            i = j;
        } else { ++i; }
    }
    // Son token @ ile başlıyorsa autocomplete göster
    {
        auto at_pos = text.rfind('@');
        if (at_pos != std::string::npos) {
            // @ sonrası boşluk yoksa (hâlâ yazıyor)
            std::string suffix = text.substr(at_pos + 1);
            bool has_space = suffix.find_first_of(" \t\n") != std::string::npos;
            if (!has_space)
                ShowAgentAutocomplete(suffix);
            else
                HideAgentAutocomplete();
        } else {
            HideAgentAutocomplete();
        }
    }
}

void BrowserWindow::UpdateAiAgentCombo() {
    if (!ai_agent_combo_) return;
    const auto& agents = AiAgentStore::Get().Agents();
    if (agents.empty()) {
        GtkStringList* sl = gtk_string_list_new(nullptr);
        gtk_string_list_append(sl, "Ajan yok\nAyarlar'dan ekle");
        gtk_drop_down_set_model(GTK_DROP_DOWN(ai_agent_combo_), G_LIST_MODEL(sl));
        g_object_unref(sl);
        gtk_widget_set_sensitive(ai_agent_combo_, FALSE);
        // Boş durum için de factory kur
    } else {
        gtk_widget_set_sensitive(ai_agent_combo_, TRUE);
        std::vector<std::string> items;
        for (const auto& a : agents) {
            std::string prov = AiAgent::DetectProvider(a.api_key);
            // Format: "AjanIsmi\nprovider · model"
            items.push_back(a.name + "\n" + prov + "  \xc2\xb7  " + a.model);
        }
        std::vector<const char*> ptrs;
        for (const auto& s : items) ptrs.push_back(s.c_str());
        ptrs.push_back(nullptr);
        GtkStringList* sl = gtk_string_list_new(ptrs.data());
        gtk_drop_down_set_model(GTK_DROP_DOWN(ai_agent_combo_), G_LIST_MODEL(sl));
        g_object_unref(sl);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(ai_agent_combo_), 0);
    }

    // Custom factory: üst satır büyük (ajan ismi), alt satır küçük gri (provider · model)
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup",
        G_CALLBACK(+[](GtkListItemFactory*, GtkListItem* item, gpointer) {
            GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
            gtk_widget_set_margin_top(box, 2);
            gtk_widget_set_margin_bottom(box, 2);
            GtkWidget* name_lbl = gtk_label_new("");
            gtk_label_set_xalign(GTK_LABEL(name_lbl), 0.0f);
            gtk_widget_add_css_class(name_lbl, "ai-combo-name");
            GtkWidget* sub_lbl = gtk_label_new("");
            gtk_label_set_xalign(GTK_LABEL(sub_lbl), 0.0f);
            gtk_widget_add_css_class(sub_lbl, "ai-combo-sub");
            gtk_box_append(GTK_BOX(box), name_lbl);
            gtk_box_append(GTK_BOX(box), sub_lbl);
            gtk_list_item_set_child(item, box);
        }), nullptr);
    g_signal_connect(factory, "bind",
        G_CALLBACK(+[](GtkListItemFactory*, GtkListItem* item, gpointer) {
            GtkWidget* box = gtk_list_item_get_child(item);
            if (!box) return;
            GtkWidget* name_lbl = gtk_widget_get_first_child(box);
            GtkWidget* sub_lbl  = name_lbl ? gtk_widget_get_next_sibling(name_lbl) : nullptr;
            auto* str_obj = GTK_STRING_OBJECT(gtk_list_item_get_item(item));
            if (!str_obj) return;
            const char* full = gtk_string_object_get_string(str_obj);
            if (!full) return;
            std::string s(full);
            auto nl = s.find('\n');
            std::string name = (nl != std::string::npos) ? s.substr(0, nl) : s;
            std::string sub  = (nl != std::string::npos) ? s.substr(nl + 1) : "";
            if (name_lbl) gtk_label_set_text(GTK_LABEL(name_lbl), name.c_str());
            if (sub_lbl)  gtk_label_set_text(GTK_LABEL(sub_lbl),  sub.c_str());
        }), nullptr);
    gtk_drop_down_set_factory(GTK_DROP_DOWN(ai_agent_combo_), factory);
    g_object_unref(factory);

    // Seçili butonda da aynı factory (list_factory = liste açıkken, factory = kapalıyken)
    GtkListItemFactory* btn_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(btn_factory, "setup",
        G_CALLBACK(+[](GtkListItemFactory*, GtkListItem* item, gpointer) {
            GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
            GtkWidget* name_lbl = gtk_label_new("");
            gtk_label_set_xalign(GTK_LABEL(name_lbl), 0.0f);
            gtk_widget_set_hexpand(name_lbl, TRUE);
            gtk_widget_add_css_class(name_lbl, "ai-combo-name");
            GtkWidget* sub_lbl = gtk_label_new("");
            gtk_label_set_xalign(GTK_LABEL(sub_lbl), 0.0f);
            gtk_widget_add_css_class(sub_lbl, "ai-combo-sub");
            gtk_box_append(GTK_BOX(box), name_lbl);
            gtk_box_append(GTK_BOX(box), sub_lbl);
            gtk_list_item_set_child(item, box);
        }), nullptr);
    g_signal_connect(btn_factory, "bind",
        G_CALLBACK(+[](GtkListItemFactory*, GtkListItem* item, gpointer) {
            GtkWidget* box = gtk_list_item_get_child(item);
            if (!box) return;
            GtkWidget* name_lbl = gtk_widget_get_first_child(box);
            GtkWidget* sub_lbl  = name_lbl ? gtk_widget_get_next_sibling(name_lbl) : nullptr;
            auto* str_obj = GTK_STRING_OBJECT(gtk_list_item_get_item(item));
            if (!str_obj) return;
            const char* full = gtk_string_object_get_string(str_obj);
            if (!full) return;
            std::string s(full);
            auto nl = s.find('\n');
            std::string name = (nl != std::string::npos) ? s.substr(0, nl) : s;
            std::string sub  = (nl != std::string::npos) ? s.substr(nl + 1) : "";
            if (name_lbl) gtk_label_set_text(GTK_LABEL(name_lbl), name.c_str());
            if (sub_lbl)  gtk_label_set_text(GTK_LABEL(sub_lbl),  sub.c_str());
        }), nullptr);
    gtk_drop_down_set_list_factory(GTK_DROP_DOWN(ai_agent_combo_), btn_factory);
    g_object_unref(btn_factory);
}

void BrowserWindow::ShowAgentAutocomplete(const std::string& prefix) {
    if (!ai_autocomplete_pop_ || !ai_autocomplete_list_) return;
    const auto& agents = AiAgentStore::Get().Agents();
    if (agents.empty()) { HideAgentAutocomplete(); return; }

    // Listeyi temizle
    GtkWidget* child = gtk_widget_get_first_child(ai_autocomplete_list_);
    while (child) {
        GtkWidget* nx = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(ai_autocomplete_list_), child);
        child = nx;
    }

    std::string plc = prefix;
    std::transform(plc.begin(), plc.end(), plc.begin(), ::tolower);
    bool any = false;
    for (const auto& ag : agents) {
        std::string aname_lc = ag.name;
        std::transform(aname_lc.begin(), aname_lc.end(), aname_lc.begin(), ::tolower);
        if (plc.empty() || aname_lc.rfind(plc, 0) == 0) {
            std::string prov = AiAgent::DetectProvider(ag.api_key);
            std::string label = ag.name + "  \xe2\x80\x94  " + prov + " / " + ag.model;
            GtkWidget* row_lbl = gtk_label_new(label.c_str());
            gtk_label_set_xalign(GTK_LABEL(row_lbl), 0.0f);
            gtk_widget_set_margin_start(row_lbl, 8);
            gtk_widget_set_margin_end(row_lbl, 8);
            gtk_widget_set_margin_top(row_lbl, 4);
            gtk_widget_set_margin_bottom(row_lbl, 4);
            GtkWidget* lbrow = gtk_list_box_row_new();
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(lbrow), row_lbl);
            g_object_set_data_full(G_OBJECT(lbrow), "agent-name",
                g_strdup(ag.name.c_str()), g_free);
            gtk_list_box_append(GTK_LIST_BOX(ai_autocomplete_list_), lbrow);
            any = true;
        }
    }
    if (any) {
        gtk_popover_set_autohide(GTK_POPOVER(ai_autocomplete_pop_), FALSE);
        gtk_popover_popup(GTK_POPOVER(ai_autocomplete_pop_));
        // Fokus input'ta kalsın
        gtk_widget_grab_focus(ai_input_);
    } else {
        gtk_popover_popdown(GTK_POPOVER(ai_autocomplete_pop_));
    }
}

void BrowserWindow::HideAgentAutocomplete() {
    if (ai_autocomplete_pop_)
        gtk_popover_popdown(GTK_POPOVER(ai_autocomplete_pop_));
}

void BrowserWindow::AddAttachmentChip(const std::string& path) {
    std::string fname = path.substr(path.rfind('/') + 1);
    ai_attachments_.push_back(path);

    GtkWidget* chip_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(chip_row, "ai-chip");

    GtkWidget* lbl = gtk_label_new(("\xf0\x9f\x93\x8e " + fname).c_str());
    gtk_widget_set_tooltip_text(lbl, path.c_str());

    GtkWidget* rm_btn = gtk_button_new_with_label("\xc3\x97");
    gtk_widget_add_css_class(rm_btn, "ai-chip-remove");
    gtk_widget_set_tooltip_text(rm_btn, "Kald\xc4\xb1r");
    g_object_set_data_full(G_OBJECT(rm_btn), "chip-path", g_strdup(path.c_str()), g_free);
    g_object_set_data(G_OBJECT(rm_btn), "chip-self", this);

    g_signal_connect(rm_btn, "clicked",
        G_CALLBACK(+[](GtkButton* b, gpointer) {
            const char* p = static_cast<const char*>(g_object_get_data(G_OBJECT(b), "chip-path"));
            auto* self = static_cast<BrowserWindow*>(g_object_get_data(G_OBJECT(b), "chip-self"));
            if (!p || !self) return;
            std::string path_str(p);
            auto& v = self->ai_attachments_;
            v.erase(std::remove(v.begin(), v.end(), path_str), v.end());
            // rm_btn → chip_row → FlowBoxChild → FlowBox
            GtkWidget* chip_row_w = gtk_widget_get_parent(GTK_WIDGET(b));
            GtkWidget* fb_child   = gtk_widget_get_parent(chip_row_w);
            GtkWidget* fb         = gtk_widget_get_parent(fb_child);
            if (fb) gtk_flow_box_remove(GTK_FLOW_BOX(fb), fb_child);
            if (self->ai_attach_box_)
                gtk_widget_set_visible(self->ai_attach_box_,
                    gtk_widget_get_first_child(self->ai_attach_box_) != nullptr);
        }), nullptr);

    gtk_box_append(GTK_BOX(chip_row), lbl);
    gtk_box_append(GTK_BOX(chip_row), rm_btn);
    gtk_flow_box_append(GTK_FLOW_BOX(ai_attach_box_), chip_row);
    gtk_widget_set_visible(ai_attach_box_, TRUE);
}

void BrowserWindow::SendAiMessage() {
    std::string raw = CollectAiInputText();
    size_t sp = raw.find_first_not_of(" \t\n\r");
    if (sp == std::string::npos) return;
    std::string input = raw.substr(sp);
    while (!input.empty() && std::isspace((unsigned char)input.back())) input.pop_back();
    if (input.empty()) return;

    // Aktif ajanı belirle: önce dropdown seçimi, yoksa ilk ajan, yoksa eski prefs
    const auto& agents = AiAgentStore::Get().Agents();
    std::string active_api_key;
    std::string active_api_url;
    std::string active_model;
    if (!agents.empty()) {
        // Dropdown'dan seçili indeksi al
        guint sel = ai_agent_combo_
            ? gtk_drop_down_get_selected(GTK_DROP_DOWN(ai_agent_combo_))
            : 0;
        if (sel == GTK_INVALID_LIST_POSITION || sel >= agents.size()) sel = 0;
        active_api_key = agents[sel].api_key;
        active_api_url = agents[sel].api_url;
        active_model   = agents[sel].model;
    } else {
        const auto& p = SettingsManager::Get().Prefs();
        active_api_key = p.ai_api_key;
        active_api_url = p.ai_base_url;
        active_model   = p.ai_model;
    }
    std::string provider = AiAgent::DetectProvider(active_api_key);
    std::string model    = active_model;

    if (ai_current_chat_id_ == 0) {
        ai_current_chat_id_ = AiManager::Get().NewChat(provider, model);
        ai_current_chat_ = AiManager::Get().LoadChat(ai_current_chat_id_);
    }

    // @ajan-ismi token override — metinde @isim varsa o ajanı seç
    // Aynı zamanda @token'ları temiz mesajdan çıkar
    std::string clean_input;
    {
        size_t i = 0;
        while (i < input.size()) {
            if (input[i] == '@') {
                size_t end = i + 1;
                while (end < input.size() && !std::isspace((unsigned char)input[end])) ++end;
                std::string tok = input.substr(i + 1, end - i - 1);
                // Ajan listesinde ara
                for (const auto& ag : agents) {
                    std::string aname = ag.name;
                    std::transform(aname.begin(), aname.end(), aname.begin(), ::tolower);
                    std::string tok_lc = tok;
                    std::transform(tok_lc.begin(), tok_lc.end(), tok_lc.begin(), ::tolower);
                    if (aname == tok_lc || aname.rfind(tok_lc, 0) == 0) {
                        active_api_key = ag.api_key;
                        active_api_url = ag.api_url;
                        model          = ag.model;
                        provider       = AiAgent::DetectProvider(ag.api_key);
                        break;
                    }
                }
                // @token'ı clean_input'a ekleme — atla (ve ardındaki boşluğu da)
                i = end;
                while (i < input.size() && input[i] == ' ') ++i;
            } else {
                clean_input += input[i++];
            }
        }
        // Baş/son boşlukları temizle
        size_t cs = clean_input.find_first_not_of(" \t\n\r");
        if (cs == std::string::npos) { clean_input.clear(); }
        else {
            clean_input = clean_input.substr(cs);
            while (!clean_input.empty() && std::isspace((unsigned char)clean_input.back()))
                clean_input.pop_back();
        }
    }
    if (clean_input.empty()) return; // sadece @token yazılmışsa gönderme
    input = clean_input;

    // #id → sekme sayfa içeriği — async JS, tüm fetch'ler bitince DoSendAiMessage çağrılır
    {
        // Kaç adet #id token var say
        struct SendCtx {
            BrowserWindow* self;
            std::string input;
            std::string provider, model, api_key, api_url;
            int pending;
        };
        auto* sctx = new SendCtx{this, input, provider, model,
                                  active_api_key, active_api_url, 0};

        for (size_t p = 0; p < input.size(); ++p) {
            if (input[p] == '#' && p + 1 < input.size() && std::isdigit((unsigned char)input[p+1])) {
                size_t end = p + 1;
                while (end < input.size() && std::isdigit((unsigned char)input[end])) ++end;
                int tab_pos = std::stoi(input.substr(p + 1, end - p - 1)); // 1-tabanlı pozisyon

                // 1-tabanlı pozisyona göre sekmeyi bul
                Tab* found_tab = nullptr;
                if (tab_pos >= 1 && tab_pos <= (int)tabs_.size())
                    found_tab = tabs_[tab_pos - 1];
                // Bulunamazsa tab->id ile de dene
                if (!found_tab) {
                    for (auto* t : tabs_) { if (t->id == tab_pos) { found_tab = t; break; } }
                }

                if (found_tab && found_tab->webview) {
                    Tab* t = found_tab;
                    const char* uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(t->webview));
                    std::string url_str = uri ? uri : "bilinmiyor";
                    static const char* js =
                        "(function(){"
                        "var skip=['script','style','noscript','iframe','header','footer',"
                        "'nav','aside','form','button','input','select','textarea'];"
                        "function clean(n){"
                        "if(n.nodeType===3){var t=n.textContent.replace(/\\s+/g,' ');return t;}"
                        "if(n.nodeType!==1)return '';"
                        "var tag=n.tagName.toLowerCase();"
                        "if(skip.indexOf(tag)>=0)return '';"
                        "var cls=(n.className||'').toLowerCase()+(n.id||'').toLowerCase();"
                        "if(/ad|banner|sponsor|popup|cookie|consent|gdpr|promo/.test(cls))return '';"
                        "var r='';"
                        "for(var i=0;i<n.childNodes.length;i++)r+=clean(n.childNodes[i]);"
                        "return r;}"
                        "var txt=clean(document.body||document).replace(/[ \\t]{2,}/g,' ')"
                        ".replace(/\\n{3,}/g,'\\n\\n').trim();"
                        "return txt.length>8000?txt.substring(0,8000)+'...':txt;"
                        "})()";
                    struct TabCtx {
                        SendCtx* sctx;
                        std::string url;
                        int pos;
                    };
                    ++sctx->pending;
                    auto* tctx = new TabCtx{sctx, url_str, tab_pos};
                    webkit_web_view_evaluate_javascript(
                        WEBKIT_WEB_VIEW(t->webview), js, -1, nullptr, nullptr, nullptr,
                        [](GObject* src, GAsyncResult* res, gpointer ud) {
                            auto* tc = static_cast<TabCtx*>(ud);
                            GError* err = nullptr;
                            JSCValue* val = webkit_web_view_evaluate_javascript_finish(
                                WEBKIT_WEB_VIEW(src), res, &err);
                            std::string page_text;
                            if (val && jsc_value_is_string(val)) {
                                char* s = jsc_value_to_string(val);
                                page_text = s ? s : "";
                                g_free(s);
                                g_object_unref(val);
                            }
                            if (err) g_error_free(err);
                            // Sayfa içeriği boşsa hata mesajı ver
                            if (page_text.empty()) page_text = "(sayfa içeriği alınamadı)";
                            AiMessage sys;
                            sys.role = "system";
                            sys.timestamp = (int64_t)time(nullptr);
                            sys.content = "Sekme #" + std::to_string(tc->pos) +
                                " — URL: " + tc->url + "\n\nSayfa İçeriği:\n" + page_text;
                            tc->sctx->self->ai_current_chat_.messages.push_back(sys);
                            --tc->sctx->pending;
                            if (tc->sctx->pending == 0) {
                                tc->sctx->self->DoSendAiMessage(
                                    tc->sctx->input, tc->sctx->provider,
                                    tc->sctx->model, tc->sctx->api_key,
                                    tc->sctx->api_url);
                                delete tc->sctx;
                            }
                            delete tc;
                        }, tctx);
                }
            }
        }
        if (sctx->pending > 0) {
            // JS fetch'ler bekliyor — DoSendAiMessage callback içinde çağrılacak
            return;
        }
        delete sctx;
    }

    DoSendAiMessage(input, provider, model, active_api_key, active_api_url);
}

                                    const std::string& provider,
                                    const std::string& model,
                                    const std::string& api_key,
                                    const std::string& api_url) {
    if (input.empty()) return;
    
    // Kurulum atlandıysa ve API key yoksa uyarı göster
    if (SettingsManager::Get().IsSetupSkipped() && 
        SettingsManager::Get().GetDecryptedApiKey().empty() &&
        api_key.empty()) {
        ShowAiSetupWarning();
        return;
    }
    // *id → geçmiş sohbet
    for (size_t p = 0; p < input.size(); ++p) {
        if (input[p] == '*' && p + 1 < input.size() && std::isdigit((unsigned char)input[p+1])) {
            size_t end = p + 1;
            while (end < input.size() && std::isdigit((unsigned char)input[end])) ++end;
            int64_t cid = (int64_t)std::stoll(input.substr(p + 1, end - p - 1));
            AiChat old = AiManager::Get().LoadChat(cid);
            if (old.id != 0) {
                std::string ctx = "Geçmiş sohbet *" + std::to_string(cid) +
                    " (" + old.title + "):\n";
                for (const auto& m : old.messages)
                    if (m.role != "system") ctx += "[" + m.role + "]: " + m.content + "\n";
                AiMessage sys; sys.role = "system"; sys.content = ctx;
                sys.timestamp = (int64_t)time(nullptr);
                ai_current_chat_.messages.push_back(sys);
            }
        }
    }

    // Ekler: bubble için isim listesi kaydet
    std::vector<std::string> attach_names;
    for (const auto& fpath : ai_attachments_) {
        auto res = FileExtractor::Extract(fpath);
        std::string fname = fpath.substr(fpath.rfind('/') + 1);
        attach_names.push_back(fname);
        AiMessage sys; sys.role = "system"; sys.timestamp = (int64_t)time(nullptr);
        if (!res.error.empty()) {
            sys.content = "Dosya (" + fname + ") okunamadı: " + res.error;
        } else if (res.is_image) {
            // Görsel: base64 veri + açıklama
            sys.content = "[Görsel eki: " + fname + "]"
                + (res.base64_data.empty() ? "" : "\ndata:" + res.mime_type + ";base64,...");
            // Vision destekli API için tam base64'u sakla
            sys.image_url = res.base64_data;
        } else {
            sys.content = "Ek dosya (" + fname + "):\n" + res.text;
        }
        ai_current_chat_.messages.push_back(sys);
    }

    // Kullanıcı mesajı
    AiMessage user_msg;
    user_msg.role = "user"; user_msg.content = input;
    user_msg.timestamp = (int64_t)time(nullptr);
    ai_current_chat_.messages.push_back(user_msg);

    // Bubble: mesaj + ek listesi
    std::string bubble_text = input;
    if (!attach_names.empty()) {
        bubble_text += "\n";
        for (const auto& fn : attach_names)
            bubble_text += "\n\xf0\x9f\x93\x8e Ek: " + fn;
    }
    AppendAiBubble("user", bubble_text);
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(ai_input_buffer_), "", 0);
    ai_attachments_.clear();
    {
        GtkWidget* chip = gtk_widget_get_first_child(ai_attach_box_);
        while (chip) {
            GtkWidget* nx = gtk_widget_get_next_sibling(chip);
            gtk_flow_box_remove(GTK_FLOW_BOX(ai_attach_box_), chip);
            chip = nx;
        }
    }
    gtk_widget_set_visible(ai_attach_box_, FALSE);
    ShowAiLoading(true);

    AiManager::Get().SendMessage(
        ai_current_chat_, provider, model, api_key, api_url,
        [this](const std::string& content, bool /*done*/, const std::string& err) {
            ShowAiLoading(false);
            if (!err.empty())
                AppendAiBubble("assistant", "⚠ Hata: " + err);
            else if (!content.empty())
                AppendAiBubble("assistant", content);
            RefreshAiChatList();
        });
}

// ── Sayfa içi arama ───────────────────────────────────────────────────────────

void BrowserWindow::ShowFindBar() {
    find_visible_ = true;
    gtk_revealer_set_reveal_child(GTK_REVEALER(find_bar_), TRUE);
    gtk_widget_grab_focus(find_entry_);
    gtk_editable_select_region(GTK_EDITABLE(find_entry_), 0, -1);
}

void BrowserWindow::HideFindBar() {
    find_visible_ = false;
    gtk_revealer_set_reveal_child(GTK_REVEALER(find_bar_), FALSE);
    if (active_tab_ && active_tab_->webview) {
        WebKitFindController* fc = webkit_web_view_get_find_controller(
            WEBKIT_WEB_VIEW(active_tab_->webview));
        webkit_find_controller_search_finish(fc);
    }
    gtk_label_set_text(GTK_LABEL(find_count_lbl_), "");
}

void BrowserWindow::FindNext() {
    if (!active_tab_ || !active_tab_->webview) return;
    const char* q = gtk_editable_get_text(GTK_EDITABLE(find_entry_));
    if (!q || !*q) return;
    WebKitFindController* fc = webkit_web_view_get_find_controller(
        WEBKIT_WEB_VIEW(active_tab_->webview));
    webkit_find_controller_search_next(fc);
}

void BrowserWindow::FindPrev() {
    if (!active_tab_ || !active_tab_->webview) return;
    const char* q = gtk_editable_get_text(GTK_EDITABLE(find_entry_));
    if (!q || !*q) return;
    WebKitFindController* fc = webkit_web_view_get_find_controller(
        WEBKIT_WEB_VIEW(active_tab_->webview));
    webkit_find_controller_search_previous(fc);
}

// ── Static callbacks ──────────────────────────────────────────────────────────

void BrowserWindow::OnAiSendCb(GtkButton*, gpointer ud) {
    static_cast<BrowserWindow*>(ud)->SendAiMessage();
}

void BrowserWindow::OnAiNewChatCb(GtkButton*, gpointer ud) {
    auto* self = static_cast<BrowserWindow*>(ud);
    const auto& p = SettingsManager::Get().Prefs();
    self->ai_current_chat_id_ = AiManager::Get().NewChat(p.ai_provider, p.ai_model);
    self->ai_current_chat_ = AiManager::Get().LoadChat(self->ai_current_chat_id_);
    if (self->ai_title_label_)
        gtk_label_set_text(GTK_LABEL(self->ai_title_label_), "AI Asistan");
    GtkWidget* child = gtk_widget_get_first_child(self->ai_chat_box_);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(self->ai_chat_box_), child);
        child = next;
    }
    self->RefreshAiChatList();
}

void BrowserWindow::OnAiToggleHistoryCb(GtkButton*, gpointer ud) {
    static_cast<BrowserWindow*>(ud)->ToggleAiHistory();
}

void BrowserWindow::OnAiToggleFilterCb(GtkButton*, gpointer ud) {
    static_cast<BrowserWindow*>(ud)->ToggleAiFilter();
}

void BrowserWindow::OnAiSearchChangedCb(GtkSearchEntry*, gpointer ud) {
    static_cast<BrowserWindow*>(ud)->RefreshAiChatList();
}

void BrowserWindow::OnAiDateFilterCb(GtkDropDown*, GParamSpec*, gpointer ud) {
    static_cast<BrowserWindow*>(ud)->RefreshAiChatList();
}

void BrowserWindow::OnAiInputChangedCb(GtkTextBuffer*, gpointer ud) {
    static_cast<BrowserWindow*>(ud)->ParseAndHighlightTokens();
}

gboolean BrowserWindow::OnAiKeyPressCb(GtkEventControllerKey*, guint keyval,
                                        guint /*keycode*/, GdkModifierType state, gpointer ud) {
    auto* self = static_cast<BrowserWindow*>(ud);
    // Escape → autocomplete kapat
    if (keyval == GDK_KEY_Escape) {
        if (self->ai_autocomplete_pop_ &&
            gtk_widget_get_visible(self->ai_autocomplete_pop_)) {
            self->HideAgentAutocomplete();
            return TRUE;
        }
        return FALSE;
    }
    // Enter (shift yok) → gönder, event'i tüket
    if (keyval == GDK_KEY_Return && !(state & GDK_SHIFT_MASK)) {
        // Autocomplete açıksa ilk satırı seç
        if (self->ai_autocomplete_pop_ &&
            gtk_widget_get_visible(self->ai_autocomplete_pop_)) {
            GtkListBoxRow* row = gtk_list_box_get_row_at_index(
                GTK_LIST_BOX(self->ai_autocomplete_list_), 0);
            if (row) {
                g_signal_emit_by_name(self->ai_autocomplete_list_, "row-activated", row);
                return TRUE;
            }
        }
        self->SendAiMessage();
        return TRUE;
    }
    // Backspace → eğer imleç @token içindeyse token'ı tamamen sil
    if (keyval == GDK_KEY_BackSpace && !(state & (GDK_SHIFT_MASK|GDK_CONTROL_MASK))) {
        if (!self->ai_input_buffer_) return FALSE;
        GtkTextBuffer* buf = GTK_TEXT_BUFFER(self->ai_input_buffer_);
        GtkTextIter cursor;
        gtk_text_buffer_get_iter_at_mark(buf, &cursor,
            gtk_text_buffer_get_insert(buf));
        int cur_off = gtk_text_iter_get_offset(&cursor);
        GtkTextIter s, e;
        gtk_text_buffer_get_bounds(buf, &s, &e);
        gchar* raw = gtk_text_iter_get_text(&s, &e);
        std::string text = raw ? raw : "";
        g_free(raw);
        // İmleç solundaki @ tokenını bul
        if (cur_off > 0) {
            int at_off = -1;
            for (int k = cur_off - 1; k >= 0; --k) {
                if ((unsigned char)text[k] <= 0x7F || (unsigned char)text[k] >= 0xC0) {
                    if (text[k] == '@') { at_off = k; break; }
                    if (text[k] == ' ' || text[k] == '\n') break;
                }
            }
            if (at_off >= 0) {
                // @token bölgesi: at_off..cur_off
                bool in_token = true;
                for (int k = at_off + 1; k < cur_off; ++k)
                    if (text[k] == ' ' || text[k] == '\n') { in_token = false; break; }
                if (in_token && cur_off > at_off + 1) {
                    GtkTextIter ts, te;
                    gtk_text_buffer_get_iter_at_offset(buf, &ts, at_off);
                    gtk_text_buffer_get_iter_at_offset(buf, &te, cur_off);
                    gtk_text_buffer_delete(buf, &ts, &te);
                    return TRUE;
                }
            }
        }
    }
    // Diğer tuşlar normal şekilde GtkTextView'a gitsin
    return FALSE;
}

void BrowserWindow::OnAiAttachCb(GtkButton*, gpointer ud) {
    auto* self = static_cast<BrowserWindow*>(ud);
    GtkFileDialog* dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, "Dosya Seç");
    gtk_file_dialog_open(dlg, GTK_WINDOW(self->window_), nullptr,
        [](GObject* src, GAsyncResult* res, gpointer ud2) {
            auto* self2 = static_cast<BrowserWindow*>(ud2);
            GError* err = nullptr;
            GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, &err);
            if (file) {
                char* path = g_file_get_path(file);
                if (path) { self2->AddAttachmentChip(path); g_free(path); }
                g_object_unref(file);
            }
            if (err) g_error_free(err);
        }, self);
    g_object_unref(dlg);
}

void BrowserWindow::SaveTabSession() {
    const char* home = g_get_home_dir();
    std::string path = std::string(home) + "/.local/share/ferman-browser/last_session.txt";
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    for (auto* t : tabs_) {
        if (!t->webview) continue;
        const char* uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(t->webview));
        std::string url = uri ? uri : t->url;
        // ferman:// iç sayfaları kaydetme — sadece gerçek URL'ler
        if (url.empty() || url.rfind("ferman://", 0) == 0 ||
            url.rfind("about:", 0) == 0) continue;
        f << url << "\n";
    }
}

void BrowserWindow::RestoreTabSession() {
    const char* home = g_get_home_dir();
    std::string path = std::string(home) + "/.local/share/ferman-browser/last_session.txt";
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    bool opened_any = false;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        NewTab(line);
        opened_any = true;
    }
    (void)opened_any;
}

void BrowserWindow::HandlefermanScheme(const std::string& uri) {
    if (!active_tab_) return;
    auto* wv = WEBKIT_WEB_VIEW(active_tab_->webview);
    std::string html;
    std::string title;

    // ── Kurulum Ekranı ──────────────────────────────────────────────────────
    if (uri == "ferman://setup" || uri.rfind("ferman://setup?", 0) == 0) {
        // Hata parametresi varsa parse et
        std::string error;
        auto err_pos = uri.find("error=");
        if (err_pos != std::string::npos) {
            err_pos += 6;
            auto end_pos = uri.find('&', err_pos);
            std::string enc_error = (end_pos == std::string::npos) 
                ? uri.substr(err_pos) : uri.substr(err_pos, end_pos - err_pos);
            // URL decode
            for (size_t i = 0; i < enc_error.size(); ++i) {
                if (enc_error[i] == '%' && i + 2 < enc_error.size()) {
                    int val; sscanf(enc_error.substr(i+1,2).c_str(), "%x", &val);
                    error += (char)val; i += 2;
                } else if (enc_error[i] == '+') {
                    error += ' ';
                } else {
                    error += enc_error[i];
                }
            }
        }
        html  = BuildSetupHTML(error);
        title = "Kurulum — Ferman Browser";
    }
    // ── Kurulum Gönder ──────────────────────────────────────────────────────
    else if (uri.rfind("ferman://setup-submit", 0) == 0) {
        auto parse_param = [&](const std::string& key) -> std::string {
            std::string search = key + "=";
            auto pos = uri.find(search);
            if (pos == std::string::npos) return "";
            pos += search.size();
            auto end = uri.find('&', pos);
            std::string encoded = (end == std::string::npos) ? uri.substr(pos) : uri.substr(pos, end - pos);
            std::string decoded;
            for (size_t i = 0; i < encoded.size(); ++i) {
                if (encoded[i] == '%' && i+2 < encoded.size()) {
                    int val; sscanf(encoded.substr(i+1,2).c_str(), "%x", &val);
                    decoded += (char)val; i += 2;
                } else if (encoded[i] == '+') decoded += ' ';
                else decoded += encoded[i];
            }
            return decoded;
        };
        
        std::string email = parse_param("email");
        std::string password = parse_param("password");
        std::string name = parse_param("name");
        
        // HTTP isteği gönder (async)
        SetupManager::Get().SendSetupRequest(email, password, name,
            [this](bool success, const SetupData& data, const std::string& error) {
                if (success) {
                    // Ayarları kaydet
                    auto& prefs = SettingsManager::Get().Prefs();
                    prefs.setup_completed = true;
                    prefs.setup_skipped = false;
                    prefs.user_email = data.email;
                    SettingsManager::Get().SetEncryptedApiKey(data.api_key);
                    
                    if (!data.homepage.empty()) prefs.homepage = data.homepage;
                    if (!data.search_engine.empty()) prefs.search_engine = data.search_engine;
                    if (!data.ai_provider.empty()) prefs.ai_provider = data.ai_provider;
                    if (!data.ai_model.empty()) prefs.ai_model = data.ai_model;
                    if (!data.ai_base_url.empty()) prefs.ai_base_url = data.ai_base_url;
                    
                    SettingsManager::Get().Save();
                    
                    // Ana sayfaya yönlendir
                    g_idle_add([](gpointer ud) -> gboolean {
                        auto* self = static_cast<BrowserWindow*>(ud);
                        if (self->active_tab_) {
                            const std::string& hp = SettingsManager::Get().Prefs().homepage;
                            std::string url = hp.empty() ? self->kHomePage : hp;
                            webkit_web_view_load_uri(
                                WEBKIT_WEB_VIEW(self->active_tab_->webview), url.c_str());
                        }
                        return G_SOURCE_REMOVE;
                    }, this);
                } else {
                    // Hata ile kurulum sayfasına geri dön
                    std::string error_url = "ferman://setup?error=" + error;
                    g_idle_add([](gpointer ud) -> gboolean {
                        auto* pair = static_cast<std::pair<BrowserWindow*, std::string>*>(ud);
                        if (pair->first->active_tab_) {
                            webkit_web_view_load_uri(
                                WEBKIT_WEB_VIEW(pair->first->active_tab_->webview),
                                pair->second.c_str());
                        }
                        delete pair;
                        return G_SOURCE_REMOVE;
                    }, new std::pair<BrowserWindow*, std::string>(this, error_url));
                }
            });
        return; // Async işlem, HTML yükleme yok
    }
    // ── Kurulum Atla ────────────────────────────────────────────────────────
    else if (uri == "ferman://setup-skip") {
        SettingsManager::Get().Prefs().setup_completed = true;
        SettingsManager::Get().Prefs().setup_skipped = true;
        SettingsManager::Get().Save();
        
        const std::string& hp = SettingsManager::Get().Prefs().homepage;
        std::string url = hp.empty() ? kHomePage : hp;
        webkit_web_view_load_uri(wv, url.c_str());
        return;
    }
    // ── Ayarlar Sayfaları ───────────────────────────────────────────────────
    else if (uri == "ferman://ayarlar" || uri == "ferman://ayarlar/genel" ||
        uri.rfind("ferman://ayarlar?", 0) == 0) {
        html  = BuildSettingsHTML("genel");
        title = "Genel Ayarlar — ferman";
    } else if (uri == "ferman://ayarlar/gorunum") {
        html  = BuildAppearanceSettingsHTML();
        title = "Görünüm — ferman";
    } else if (uri == "ferman://ayarlar/sekmeler") {
        html  = BuildTabsSettingsHTML();
        title = "Sekmeler — ferman";
    } else if (uri == "ferman://ayarlar/gelismis") {
        html  = BuildAdvancedSettingsHTML();
        title = "Gelişmiş — ferman";
    } else if (uri == "ferman://ayarlar/hakkimizda") {
        html  = BuildAboutHTML();
        title = "Hakkımızda — ferman";
    } else if (uri == "ferman://ayarlar/gizlilik") {
        html  = BuildPrivacyHTML();
        title = "Gizlilik — ferman";
    } else if (uri == "ferman://ayarlar/yapay-zeka") {
        html  = BuildSettingsAiHTML();
        title = "Yapay Zeka — ferman";
    } else if (uri.rfind("ferman://ai-ajan-kaydet", 0) == 0) {
        auto decode = [&](const std::string& key) -> std::string {
            std::string search = key + "=";
            auto pos = uri.find(search);
            if (pos == std::string::npos) return "";
            pos += search.size();
            auto end = uri.find('&', pos);
            std::string enc = (end == std::string::npos) ? uri.substr(pos) : uri.substr(pos, end - pos);
            std::string out;
            for (size_t i = 0; i < enc.size(); ++i) {
                if (enc[i] == '%' && i + 2 < enc.size()) {
                    int v = 0; sscanf(enc.substr(i+1,2).c_str(), "%x", &v);
                    out += (char)v; i += 2;
                } else if (enc[i] == '+') { out += ' '; }
                else { out += enc[i]; }
            }
            return out;
        };
        AiAgent ag;
        ag.id      = decode("id");
        ag.name    = decode("name");
        ag.api_key = decode("key");
        ag.model   = decode("model");
        ag.api_url = decode("url");
        // Düzenleme modunda (id varsa) mevcut anahtar boşsa eski anahtar korunur
        if (!ag.id.empty() && ag.api_key.empty()) {
            auto* existing = AiAgentStore::Get().FindById(ag.id);
            if (existing) ag.api_key = existing->api_key;
        }
        AiAgentStore::Get().AddAgent(ag);
        webkit_web_view_load_uri(wv, "ferman://ayarlar/yapay-zeka");
        return;
    } else if (uri.rfind("ferman://ai-ajan-sil", 0) == 0) {
        auto pos = uri.find("id=");
        if (pos != std::string::npos) {
            std::string enc_id = uri.substr(pos + 3);
            std::string id;
            for (size_t i = 0; i < enc_id.size(); ++i) {
                if (enc_id[i] == '%' && i + 2 < enc_id.size()) {
                    int v = 0; sscanf(enc_id.substr(i+1,2).c_str(), "%x", &v);
                    id += (char)v; i += 2;
                } else { id += enc_id[i]; }
            }
            AiAgentStore::Get().RemoveAgent(id);
        }
        webkit_web_view_load_uri(wv, "ferman://ayarlar/yapay-zeka");
        return;
    } else if (uri == "ferman://indirmeler") {
        DownloadManager::Get().ShowPanel();
        return;
    } else if (uri.rfind("ferman://pick-download-dir", 0) == 0) {
        // İndirme klasörü seçici — GtkFileDialog ile native klasör seçici aç
        GtkFileDialog* fd = gtk_file_dialog_new();
        gtk_file_dialog_set_title(fd, "İndirme Klasörünü Seç");
        gtk_file_dialog_set_modal(fd, TRUE);
        // Mevcut değeri başlangıç klasörü olarak ayarla
        auto cur_pos = uri.find("cur=");
        if (cur_pos != std::string::npos) {
            std::string enc = uri.substr(cur_pos + 4);
            std::string dec;
            for (size_t i = 0; i < enc.size(); ++i) {
                if (enc[i] == '%' && i + 2 < enc.size()) {
                    int v = 0; sscanf(enc.substr(i+1,2).c_str(), "%x", &v);
                    dec += (char)v; i += 2;
                } else if (enc[i] == '+') dec += ' ';
                else dec += enc[i];
            }
            if (!dec.empty()) {
                std::string path = (dec[0]=='~')
                    ? std::string(g_get_home_dir()) + dec.substr(1) : dec;
                GFile* f = g_file_new_for_path(path.c_str());
                gtk_file_dialog_set_initial_folder(fd, f);
                g_object_unref(f);
            }
        }
        struct FdCtx { BrowserWindow* self; };
        auto* ctx = new FdCtx{this};
        gtk_file_dialog_select_folder(fd, GTK_WINDOW(window_), nullptr,
            [](GObject* src, GAsyncResult* res, gpointer ud) {
                auto* ctx = static_cast<FdCtx*>(ud);
                GFile* f = gtk_file_dialog_select_folder_finish(
                    GTK_FILE_DIALOG(src), res, nullptr);
                if (f) {
                    char* path = g_file_get_path(f);
                    if (path) {
                        SettingsManager::Get().Prefs().download_dir = path;
                        SettingsManager::Get().Save();
                        // Ayarlar sayfasını yenile
                        ctx->self->HandlefermanScheme("ferman://ayarlar");
                        g_free(path);
                    }
                    g_object_unref(f);
                }
                delete ctx;
            }, ctx);
        g_object_unref(fd);
        return;
    } else if (uri == "ferman://gecmis") {
        html  = BuildHistoryHTML();
        title = "Geçmiş — ferman";
    } else if (uri.rfind("ferman://gecmis-temizle", 0) == 0) {
        // Geçmişi temizle — sayfa içinden onay alındıktan sonra
        HistoryManager::Get().Clear();
        WebKitNetworkSession* ns = SessionManager::Get().GetSession();
        WebKitWebsiteDataManager* dm = webkit_network_session_get_website_data_manager(ns);
        webkit_website_data_manager_clear(dm,
            static_cast<WebKitWebsiteDataTypes>(
                WEBKIT_WEBSITE_DATA_MEMORY_CACHE |
                WEBKIT_WEBSITE_DATA_DISK_CACHE),
            0, nullptr, nullptr, nullptr);
        webkit_web_view_load_uri(wv, "ferman://gecmis");
        return;
    } else if (uri.rfind("ferman://favori-yeniden-adlandir", 0) == 0) {
        // ferman://favori-yeniden-adlandir?url=...&title=...
        auto parse_param = [&](const std::string& key) -> std::string {
            std::string search = key + "=";
            auto pos = uri.find(search);
            if (pos == std::string::npos) return "";
            pos += search.size();
            auto end = uri.find('&', pos);
            std::string encoded = (end == std::string::npos) ? uri.substr(pos) : uri.substr(pos, end - pos);
            std::string decoded;
            for (size_t i = 0; i < encoded.size(); ++i) {
                if (encoded[i] == '%' && i+2 < encoded.size()) {
                    int val; sscanf(encoded.substr(i+1,2).c_str(), "%x", &val);
                    decoded += (char)val; i += 2;
                } else if (encoded[i] == '+') decoded += ' ';
                else decoded += encoded[i];
            }
            return decoded;
        };
        std::string burl  = parse_param("url");
        std::string btitle = parse_param("title");
        if (!burl.empty()) BookmarkManager::Get().Rename(burl, btitle);
        RebuildBookmarksBar();
        return;
    } else if (uri.rfind("ferman://favori-klasor", 0) == 0) {
        auto parse_param = [&](const std::string& key) -> std::string {
            std::string search = key + "=";
            auto pos = uri.find(search);
            if (pos == std::string::npos) return "";
            pos += search.size();
            auto end = uri.find('&', pos);
            std::string encoded = (end == std::string::npos) ? uri.substr(pos) : uri.substr(pos, end - pos);
            std::string decoded;
            for (size_t i = 0; i < encoded.size(); ++i) {
                if (encoded[i] == '%' && i+2 < encoded.size()) {
                    int val; sscanf(encoded.substr(i+1,2).c_str(), "%x", &val);
                    decoded += (char)val; i += 2;
                } else if (encoded[i] == '+') decoded += ' ';
                else decoded += encoded[i];
            }
            return decoded;
        };
        std::string burl    = parse_param("url");
        std::string bfolder = parse_param("folder");
        if (!burl.empty()) BookmarkManager::Get().MoveToFolder(burl, bfolder);
        RebuildBookmarksBar();
        return;
    } else if (uri.rfind("ferman://ayarlar-kaydet", 0) == 0) {
        // Ayarları URL parametrelerinden oku ve kaydet
        auto& prefs = SettingsManager::Get().Prefs();
        auto parse_param = [&](const std::string& key) -> std::string {
            std::string search = key + "=";
            auto pos = uri.find(search);
            if (pos == std::string::npos) return "";
            pos += search.size();
            auto end = uri.find('&', pos);
            std::string encoded = (end == std::string::npos)
                ? uri.substr(pos) : uri.substr(pos, end - pos);
            // URL decode basit
            std::string decoded;
            for (size_t i = 0; i < encoded.size(); ++i) {
                if (encoded[i] == '%' && i + 2 < encoded.size()) {
                    int val;
                    sscanf(encoded.substr(i+1,2).c_str(), "%x", &val);
                    decoded += (char)val; i += 2;
                } else if (encoded[i] == '+') {
                    decoded += ' ';
                } else {
                    decoded += encoded[i];
                }
            }
            return decoded;
        };
        std::string hp       = parse_param("hp");
        std::string zoom     = parse_param("zoom");
        std::string js       = parse_param("js");
        std::string hw       = parse_param("hw");
        std::string se       = parse_param("se");
        std::string dldir    = parse_param("dldir");
        std::string histdays = parse_param("histdays");
        std::string maxtabs  = parse_param("maxtabs");
        std::string restore  = parse_param("restore");
        std::string fontsize    = parse_param("fontsize");
        std::string minfont     = parse_param("minfont");
        std::string ai_provider = parse_param("ai_provider");
        std::string ai_model    = parse_param("ai_model");
        std::string ai_api_key  = parse_param("ai_api_key");
        std::string ai_base_url = parse_param("ai_base_url");
        if (!hp.empty())          prefs.homepage          = hp;
        if (!zoom.empty())        prefs.default_zoom      = std::stod(zoom) / 100.0;
        if (!js.empty())          prefs.javascript_enabled= (js == "1");
        if (!hw.empty())          prefs.hardware_accel    = (hw == "1");
        if (!se.empty())          prefs.search_engine     = se;
        prefs.download_dir = dldir;
        if (!histdays.empty())    prefs.history_days      = std::stoi(histdays);
        if (!maxtabs.empty())     prefs.max_tabs          = std::stoi(maxtabs);
        if (!restore.empty())     prefs.restore_tabs      = (restore == "1");
        if (!fontsize.empty())    prefs.font_size         = std::stoi(fontsize);
        if (!minfont.empty())     prefs.min_font_size     = std::stoi(minfont);
        if (!ai_provider.empty()) prefs.ai_provider       = ai_provider;
        if (!ai_model.empty())    prefs.ai_model          = ai_model;
        if (!ai_api_key.empty())  prefs.ai_api_key        = ai_api_key;
        prefs.ai_base_url = ai_base_url;
        SettingsManager::Get().Save();
        // Kayıt sonrası tüm mevcut sekmelere js/hw/zoom uygula
        {
            const auto& p2 = SettingsManager::Get().Prefs();
            for (auto* t : tabs_) {
                if (!t->webview) continue;
                auto* ws = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(t->webview));
                if (!js.empty())
                    webkit_settings_set_enable_javascript(ws, p2.javascript_enabled);
                if (!hw.empty())
                    webkit_settings_set_hardware_acceleration_policy(ws,
                        p2.hardware_accel
                            ? WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS
                            : WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
                if (!zoom.empty())
                    webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(t->webview), p2.default_zoom);
                if (!fontsize.empty())
                    webkit_settings_set_default_font_size(ws, (guint32)p2.font_size);
                if (!minfont.empty())
                    webkit_settings_set_minimum_font_size(ws, (guint32)p2.min_font_size);
            }
        }
        // Kayıt sonrası ayarlar sayfasına dön
        webkit_web_view_load_uri(wv, "ferman://ayarlar");
        return;
    } else {
        return;  // bilinmeyen ferman:// — zaten home tarafından handle ediliyor
    }

    webkit_web_view_load_html(wv, html.c_str(), nullptr);
    active_tab_->title = title;
    active_tab_->url   = uri;  // URL bar'da ferman:// görünsün
    UpdateTabLabel(active_tab_);
    // URL barını güncelle (programatik — focus yoksa changed sinyali popup açmaz)
    gtk_editable_set_text(GTK_EDITABLE(url_entry_), uri.c_str());
    std::string wt = (title.empty() || title == kAppName) ? kAppName : (title + " — " + kAppName);
    gtk_window_set_title(GTK_WINDOW(window_), wt.c_str());
}

// ── Static callback'ler (yeni) ───────────────────────────────────────────

gboolean BrowserWindow::OnDecidePolicyCb(WebKitWebView* wv,
                                          WebKitPolicyDecision* dec,
                                          WebKitPolicyDecisionType type,
                                          gpointer ud) {
    return static_cast<BrowserWindow*>(ud)->OnDecidePolicy(wv, dec, type) ? TRUE : FALSE;
}
gboolean BrowserWindow::OnContextMenuCb(WebKitWebView* wv,
                                         WebKitContextMenu* menu,
                                         WebKitHitTestResult* hit,
                                         gpointer ud) {
    return static_cast<BrowserWindow*>(ud)->OnContextMenu(wv, menu, hit) ? TRUE : FALSE;
}
WebKitWebView* BrowserWindow::OnCreateWebViewCb(WebKitWebView* wv,
                                                  WebKitNavigationAction* action,
                                                  gpointer ud) {
    return static_cast<BrowserWindow*>(ud)->OnCreateWebView(wv, action);
}
gboolean BrowserWindow::OnPrintCb(WebKitWebView* wv,
                                    WebKitPrintOperation* op,
                                    gpointer ud) {
    static_cast<BrowserWindow*>(ud)->OnPrint(wv, op);
    return TRUE;
}
void BrowserWindow::OnMouseTargetChangedCb(WebKitWebView* wv,
                                            WebKitHitTestResult* hit,
                                            guint, gpointer ud) {
    static_cast<BrowserWindow*>(ud)->OnMouseTargetChanged(wv, hit);
}

void BrowserWindow::ShowAiSetupWarning() {
    GtkAlertDialog* dlg = gtk_alert_dialog_new("Yapay Zeka Kurulumu Gerekli");
    gtk_alert_dialog_set_detail(dlg,
        "Yapay zeka özelliklerini kullanmak için aşağıdaki adreslerden "
        "API anahtarı almanız gerekir:\n\n"
        "• ferman.net.tr\n"
        "• openai.com\n"
        "• claude.com (anthropic.com)\n"
        "• deepseek.com\n\n"
        "API anahtarınızı Ayarlar > Yapay Zeka bölümünden ekleyebilirsiniz.");
    
    const char* btns[] = { "Tamam", "Ayarlara Git", nullptr };
    gtk_alert_dialog_set_buttons(dlg, btns);
    gtk_alert_dialog_set_cancel_button(dlg, 0);
    gtk_alert_dialog_set_default_button(dlg, 1);
    
    gtk_alert_dialog_choose(dlg, GTK_WINDOW(window_), nullptr,
        [](GObject* src, GAsyncResult* res, gpointer ud) {
            int btn = gtk_alert_dialog_choose_finish(
                GTK_ALERT_DIALOG(src), res, nullptr);
            if (btn == 1) {
                auto* self = static_cast<BrowserWindow*>(ud);
                self->NewTab("ferman://ayarlar/yapay-zeka");
            }
            g_object_unref(src);
        }, this);
}

} // namespace ferman
