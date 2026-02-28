#include "browser_window.h"
#include "session_manager.h"
#include "history_manager.h"
#include "bookmark_manager.h"
#include "download_manager.h"
#include "settings_manager.h"
#include <algorithm>
#include <string>
#include <sstream>
#include <cstdio>
#include <cctype>

// kHomeHTML artık BuildHomeHTML() ile dinamik oluşturuluyor (arama motoru ayarına göre)

static const char* kTabCSS = R"(
.tab-row {
    min-width: 80px;
    border-radius: 6px 6px 0 0;
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
    font-size: 0.72rem;
    font-weight: 600;
    opacity: 0.55;
    min-width: 20px;
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
)";

namespace ferzan {

// ── Constructor ──────────────────────────────────────────────────────────────

BrowserWindow::BrowserWindow(GtkApplication* app) {
    app_    = app;
    window_ = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window_), kAppName);
    gtk_window_set_default_size(GTK_WINDOW(window_), 1280, 800);
    g_object_set_data(G_OBJECT(window_), "browser-window", this);

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
        std::string data_dir = std::string(home) + "/.local/share/ferzan-browser";
        std::string cfg_dir  = std::string(home) + "/.config/ferzan-browser";
        HistoryManager::Get().Init(data_dir + "/history.db");
        BookmarkManager::Get().Init(data_dir + "/bookmarks.json");
        SettingsManager::Get().Init(cfg_dir);
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

    // Tab çubuğu — pack_start ile sola yaslı (title_widget ortaladığı için kullanılmıyor)
    GtkWidget* tabs_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tabs_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_set_hexpand(tabs_scroll, TRUE);
    gtk_widget_set_halign(tabs_scroll, GTK_ALIGN_FILL);
    gtk_widget_set_size_request(tabs_scroll, 100, -1);

    tab_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_halign(tab_box_, GTK_ALIGN_START);
    gtk_widget_set_hexpand(tab_box_, FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(tabs_scroll), tab_box_);

    new_tab_btn_ = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_set_tooltip_text(new_tab_btn_, "Yeni sekme");
    gtk_widget_add_css_class(new_tab_btn_, "flat");

    // Tab alanı: sadece scroll — pack_start ile sola yaslı
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), tabs_scroll);
    // title_widget boş (ortalama placeholder kaldırıldı)
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header),
        gtk_label_new(nullptr));

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
    g_menu_append_section(menu_model, "Görünüm", G_MENU_MODEL(view_section));
    g_object_unref(view_section);

    // Araçlar bölümü
    GMenu* tools_section = g_menu_new();
    g_menu_append(tools_section,  "Geçmiş",                    "win.show-history");
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
        {"show-history"}, {"toggle-bookmarks-bar"}
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
    gtk_box_append(GTK_BOX(content_box), stack_);

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
            return FALSE;
        }), this);
    gtk_widget_add_controller(window_, key_ctrl);

    // İlk tab (ana sayfa)
    NewTab(kHomePage);

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
        "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36");
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
    gtk_widget_set_margin_start(row, 4);
    gtk_widget_set_margin_end(row, 4);
    gtk_widget_set_margin_top(row, 3);
    gtk_widget_set_margin_bottom(row, 3);
    gtk_widget_set_size_request(row, 80, -1);
    gtk_widget_set_hexpand(row, FALSE);

    // #id etiketi (sabit, kısa)
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

    // Tab başlığı etiketi — sola yaslı
    tab->label = gtk_label_new("Yeni Sekme");
    gtk_label_set_max_width_chars(GTK_LABEL(tab->label), 14);
    gtk_label_set_ellipsize(GTK_LABEL(tab->label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(tab->label, TRUE);
    gtk_widget_set_halign(tab->label, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(tab->label), 0.0f);

    GtkWidget* close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(close_btn, "flat");
    gtk_widget_add_css_class(close_btn, "circular");
    gtk_widget_set_focus_on_click(close_btn, FALSE);
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
    std::string wt = title ? (std::string(title) + " — " + kAppName) : kAppName;
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
    auto results = HistoryManager::Get().Query(text, 8);
    if (results.empty()) { HideUrlSuggestions(); return; }

    if (!suggest_pop_) {
        suggest_pop_ = gtk_popover_new();
        gtk_popover_set_has_arrow(GTK_POPOVER(suggest_pop_), FALSE);
        gtk_popover_set_autohide(GTK_POPOVER(suggest_pop_), FALSE);
        gtk_widget_set_parent(suggest_pop_, url_entry_);
        suggest_list_ = gtk_list_box_new();
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(suggest_list_), GTK_SELECTION_BROWSE);
        gtk_widget_set_size_request(suggest_list_, 500, -1);
        gtk_popover_set_child(GTK_POPOVER(suggest_pop_), suggest_list_);
        g_signal_connect(suggest_list_, "row-activated",
            G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer ud) {
                auto* self = static_cast<BrowserWindow*>(ud);
                const char* url = static_cast<const char*>(
                    g_object_get_data(G_OBJECT(row), "url"));
                if (url && self->active_tab_) {
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

    for (const auto& e : results) {
        GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_margin_start(row_box, 8);
        gtk_widget_set_margin_end(row_box, 8);
        gtk_widget_set_margin_top(row_box, 4);
        gtk_widget_set_margin_bottom(row_box, 4);

        GtkWidget* url_lbl = gtk_label_new(e.url.c_str());
        gtk_label_set_ellipsize(GTK_LABEL(url_lbl), PANGO_ELLIPSIZE_MIDDLE);
        gtk_widget_set_halign(url_lbl, GTK_ALIGN_START);
        gtk_widget_add_css_class(url_lbl, "caption");

        gtk_box_append(GTK_BOX(row_box), url_lbl);

        if (!e.title.empty()) {
            GtkWidget* title_lbl = gtk_label_new(e.title.c_str());
            gtk_label_set_ellipsize(GTK_LABEL(title_lbl), PANGO_ELLIPSIZE_END);
            gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
            gtk_widget_add_css_class(title_lbl, "dim-label");
            gtk_box_append(GTK_BOX(row_box), title_lbl);
        }

        GtkWidget* row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
        g_object_set_data_full(G_OBJECT(row), "url",
            g_strdup(e.url.c_str()), g_free);
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
            strncmp(uri, "ferzan://", 9) != 0 &&
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
        std::string wt = tab->title.empty() ? kAppName :
                         (tab->title + " — " + kAppName);
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
        gtk_label_set_text(GTK_LABEL(status_bar_), link_uri ? link_uri : "");
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
        self->NewTab("ferzan://gecmis");
    else if (!g_strcmp0(name, "toggle-bookmarks-bar"))
        self->ToggleBookmarksBar();
    else if (!g_strcmp0(name, "clear-history")) {
        GtkAlertDialog* dlg = gtk_alert_dialog_new("Geçmişi temizle");
        gtk_alert_dialog_set_detail(dlg, "Çerezler ve geçmiş temizlensin mi?");
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
                            WEBKIT_WEBSITE_DATA_COOKIES |
                            WEBKIT_WEBSITE_DATA_MEMORY_CACHE |
                            WEBKIT_WEBSITE_DATA_DISK_CACHE |
                            WEBKIT_WEBSITE_DATA_SESSION_STORAGE),
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
    else if (!g_strcmp0(name, "settings"))  self->NewTab("ferzan://ayarlar");
    else if (!g_strcmp0(name, "about"))     self->NewTab("ferzan://ayarlar/hakkimizda");
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

    // ferzan:// dahili sayfaları intercept et
    {
        WebKitNavigationPolicyDecision* nd = WEBKIT_NAVIGATION_POLICY_DECISION(dec);
        WebKitNavigationAction* na = webkit_navigation_policy_decision_get_navigation_action(nd);
        WebKitURIRequest* req = webkit_navigation_action_get_request(na);
        const char* req_uri = webkit_uri_request_get_uri(req);
        if (req_uri && g_str_has_prefix(req_uri, "ferzan://") &&
            !g_str_has_prefix(req_uri, "ferzan://home")) {
            webkit_policy_decision_ignore(dec);
            // idle'da çağır: tab yaratıldıktan sonra wv active_tab ile eşleşsin
            struct Ctx { BrowserWindow* self; std::string uri; };
            auto* ctx = new Ctx{ this, req_uri };
            g_idle_add([](gpointer ud) -> gboolean {
                auto* c = static_cast<Ctx*>(ud);
                // TabForWebView ile doğru tabı bul
                c->self->HandleFerzanScheme(c->uri);
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
    // Link bağlamında değilsek dokunsunma
    if (!webkit_hit_test_result_context_is_link(hit)) return false;

    const char* link_uri = webkit_hit_test_result_get_link_uri(hit);
    if (!link_uri || !*link_uri) return false;

    // Mevcut "Yeni Pencerede Aç" öğesini kaldır — biz handle edeceğiz
    // (OPEN_LINK_IN_NEW_WINDOW stock action'u zaten var, onu silip custom ekleyelim)

    // Ayracı ekle (eğer menü boş değilse)
    if (webkit_context_menu_get_n_items(menu) > 0)
        webkit_context_menu_append(menu, webkit_context_menu_item_new_separator());

    // "Yeni Sekmede Aç" — custom GAction
    GSimpleAction* open_tab_act = g_simple_action_new("open-in-new-tab", G_VARIANT_TYPE_STRING);
    g_signal_connect(open_tab_act, "activate",
        G_CALLBACK(+[](GSimpleAction*, GVariant* param, gpointer ud) {
            const char* uri = g_variant_get_string(param, nullptr);
            if (uri && *uri)
                static_cast<BrowserWindow*>(ud)->NewTab(uri);
        }), this);
    GVariant* uri_variant = g_variant_new_string(link_uri);
    WebKitContextMenuItem* item_tab = webkit_context_menu_item_new_from_gaction(
        G_ACTION(open_tab_act), "Yeni Sekmede Aç", uri_variant);
    webkit_context_menu_append(menu, item_tab);
    g_object_unref(open_tab_act);

    // "Yeni Pencerede Aç" — custom action ile gerçek yeni pencere
    GSimpleAction* open_win_act = g_simple_action_new("open-in-new-window-ctx", G_VARIANT_TYPE_STRING);
    g_signal_connect(open_win_act, "activate",
        G_CALLBACK(+[](GSimpleAction*, GVariant* param, gpointer ud) {
            const char* uri = g_variant_get_string(param, nullptr);
            if (uri && *uri)
                static_cast<BrowserWindow*>(ud)->OpenInNewWindow(uri);
        }), this);
    GVariant* uri_win_variant = g_variant_new_string(link_uri);
    WebKitContextMenuItem* item_win = webkit_context_menu_item_new_from_gaction(
        G_ACTION(open_win_act), "Yeni Pencerede Aç", uri_win_variant);
    webkit_context_menu_append(menu, item_win);
    g_object_unref(open_win_act);

    return false;  // false → menüyü göster
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
        "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36");
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
    gtk_widget_set_margin_start(row, 4); gtk_widget_set_margin_end(row, 4);
    gtk_widget_set_margin_top(row, 3);   gtk_widget_set_margin_bottom(row, 3);
    gtk_widget_set_size_request(row, 80, -1);
    gtk_widget_set_hexpand(row, FALSE);

    std::string id_str = "#" + std::to_string(tab->id);
    GtkWidget* id_label = gtk_label_new(id_str.c_str());
    gtk_widget_add_css_class(id_label, "tab-id-label");
    gtk_widget_set_hexpand(id_label, FALSE);
    gtk_widget_set_valign(id_label, GTK_ALIGN_CENTER);

    tab->favicon = gtk_image_new();
    gtk_image_set_pixel_size(GTK_IMAGE(tab->favicon), 16);
    gtk_widget_set_valign(tab->favicon, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(tab->favicon, FALSE);
    gtk_widget_set_visible(tab->favicon, FALSE);

    tab->label = gtk_label_new("Yeni Sekme");
    gtk_label_set_max_width_chars(GTK_LABEL(tab->label), 14);
    gtk_label_set_ellipsize(GTK_LABEL(tab->label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(tab->label, TRUE);
    gtk_widget_set_halign(tab->label, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(tab->label), 0.0f);

    GtkWidget* close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(close_btn, "flat");
    gtk_widget_add_css_class(close_btn, "circular");
    gtk_widget_set_focus_on_click(close_btn, FALSE);
    gtk_widget_set_tooltip_text(close_btn, "Sekmeyi kapat");
    g_object_set_data(G_OBJECT(close_btn), "tab", tab);
    g_signal_connect(close_btn, "clicked",
        G_CALLBACK(+[](GtkButton* btn, gpointer ud) {
            static_cast<BrowserWindow*>(ud)->CloseTab(
                static_cast<Tab*>(g_object_get_data(G_OBJECT(btn), "tab")));
        }), this);

    gtk_box_append(GTK_BOX(row), id_label);
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
<title>Ferzan Browser</title><style>
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
<div class="logo">Ferzan Browser</div>
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
        "<a href=\"ferzan://ayarlar/genel\""     + a("genel")     + ">🔧 Genel</a>"
        "<a href=\"ferzan://ayarlar/gorunum\""    + a("gorunum")   + ">🎨 Görünüm</a>"
        "<a href=\"ferzan://ayarlar/sekmeler\""   + a("sekmeler")  + ">📑 Sekmeler</a>"
        "<a href=\"ferzan://ayarlar/gizlilik\""   + a("gizlilik")  + ">🔒 Gizlilik</a>"
        "<a href=\"ferzan://ayarlar/gelismis\""   + a("gelismis")  + ">⚙ Gelişmiş</a>"
        "<div class=\"divider\"></div>"
        "<a href=\"ferzan://ayarlar/hakkimizda\"" + a("hakkimizda") + ">ℹ Hakkımızda</a>";
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
      <input type="text" id="dldir" placeholder="~/İndirilenler (varsayılan)"
        value=")css" + p.download_dir + R"css(">
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
    window.location.href='ferzan://ayarlar-kaydet?hp='+encodeURIComponent(hp)
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
        <span>Sayfa önbelleği <code>~/.cache/ferzan-browser</code> konumunda tutulur.</span>
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
function doClean(){window.location.href='ferzan://gecmis-temizle';}
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
function doClean(){window.location.href='ferzan://gecmis-temizle';}
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
  <div class="logo">Ferzan Browser</div>
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
    <a class="link" href="ferzan://ayarlar">Ayarlar</a>
  </div>
  <div class="copy">&copy; 2026 Ferzan Project — MIT Lisansı</div>
</div>
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
    window.location.href='ferzan://ayarlar-kaydet?zoom='+zoom
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
      <input type="text" id="newtab" placeholder="ferzan://home"
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
    window.location.href='ferzan://ayarlar-kaydet?maxtabs='+maxtabs
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
      <input type="text" id="dldir" placeholder="~/İndirilenler (varsayılan)"
        value=")css" + p.download_dir + R"css(">
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
        value="Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"
        readonly style="background:#f5f5f5;color:#888;cursor:not-allowed">
      <span style="font-size:.78rem;color:#aaa">Chrome 131 kimliği kullanılıyor (değiştirilemez).</span>
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
    window.location.href='ferzan://ayarlar-kaydet?js='+js+'&hw='+hw
      +'&dldir='+encodeURIComponent(dldir)+'&histdays='+histdays;
    toast('Gelişmiş ayarlar kaydedildi.',true);
  }catch(err){toast('Hata oluştu!',false);}
}
</script>
</body></html>)html";
}

void BrowserWindow::ShowSettingsPage() {
    NewTab("ferzan://ayarlar");
}

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
            if (bl.size() > 32) bl = bl.substr(0, 30) + "…";
            GtkWidget* item_btn = gtk_button_new_with_label(bl.c_str());
            gtk_widget_add_css_class(item_btn, "flat");
            gtk_widget_set_tooltip_text(item_btn, bm.url.c_str());
            g_object_set_data_full(G_OBJECT(item_btn), "bm-url",
                g_strdup(bm.url.c_str()), g_free);
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
            gtk_box_append(GTK_BOX(pop_box), item_btn);
        }

        gtk_popover_set_child(GTK_POPOVER(pop), pop_box);
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(folder_btn), pop);
        gtk_box_append(GTK_BOX(bookmarks_box_), folder_btn);
    }

    // Kök seviyesindeki favoriler (klasörsüz)
    for (const auto& bm : bmarks) {
        if (!bm.folder.empty()) continue;

        std::string label = bm.title.empty() ? bm.url : bm.title;
        if (label.size() > 28) label = label.substr(0, 26) + "…";

        // Her favori için küçük bir kutu: [buton] [▾ düzenle]
        GtkWidget* item_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

        GtkWidget* btn = gtk_button_new_with_label(label.c_str());
        gtk_widget_add_css_class(btn, "flat");
        gtk_widget_set_tooltip_text(btn, bm.url.c_str());
        g_object_set_data_full(G_OBJECT(btn), "bm-url",
            g_strdup(bm.url.c_str()), g_free);
        g_signal_connect(btn, "clicked",
            G_CALLBACK(+[](GtkButton* b, gpointer ud) {
                auto* self = static_cast<BrowserWindow*>(ud);
                const char* url = static_cast<const char*>(
                    g_object_get_data(G_OBJECT(b), "bm-url"));
                if (url && self->active_tab_)
                    webkit_web_view_load_uri(
                        WEBKIT_WEB_VIEW(self->active_tab_->webview), url);
            }), this);

        // Düzenle butonu (küçük ▾)
        GtkWidget* edit_btn = gtk_menu_button_new();
        gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(edit_btn), "pan-down-symbolic");
        gtk_widget_add_css_class(edit_btn, "flat");
        gtk_widget_set_tooltip_text(edit_btn, "Düzenle");

        // Düzenleme popover'ı
        GtkWidget* edit_pop = gtk_popover_new();
        gtk_popover_set_has_arrow(GTK_POPOVER(edit_pop), TRUE);

        GtkWidget* ep_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_margin_start(ep_box, 10);
        gtk_widget_set_margin_end(ep_box, 10);
        gtk_widget_set_margin_top(ep_box, 10);
        gtk_widget_set_margin_bottom(ep_box, 10);

        // Ad değiştirme satırı
        GtkWidget* name_lbl = gtk_label_new("Ad:");
        gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
        GtkWidget* name_entry = gtk_entry_new();
        gtk_editable_set_text(GTK_EDITABLE(name_entry),
            (bm.title.empty() ? bm.url : bm.title).c_str());
        gtk_widget_set_size_request(name_entry, 180, -1);

        // Klasör satırı
        GtkWidget* folder_lbl = gtk_label_new("Klasör:");
        gtk_widget_set_halign(folder_lbl, GTK_ALIGN_START);
        GtkWidget* folder_entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(folder_entry), "Klasör adı (boş=kök)");
        gtk_editable_set_text(GTK_EDITABLE(folder_entry), bm.folder.c_str());
        gtk_widget_set_size_request(folder_entry, 180, -1);

        // Kaydet butonu
        GtkWidget* save_btn = gtk_button_new_with_label("Kaydet");
        gtk_widget_add_css_class(save_btn, "suggested-action");

        struct EditCtx {
            BrowserWindow* self;
            GtkWidget* name_entry;
            GtkWidget* folder_entry;
            GtkWidget* pop;
            std::string url;
        };
        auto* ectx = new EditCtx{ this, name_entry, folder_entry, edit_pop, bm.url };
        g_signal_connect(save_btn, "clicked",
            G_CALLBACK(+[](GtkButton*, gpointer ud) {
                auto* c = static_cast<EditCtx*>(ud);
                const char* new_name   = gtk_editable_get_text(GTK_EDITABLE(c->name_entry));
                const char* new_folder = gtk_editable_get_text(GTK_EDITABLE(c->folder_entry));
                BookmarkManager::Get().Rename(c->url, new_name ? new_name : "");
                BookmarkManager::Get().MoveToFolder(c->url, new_folder ? new_folder : "");
                gtk_popover_popdown(GTK_POPOVER(c->pop));
                c->self->RebuildBookmarksBar();
            }), ectx);
        g_signal_connect(edit_pop, "closed",
            G_CALLBACK(+[](GtkPopover*, gpointer ud) { delete static_cast<EditCtx*>(ud); }),
            ectx);

        gtk_box_append(GTK_BOX(ep_box), name_lbl);
        gtk_box_append(GTK_BOX(ep_box), name_entry);
        gtk_box_append(GTK_BOX(ep_box), folder_lbl);
        gtk_box_append(GTK_BOX(ep_box), folder_entry);
        gtk_box_append(GTK_BOX(ep_box), save_btn);
        gtk_popover_set_child(GTK_POPOVER(edit_pop), ep_box);
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(edit_btn), edit_pop);

        gtk_box_append(GTK_BOX(item_box), btn);
        gtk_box_append(GTK_BOX(item_box), edit_btn);
        gtk_box_append(GTK_BOX(bookmarks_box_), item_box);
    }
}

void BrowserWindow::ToggleBookmarksBar() {
    bookmarks_visible_ = !bookmarks_visible_;
    gtk_revealer_set_reveal_child(
        GTK_REVEALER(bookmarks_bar_), bookmarks_visible_);
}

void BrowserWindow::HandleFerzanScheme(const std::string& uri) {
    if (!active_tab_) return;
    auto* wv = WEBKIT_WEB_VIEW(active_tab_->webview);
    std::string html;
    std::string title;

    if (uri == "ferzan://ayarlar" || uri == "ferzan://ayarlar/genel" ||
        uri.rfind("ferzan://ayarlar?", 0) == 0) {
        html  = BuildSettingsHTML("genel");
        title = "Genel Ayarlar — Ferzan";
    } else if (uri == "ferzan://ayarlar/gorunum") {
        html  = BuildAppearanceSettingsHTML();
        title = "Görünüm — Ferzan";
    } else if (uri == "ferzan://ayarlar/sekmeler") {
        html  = BuildTabsSettingsHTML();
        title = "Sekmeler — Ferzan";
    } else if (uri == "ferzan://ayarlar/gelismis") {
        html  = BuildAdvancedSettingsHTML();
        title = "Gelişmiş — Ferzan";
    } else if (uri == "ferzan://ayarlar/hakkimizda") {
        html  = BuildAboutHTML();
        title = "Hakkımızda — Ferzan";
    } else if (uri == "ferzan://ayarlar/gizlilik") {
        html  = BuildPrivacyHTML();
        title = "Gizlilik — Ferzan";
    } else if (uri == "ferzan://gecmis") {
        html  = BuildHistoryHTML();
        title = "Geçmiş — Ferzan";
    } else if (uri.rfind("ferzan://gecmis-temizle", 0) == 0) {
        // Geçmişi temizle — sayfa içinden onay alındıktan sonra
        HistoryManager::Get().Clear();
        WebKitNetworkSession* ns = SessionManager::Get().GetSession();
        WebKitWebsiteDataManager* dm = webkit_network_session_get_website_data_manager(ns);
        webkit_website_data_manager_clear(dm,
            static_cast<WebKitWebsiteDataTypes>(
                WEBKIT_WEBSITE_DATA_COOKIES | WEBKIT_WEBSITE_DATA_MEMORY_CACHE |
                WEBKIT_WEBSITE_DATA_DISK_CACHE | WEBKIT_WEBSITE_DATA_SESSION_STORAGE),
            0, nullptr, nullptr, nullptr);
        webkit_web_view_load_uri(wv, "ferzan://gecmis");
        return;
    } else if (uri.rfind("ferzan://favori-yeniden-adlandir", 0) == 0) {
        // ferzan://favori-yeniden-adlandir?url=...&title=...
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
    } else if (uri.rfind("ferzan://favori-klasor", 0) == 0) {
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
    } else if (uri.rfind("ferzan://ayarlar-kaydet", 0) == 0) {
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
        std::string fontsize = parse_param("fontsize");
        std::string minfont  = parse_param("minfont");
        if (!hp.empty())       prefs.homepage          = hp;
        if (!zoom.empty())     prefs.default_zoom      = std::stod(zoom) / 100.0;
        if (!js.empty())       prefs.javascript_enabled= (js == "1");
        if (!hw.empty())       prefs.hardware_accel    = (hw == "1");
        if (!se.empty())       prefs.search_engine     = se;
        prefs.download_dir = dldir;
        if (!histdays.empty()) prefs.history_days      = std::stoi(histdays);
        if (!maxtabs.empty())  prefs.max_tabs          = std::stoi(maxtabs);
        if (!restore.empty())  prefs.restore_tabs      = (restore == "1");
        if (!fontsize.empty()) prefs.font_size         = std::stoi(fontsize);
        if (!minfont.empty())  prefs.min_font_size     = std::stoi(minfont);
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
        webkit_web_view_load_uri(wv, "ferzan://ayarlar");
        return;
    } else {
        return;  // bilinmeyen ferzan:// — zaten home tarafından handle ediliyor
    }

    webkit_web_view_load_html(wv, html.c_str(), nullptr);
    active_tab_->title = title;
    active_tab_->url   = uri;  // URL bar'da ferzan:// görünsün
    UpdateTabLabel(active_tab_);
    // URL barını güncelle (programatik — focus yoksa changed sinyali popup açmaz)
    gtk_editable_set_text(GTK_EDITABLE(url_entry_), uri.c_str());
    gtk_window_set_title(GTK_WINDOW(window_), (title + " — " + kAppName).c_str());
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

} // namespace ferzan
