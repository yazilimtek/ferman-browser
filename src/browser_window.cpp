#include "browser_window.h"
#include <algorithm>
#include <string>
#include <sstream>
#include <cstdio>
#include <cctype>

static const char* kHomeHTML = R"html(
<!DOCTYPE html><html lang="tr"><head><meta charset="UTF-8">
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
<input id="q" type="text" placeholder="Google'da ara veya adres gir..." autocomplete="off" autofocus>
<button class="btn" type="submit">Ara</button>
</form>
<div class="sc">
<a href="https://google.com"><div class="ic">&#9654;</div><span>Google</span></a>
<a href="https://github.com"><div class="ic">&#128025;</div><span>GitHub</span></a>
<a href="https://youtube.com"><div class="ic">&#128309;</div><span>Youtube</span></a>
<a href="https://wikipedia.org"><div class="ic">&#128214;</div><span>Wikipedia</span></a>
</div>
<script>
function doSearch(e){e.preventDefault();
var q=document.getElementById('q').value.trim();if(!q)return;
if(/^https?:\/\//i.test(q)||/^[a-z0-9-]+\.[a-z]{2,}/i.test(q))
  window.location.href=q.startsWith('http')?q:'https://'+q;
else window.location.href='https://www.google.com/search?q='+encodeURIComponent(q);}
</script></body></html>
)html";

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
)";

namespace ferzan {

// ── Constructor ──────────────────────────────────────────────────────────────

BrowserWindow::BrowserWindow(GtkApplication* app) {
    app_    = app;
    window_ = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window_), kAppName);
    gtk_window_set_default_size(GTK_WINDOW(window_), 1280, 800);
    g_object_set_data(G_OBJECT(window_), "browser-window", this);

    // ── Header bar ──
    GtkWidget* header = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(window_), header);

    // Favicon DB etkinleştir
    WebKitNetworkSession* net_session = webkit_network_session_get_default();
    WebKitWebsiteDataManager* data_mgr =
        webkit_network_session_get_website_data_manager(net_session);
    webkit_website_data_manager_set_favicons_enabled(data_mgr, TRUE);

    // Nav butonları (sol)
    GtkWidget* nav_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(nav_box, "linked");
    back_btn_   = gtk_button_new_from_icon_name("go-previous-symbolic");
    fwd_btn_    = gtk_button_new_from_icon_name("go-next-symbolic");
    reload_btn_ = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_sensitive(back_btn_, FALSE);
    gtk_widget_set_sensitive(fwd_btn_,  FALSE);
    gtk_box_append(GTK_BOX(nav_box), back_btn_);
    gtk_box_append(GTK_BOX(nav_box), fwd_btn_);
    gtk_box_append(GTK_BOX(nav_box), reload_btn_);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), nav_box);

    // Tab çubuğu (orta — title widget)
    // tab_box_: scroll olmadan yatay kutu; tablar + [+] butonu
    GtkWidget* tabs_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tabs_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_set_hexpand(tabs_scroll, TRUE);
    gtk_widget_set_size_request(tabs_scroll, 200, -1);

    tab_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(tabs_scroll), tab_box_);

    new_tab_btn_ = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_set_tooltip_text(new_tab_btn_, "Yeni sekme");
    gtk_widget_add_css_class(new_tab_btn_, "flat");

    GtkWidget* title_area = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_hexpand(title_area, TRUE);
    gtk_box_append(GTK_BOX(title_area), tabs_scroll);
    gtk_box_append(GTK_BOX(title_area), new_tab_btn_);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title_area);

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
    g_menu_append(view_section,   "Yakınlaştır\t\t\tCtrl++",  "win.zoom-in");
    g_menu_append(view_section,   "Uzaklaştır\t\t\tCtrl+-",  "win.zoom-out");
    g_menu_append(view_section,   "Varsayılan Boyut\t\tCtrl+0", "win.zoom-reset");
    g_menu_append(view_section,   "Tam Ekran\t\t\tF11",       "win.fullscreen");
    g_menu_append_section(menu_model, "Görünüm", G_MENU_MODEL(view_section));
    g_object_unref(view_section);

    // Araçlar bölümü
    GMenu* tools_section = g_menu_new();
    g_menu_append(tools_section,  "Sayfayı Kaydet\t\tCtrl+S",  "win.save-page");
    g_menu_append(tools_section,  "Sayfayı Yazdır\t\tCtrl+P",  "win.print-page");
    g_menu_append(tools_section,  "Geçmişi Temizle",            "win.clear-history");
    g_menu_append_section(menu_model, "Araçlar", G_MENU_MODEL(tools_section));
    g_object_unref(tools_section);

    // Ayarlar / Hakkında
    GMenu* sys_section = g_menu_new();
    g_menu_append(sys_section,    "Ayarlar",                  "win.settings");
    g_menu_append(sys_section,    "Hakkında",                  "win.about");
    g_menu_append_section(menu_model, "", G_MENU_MODEL(sys_section));
    g_object_unref(sys_section);

    // GActionGroup — window actions
    GSimpleActionGroup* ag = g_simple_action_group_new();
    struct { const char* name; } actions[] = {
        {"new-tab"}, {"new-window"}, {"close-tab"},
        {"reload"}, {"go-back"}, {"go-forward"}, {"go-home"},
        {"zoom-in"}, {"zoom-out"}, {"zoom-reset"},
        {"save-page"}, {"print-page"}, {"fullscreen"},
        {"clear-history"}, {"settings"}, {"about"}
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
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_btn_);

    // ── İçerik alanı: URL bar + stack ──
    GtkWidget* content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    // URL bar satırı
    GtkWidget* url_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
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

    fav_btn_ = gtk_button_new_from_icon_name("bookmark-new-symbolic");
    gtk_widget_add_css_class(fav_btn_, "flat");
    gtk_widget_set_tooltip_text(fav_btn_, "Favorilere ekle");

    GtkWidget* entry_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(entry_row, TRUE);
    gtk_box_append(GTK_BOX(entry_row), url_entry_);
    gtk_box_append(GTK_BOX(entry_row), clear_btn);
    gtk_box_append(GTK_BOX(entry_row), copy_btn);
    gtk_box_append(GTK_BOX(entry_row), fav_btn_);
    gtk_box_append(GTK_BOX(url_bar), entry_row);

    gtk_box_append(GTK_BOX(content_box), url_bar);

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
    g_signal_connect(back_btn_,   "clicked",  G_CALLBACK(OnBackCb),      this);
    g_signal_connect(fwd_btn_,    "clicked",  G_CALLBACK(OnForwardCb),   this);
    g_signal_connect(reload_btn_, "clicked",  G_CALLBACK(OnReloadCb),    this);
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
    auto* tab = new Tab();
    tab->id   = next_tab_id_++;
    tab->url  = url;

    // WebView
    tab->webview = webkit_web_view_new();
    WebKitSettings* wk_settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(tab->webview));
    // Chrome UA — Google ve modern siteler için (tam platform bilgisi dahil)
    webkit_settings_set_user_agent(wk_settings,
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36");
    webkit_settings_set_enable_javascript(wk_settings, TRUE);
    webkit_settings_set_enable_media(wk_settings, TRUE);
    webkit_settings_set_javascript_can_open_windows_automatically(wk_settings, TRUE);
    webkit_settings_set_allow_modal_dialogs(wk_settings, TRUE);
    webkit_settings_set_enable_smooth_scrolling(wk_settings, FALSE);
    webkit_settings_set_media_playback_requires_user_gesture(wk_settings, FALSE);
    // Donanım hızlandırma KAPALI — pointer event offset sorununu giderir
    webkit_settings_set_hardware_acceleration_policy(
        wk_settings, WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
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

    // Favicon (başlangıçta generic ikon)
    tab->favicon = gtk_image_new_from_icon_name("text-html-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(tab->favicon), 16);
    gtk_widget_set_valign(tab->favicon, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(tab->favicon, FALSE);

    // Başlık etiketi
    tab->label = gtk_label_new("Yeni Sekme");
    gtk_label_set_max_width_chars(GTK_LABEL(tab->label), 12);
    gtk_label_set_ellipsize(GTK_LABEL(tab->label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(tab->label, TRUE);
    gtk_widget_set_halign(tab->label, GTK_ALIGN_START);

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
            webkit_web_view_load_html(WEBKIT_WEB_VIEW(tab->webview), kHomeHTML, nullptr);
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
    }
}

void BrowserWindow::OnUriChanged(WebKitWebView* wv) {
    Tab* tab = TabForWebView(wv);
    if (!tab) return;
    const char* uri = webkit_web_view_get_uri(wv);
    tab->url = uri ? uri : "";
    UpdateTabLabel(tab);
    if (tab == active_tab_) UpdateUrlEntry();
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

    // Önce webkit_web_view_get_favicon() dene (sync, cache'den gelir)
    GdkTexture* texture = webkit_web_view_get_favicon(wv);
    if (texture) {
        gtk_image_set_from_paintable(GTK_IMAGE(tab->favicon), GDK_PAINTABLE(texture));
        gtk_image_set_pixel_size(GTK_IMAGE(tab->favicon), 16);
        return;
    }

    // Sync API null döndü — FaviconDatabase ile async sorgula
    const char* page_uri = webkit_web_view_get_uri(wv);
    if (!page_uri || !*page_uri) return;

    WebKitNetworkSession* ns = webkit_network_session_get_default();
    WebKitWebsiteDataManager* dm = webkit_network_session_get_website_data_manager(ns);
    WebKitFaviconDatabase* fdb = webkit_website_data_manager_get_favicon_database(dm);
    if (!fdb) return;

    // favicon_widget ve page_uri'yi callback için paket haline getir
    struct Ctx { GtkWidget* img; };
    auto* ctx = new Ctx{ tab->favicon };
    webkit_favicon_database_get_favicon(fdb, page_uri, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer ud) {
            auto* ctx = static_cast<Ctx*>(ud);
            GError* err = nullptr;
            GdkTexture* tex = webkit_favicon_database_get_favicon_finish(
                WEBKIT_FAVICON_DATABASE(src), res, &err);
            if (tex && GTK_IS_IMAGE(ctx->img)) {
                gtk_image_set_from_paintable(GTK_IMAGE(ctx->img), GDK_PAINTABLE(tex));
                gtk_image_set_pixel_size(GTK_IMAGE(ctx->img), 16);
                g_object_unref(tex);
            }
            if (err) g_error_free(err);
            delete ctx;
        }, ctx);
}

void BrowserWindow::OnMouseTargetChanged(WebKitWebView* wv,
                                          WebKitHitTestResult* hit) {
    // Sadece aktif sekmenin hover bilgisini göster
    Tab* tab = TabForWebView(wv);
    if (!tab || tab != active_tab_) return;

    if (webkit_hit_test_result_context_is_link(hit)) {
        const char* link = webkit_hit_test_result_get_link_uri(hit);
        if (link && *link) {
            gtk_label_set_text(GTK_LABEL(status_bar_), link);
            gtk_widget_set_visible(status_bar_, TRUE);
            return;
        }
    }
    gtk_label_set_text(GTK_LABEL(status_bar_), "");
    gtk_widget_set_visible(status_bar_, FALSE);
}

void BrowserWindow::OnUrlActivate() {
    if (!active_tab_) return;
    std::string input = gtk_editable_get_text(GTK_EDITABLE(url_entry_));
    if (input.empty()) return;

    std::string url;
    // Arama sorgusu mu, URL mi?
    bool looks_like_url = (input.find("://") != std::string::npos) ||
                          (input.find('.') != std::string::npos &&
                           input.find(' ') == std::string::npos);
    if (looks_like_url) {
        url = (input.find("://") == std::string::npos) ? "https://" + input : input;
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
        url = "https://www.google.com/search?q=" + enc;
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
    static_cast<BrowserWindow*>(ud)->NewTab(kHomePage);
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
    if      (!g_strcmp0(name, "new-tab"))       self->NewTab(self->kHomePage);
    else if (!g_strcmp0(name, "new-window"))    self->OpenInNewWindow(self->kHomePage);
    else if (!g_strcmp0(name, "close-tab"))     { if (self->active_tab_) self->CloseTab(self->active_tab_); }
    else if (!g_strcmp0(name, "go-home"))       self->NewTab(self->kHomePage);
    else if (!g_strcmp0(name, "reload"))        self->OnReload();
    else if (!g_strcmp0(name, "go-back"))       self->OnBack();
    else if (!g_strcmp0(name, "go-forward"))    self->OnForward();
    else if (!g_strcmp0(name, "zoom-in")) {
        if (self->active_tab_) {
            auto* wv = WEBKIT_WEB_VIEW(self->active_tab_->webview);
            webkit_web_view_set_zoom_level(wv,
                std::min(webkit_web_view_get_zoom_level(wv) + 0.1, 5.0));
        }
    }
    else if (!g_strcmp0(name, "zoom-out")) {
        if (self->active_tab_) {
            auto* wv = WEBKIT_WEB_VIEW(self->active_tab_->webview);
            webkit_web_view_set_zoom_level(wv,
                std::max(webkit_web_view_get_zoom_level(wv) - 0.1, 0.25));
        }
    }
    else if (!g_strcmp0(name, "zoom-reset")) {
        if (self->active_tab_)
            webkit_web_view_set_zoom_level(
                WEBKIT_WEB_VIEW(self->active_tab_->webview), 1.0);
    }
    else if (!g_strcmp0(name, "clear-history")) {
        GtkAlertDialog* dlg = gtk_alert_dialog_new("Geçmişi temizle");
        gtk_alert_dialog_set_detail(dlg, "Çerezler ve geçmiş temizlensin mi?");
        const char* btns[] = { "İptal", "Temizle", nullptr };
        gtk_alert_dialog_set_buttons(dlg, btns);
        gtk_alert_dialog_set_cancel_button(dlg, 0);
        gtk_alert_dialog_set_default_button(dlg, 1);
        gtk_alert_dialog_choose(dlg, GTK_WINDOW(self->window_), nullptr,
            [](GObject* src, GAsyncResult* res, gpointer ud) {
                int btn = gtk_alert_dialog_choose_finish(
                    GTK_ALERT_DIALOG(src), res, nullptr);
                if (btn == 1) {
                    WebKitNetworkSession* ns = webkit_network_session_get_default();
                    WebKitWebsiteDataManager* dm =
                        webkit_network_session_get_website_data_manager(ns);
                    webkit_website_data_manager_clear(dm,
                        static_cast<WebKitWebsiteDataTypes>(
                            WEBKIT_WEBSITE_DATA_COOKIES |
                            WEBKIT_WEBSITE_DATA_MEMORY_CACHE |
                            WEBKIT_WEBSITE_DATA_DISK_CACHE |
                            WEBKIT_WEBSITE_DATA_SESSION_STORAGE),
                        0, nullptr, nullptr, nullptr);
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
    else if (!g_strcmp0(name, "settings")) { self->ShowSettingsWindow(); }
    else if (!g_strcmp0(name, "about")) {
        GtkAlertDialog* dlg = gtk_alert_dialog_new("Ferzan Browser");
        gtk_alert_dialog_set_detail(dlg,
            "Sürüm: 0.2.0\n"
            "Yapı: GTK4 + WebKitGTK 6.0\n"
            "Lisans: MIT");
        gtk_alert_dialog_show(dlg, GTK_WINDOW(self->window_));
        g_object_unref(dlg);
    }
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

bool BrowserWindow::OnDecidePolicy(WebKitWebView*,
                                    WebKitPolicyDecision* dec,
                                    WebKitPolicyDecisionType type) {
    // NEW_WINDOW_ACTION: "create" sinyali zaten yeni sekme açıyor;
    // burada ignore etmiyoruz — WebKit create sinyalini tetiklesin.
    if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        webkit_policy_decision_use(dec);
        return true;
    }

    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) return false;

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
        "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36");
    webkit_settings_set_enable_javascript(wk_settings, TRUE);
    webkit_settings_set_enable_media(wk_settings, TRUE);
    webkit_settings_set_javascript_can_open_windows_automatically(wk_settings, TRUE);
    webkit_settings_set_allow_modal_dialogs(wk_settings, TRUE);
    webkit_settings_set_enable_smooth_scrolling(wk_settings, FALSE);
    webkit_settings_set_media_playback_requires_user_gesture(wk_settings, FALSE);
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

    tab->favicon = gtk_image_new_from_icon_name("text-html-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(tab->favicon), 16);
    gtk_widget_set_valign(tab->favicon, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(tab->favicon, FALSE);

    tab->label = gtk_label_new("Yeni Sekme");
    gtk_label_set_max_width_chars(GTK_LABEL(tab->label), 12);
    gtk_label_set_ellipsize(GTK_LABEL(tab->label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(tab->label, TRUE);
    gtk_widget_set_halign(tab->label, GTK_ALIGN_START);

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

// ── Ayarlar penceresi ────────────────────────────────────────────────

void BrowserWindow::ShowSettingsWindow() {
    GtkWidget* dlg = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dlg), "Tarayıcı Ayarları");
    gtk_window_set_default_size(GTK_WINDOW(dlg), 480, 520);
    gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(window_));
    gtk_window_set_modal(GTK_WINDOW(dlg), FALSE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dlg), TRUE);

    GtkWidget* outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    // Başlık barı
    GtkWidget* hdr = gtk_header_bar_new();
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(hdr),
        gtk_label_new("Tarayıcı Ayarları"));
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hdr), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(dlg), hdr);

    // İçerik: kaydırılabilir
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(vbox, 18);
    gtk_widget_set_margin_end(vbox, 18);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 18);

    // — Yardımcı lambda'lar —
    auto make_section_label = [](const char* txt) {
        GtkWidget* lbl = gtk_label_new(nullptr);
        char* markup = g_strdup_printf("<b>%s</b>", txt);
        gtk_label_set_markup(GTK_LABEL(lbl), markup);
        g_free(markup);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_widget_set_margin_top(lbl, 18);
        gtk_widget_set_margin_bottom(lbl, 6);
        return lbl;
    };
    auto make_row = [](const char* label_txt, GtkWidget* control) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_set_margin_top(row, 4);
        gtk_widget_set_margin_bottom(row, 4);
        GtkWidget* lbl = gtk_label_new(label_txt);
        gtk_widget_set_hexpand(lbl, TRUE);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(row), lbl);
        gtk_box_append(GTK_BOX(row), control);
        return row;
    };

    // ══ Genel ══
    gtk_box_append(GTK_BOX(vbox), make_section_label("⛺ Genel"));

    // Ana sayfa
    GtkWidget* home_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(home_entry), "ferzan://home");
    gtk_widget_set_size_request(home_entry, 220, -1);
    gtk_box_append(GTK_BOX(vbox), make_row("Ana Sayfa", home_entry));

    // Arama motoru
    GtkWidget* engine_combo = gtk_drop_down_new_from_strings(
        (const char*[]){"Google", "DuckDuckGo", "Bing", "Yandex", nullptr});
    gtk_box_append(GTK_BOX(vbox), make_row("Varsayılan Arama Motoru", engine_combo));

    // ══ Görünüm ══
    gtk_box_append(GTK_BOX(vbox), make_section_label("🎨 Görünüm"));

    GtkWidget* zoom_spin = gtk_spin_button_new_with_range(25, 500, 25);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(zoom_spin), 100);
    gtk_box_append(GTK_BOX(vbox), make_row("Varsayılan Yakınlaştırma (%)", zoom_spin));

    GtkWidget* fonts_btn = gtk_button_new_with_label("Yazı Tiplerini Düzenle...");
    gtk_widget_add_css_class(fonts_btn, "flat");
    gtk_widget_set_halign(fonts_btn, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), fonts_btn);

    // ══ Gizlilik ══
    gtk_box_append(GTK_BOX(vbox), make_section_label("🔒 Gizlilik ve Güvenlik"));

    GtkWidget* cookies_sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(cookies_sw), TRUE);
    gtk_box_append(GTK_BOX(vbox), make_row("Çerezlere İzin Ver", cookies_sw));

    GtkWidget* js_sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(js_sw), TRUE);
    gtk_box_append(GTK_BOX(vbox), make_row("JavaScript", js_sw));

    GtkWidget* webrtc_sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(webrtc_sw), FALSE);
    gtk_box_append(GTK_BOX(vbox), make_row("WebRTC", webrtc_sw));

    GtkWidget* clear_btn2 = gtk_button_new_with_label("Çerezleri ve Geçmişi Temizle...");
    gtk_widget_add_css_class(clear_btn2, "destructive-action");
    gtk_widget_set_halign(clear_btn2, GTK_ALIGN_START);
    gtk_widget_set_margin_top(clear_btn2, 8);
    gtk_box_append(GTK_BOX(vbox), clear_btn2);

    // ══ İndirmeler ══
    gtk_box_append(GTK_BOX(vbox), make_section_label("📥 İndirmeler"));

    GtkWidget* dl_path = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(dl_path), g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD) ? g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD) : "");
    gtk_widget_set_size_request(dl_path, 220, -1);
    gtk_box_append(GTK_BOX(vbox), make_row("İndirme Klasörü", dl_path));

    GtkWidget* ask_dl_sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(ask_dl_sw), TRUE);
    gtk_box_append(GTK_BOX(vbox), make_row("İndirmeden Önce Sor", ask_dl_sw));

    // ══ Hakkında ══
    gtk_box_append(GTK_BOX(vbox), make_section_label("ℹ️ Hakkında"));
    GtkWidget* ver_lbl = gtk_label_new(
        "Ferzan Browser 0.2.0\nGTK4 + WebKitGTK 6.0\nLisans: MIT");
    gtk_widget_set_halign(ver_lbl, GTK_ALIGN_START);
    gtk_widget_add_css_class(ver_lbl, "dim-label");
    gtk_box_append(GTK_BOX(vbox), ver_lbl);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), vbox);
    gtk_box_append(GTK_BOX(outer), scroll);
    gtk_window_set_child(GTK_WINDOW(dlg), outer);
    gtk_window_present(GTK_WINDOW(dlg));
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
