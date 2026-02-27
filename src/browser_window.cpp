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
<a href="https://youtube.com"><div class="ic">&#9654;</div><span>YouTube</span></a>
<a href="https://github.com"><div class="ic">&#128025;</div><span>GitHub</span></a>
<a href="https://reddit.com"><div class="ic">&#128309;</div><span>Reddit</span></a>
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
    max-width: 200px;
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
    window_ = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window_), kAppName);
    gtk_window_set_default_size(GTK_WINDOW(window_), 1280, 800);

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

    GMenu* nav_section = g_menu_new();
    g_menu_append(nav_section, "Yeni Sekme",       "win.new-tab");
    g_menu_append(nav_section, "Sayfayı Yenile",   "win.reload");
    g_menu_append(nav_section, "Geri",             "win.go-back");
    g_menu_append(nav_section, "İleri",            "win.go-forward");
    g_menu_append_section(menu_model, "Gezinti", G_MENU_MODEL(nav_section));
    g_object_unref(nav_section);

    GMenu* tools_section = g_menu_new();
    g_menu_append(tools_section, "Sayfayı Kaydet (Ctrl+S)",    "win.save-page");
    g_menu_append(tools_section, "Sayfayı Yazdır (Ctrl+P)",    "win.print-page");
    g_menu_append(tools_section, "Tam Ekran (F11)",            "win.fullscreen");
    g_menu_append_section(menu_model, "Araçlar", G_MENU_MODEL(tools_section));
    g_object_unref(tools_section);

    GMenu* about_section = g_menu_new();
    g_menu_append(about_section, "Ferzan Browser Hakkında", "win.about");
    g_menu_append_section(menu_model, "", G_MENU_MODEL(about_section));
    g_object_unref(about_section);

    // GActionGroup — window actions
    GSimpleActionGroup* ag = g_simple_action_group_new();
    struct { const char* name; } actions[] = {
        {"new-tab"}, {"reload"}, {"go-back"}, {"go-forward"},
        {"save-page"}, {"print-page"}, {"fullscreen"}, {"about"}
    };
    for (auto& a : actions) {
        GSimpleAction* act = g_simple_action_new(a.name, nullptr);
        g_signal_connect(act, "activate", G_CALLBACK(OnMenuActionCb), this);
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
    add_shortcut("<Control>t",   "win.new-tab");
    add_shortcut("<Control>r",   "win.reload");
    add_shortcut("F5",           "win.reload");
    add_shortcut("<Alt>Left",    "win.go-back");
    add_shortcut("<Alt>Right",   "win.go-forward");
    add_shortcut("F11",          "win.fullscreen");
    add_shortcut("<Control>p",   "win.print-page");
    add_shortcut("<Control>s",   "win.save-page");
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

Tab* BrowserWindow::NewTab(const std::string& url) {
    auto* tab = new Tab();
    tab->id   = next_tab_id_++;
    tab->url  = url;

    // WebView
    tab->webview = webkit_web_view_new();
    WebKitSettings* wk_settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(tab->webview));
    // Chrome UA — Google Görseller ve diğer modern siteler için
    webkit_settings_set_user_agent(wk_settings,
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36");
    webkit_settings_set_enable_javascript(wk_settings, TRUE);
    webkit_settings_set_enable_media(wk_settings, TRUE);
    webkit_settings_set_javascript_can_open_windows_automatically(wk_settings, TRUE);
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
    if (url == kHomePage) {
        webkit_web_view_load_html(WEBKIT_WEB_VIEW(tab->webview), kHomeHTML, nullptr);
    } else {
        std::string load_url = url;
        if (load_url.find("://") == std::string::npos)
            load_url = "https://" + load_url;
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(tab->webview), load_url.c_str());
    }
    SwitchToTab(tab);
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
void BrowserWindow::OnMenuActionCb(GSimpleAction* action, GVariant*, gpointer ud) {
    auto* self = static_cast<BrowserWindow*>(ud);
    const char* name = g_action_get_name(G_ACTION(action));
    if      (!g_strcmp0(name, "new-tab"))    self->NewTab(self->kHomePage);
    else if (!g_strcmp0(name, "reload"))     self->OnReload();
    else if (!g_strcmp0(name, "go-back"))    self->OnBack();
    else if (!g_strcmp0(name, "go-forward")) self->OnForward();
    else if (!g_strcmp0(name, "fullscreen")) {
        if (gtk_window_is_fullscreen(GTK_WINDOW(self->window_)))
            gtk_window_unfullscreen(GTK_WINDOW(self->window_));
        else
            gtk_window_fullscreen(GTK_WINDOW(self->window_));
    }
    else if (!g_strcmp0(name, "about")) {
        GtkAlertDialog* dlg = gtk_alert_dialog_new(
            "Ferzan Browser");
        gtk_alert_dialog_set_detail(dlg,
            "Sürüm: 0.2.0\n"
            "Yapı: GTK4 + WebKitGTK 6.0\n"
            "Lisans: MIT");
        gtk_alert_dialog_show(dlg, GTK_WINDOW(self->window_));
        g_object_unref(dlg);
    }
}
void BrowserWindow::OnMouseTargetChangedCb(WebKitWebView* wv,
                                            WebKitHitTestResult* hit,
                                            guint, gpointer ud) {
    static_cast<BrowserWindow*>(ud)->OnMouseTargetChanged(wv, hit);
}

} // namespace ferzan
