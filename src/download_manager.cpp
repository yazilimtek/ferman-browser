#include "download_manager.h"
#include "settings_manager.h"
#include <gio/gio.h>
#include <cstring>
#include <string>
#include <cstdio>

namespace ferman {

// ── Yardımcı: bayt → okunabilir boyut ────────────────────────────────────────
static std::string fmt_size(gint64 bytes) {
    char buf[32];
    if (bytes < 0)              return "";
    if (bytes < 1024)           { snprintf(buf,sizeof(buf),"%lldB",(long long)bytes); }
    else if (bytes < 1<<20)     { snprintf(buf,sizeof(buf),"%.1f KB",(double)bytes/1024); }
    else if (bytes < 1<<30)     { snprintf(buf,sizeof(buf),"%.1f MB",(double)bytes/(1<<20)); }
    else                        { snprintf(buf,sizeof(buf),"%.2f GB",(double)bytes/(1<<30)); }
    return buf;
}

DownloadManager& DownloadManager::Get() {
    static DownloadManager inst;
    return inst;
}

void DownloadManager::Init(GtkWindow* parent_window) {
    parent_ = parent_window;
    BuildPanel();
}

void DownloadManager::BuildPanel() {
    // ── Buton ─────────────────────────────────────────────────────────────────
    panel_btn_ = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(panel_btn_), "folder-download-symbolic");
    gtk_widget_set_tooltip_text(panel_btn_, "İndirmeler");
    gtk_widget_add_css_class(panel_btn_, "flat");

    // ── Popover ───────────────────────────────────────────────────────────────
    panel_ = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(panel_), FALSE);
    gtk_widget_set_size_request(panel_, 420, -1);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(panel_btn_), panel_);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(root, 12);
    gtk_widget_set_margin_end(root, 12);
    gtk_widget_set_margin_top(root, 10);
    gtk_widget_set_margin_bottom(root, 10);

    // Başlık + temizle + klasör
    GtkWidget* hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* title_lbl = gtk_label_new("İndirmeler");
    gtk_widget_add_css_class(title_lbl, "title-4");
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
    gtk_widget_set_hexpand(title_lbl, TRUE);
    gtk_box_append(GTK_BOX(hdr), title_lbl);

    // Klasörü aç butonu
    GtkWidget* open_dir_btn = gtk_button_new_from_icon_name("folder-open-symbolic");
    gtk_widget_add_css_class(open_dir_btn, "flat");
    gtk_widget_set_tooltip_text(open_dir_btn, "İndirme klasörünü aç");
    g_object_set_data(G_OBJECT(open_dir_btn), "dm", this);
    g_signal_connect(open_dir_btn, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer ud) {
            auto* self = static_cast<DownloadManager*>(ud);
            const std::string& cfg = SettingsManager::Get().Prefs().download_dir;
            std::string dir;
            if (!cfg.empty()) {
                dir = (cfg[0]=='~') ? std::string(g_get_home_dir()) + cfg.substr(1) : cfg;
            } else {
                const char* d = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
                dir = d ? std::string(d) : std::string(g_get_home_dir());
            }
            GFile* f = g_file_new_for_path(dir.c_str());
            g_app_info_launch_default_for_uri(g_file_get_uri(f), nullptr, nullptr);
            g_object_unref(f);
            gtk_popover_popdown(GTK_POPOVER(self->panel_));
        }), this);
    gtk_box_append(GTK_BOX(hdr), open_dir_btn);

    // Tamamlananları temizle
    clear_btn_ = gtk_button_new_from_icon_name("edit-clear-all-symbolic");
    gtk_widget_add_css_class(clear_btn_, "flat");
    gtk_widget_set_tooltip_text(clear_btn_, "Tamamlananları temizle");
    gtk_widget_set_sensitive(clear_btn_, FALSE);
    g_object_set_data(G_OBJECT(clear_btn_), "dm", this);
    g_signal_connect(clear_btn_, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer ud) {
            auto* self = static_cast<DownloadManager*>(ud);
            std::vector<DownloadItem*> remaining;
            for (auto* it : self->items_) {
                if (it->done) {
                    gtk_list_box_remove(GTK_LIST_BOX(self->list_box_), it->row);
                    delete it;
                } else {
                    remaining.push_back(it);
                }
            }
            self->items_ = remaining;
            self->UpdateEmptyLabel();
            gtk_widget_set_sensitive(self->clear_btn_, FALSE);
        }), this);
    gtk_box_append(GTK_BOX(hdr), clear_btn_);
    gtk_box_append(GTK_BOX(root), hdr);
    gtk_box_append(GTK_BOX(root), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // Kaydırılabilir liste
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 320);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);

    list_box_ = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box_), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(list_box_, "boxed-list");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list_box_);
    gtk_box_append(GTK_BOX(root), scroll);

    // Boş etiket
    empty_lbl_ = gtk_label_new("Henüz indirme yok");
    gtk_widget_add_css_class(empty_lbl_, "dim-label");
    gtk_widget_set_halign(empty_lbl_, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(empty_lbl_, 8);
    gtk_box_append(GTK_BOX(root), empty_lbl_);

    gtk_popover_set_child(GTK_POPOVER(panel_), root);
}

void DownloadManager::UpdateEmptyLabel() {
    bool has = !items_.empty();
    gtk_widget_set_visible(empty_lbl_, !has);
    gtk_widget_set_visible(list_box_,   has);
}

void DownloadManager::OnDownloadStarted(WebKitDownload* download) {
    auto* item     = new DownloadItem();
    item->download = download;
    item->url      = webkit_uri_request_get_uri(webkit_download_get_request(download));
    ++active_count_;

    // ── Satır ─────────────────────────────────────────────────────────────────
    GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(row_box, 8);
    gtk_widget_set_margin_end(row_box, 8);
    gtk_widget_set_margin_top(row_box, 7);
    gtk_widget_set_margin_bottom(row_box, 7);

    // Üst satır: ikon + dosya adı + durum + iptal/aç butonu
    GtkWidget* top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    GtkWidget* icon = gtk_image_new_from_icon_name("text-x-generic-symbolic");
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(top), icon);

    item->label = gtk_label_new("Bekleniyor…");
    gtk_label_set_ellipsize(GTK_LABEL(item->label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(item->label, TRUE);
    gtk_widget_set_halign(item->label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(top), item->label);

    item->status = gtk_label_new("0%");
    gtk_widget_add_css_class(item->status, "dim-label");
    gtk_widget_set_halign(item->status, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(top), item->status);

    // İptal butonu
    item->cancel_btn = gtk_button_new_from_icon_name("process-stop-symbolic");
    gtk_widget_add_css_class(item->cancel_btn, "flat");
    gtk_widget_add_css_class(item->cancel_btn, "circular");
    gtk_widget_set_tooltip_text(item->cancel_btn, "İndirmeyi iptal et");
    gtk_widget_set_valign(item->cancel_btn, GTK_ALIGN_CENTER);
    g_object_set_data(G_OBJECT(item->cancel_btn), "item", item);
    g_object_set_data(G_OBJECT(item->cancel_btn), "dm",   this);
    g_signal_connect(item->cancel_btn, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer ud) {
            auto* it   = static_cast<DownloadItem*>(g_object_get_data(G_OBJECT(
                             g_object_get_data(G_OBJECT(
                                 gtk_widget_get_parent(
                                     gtk_widget_get_parent(
                                         gtk_widget_get_parent(GTK_WIDGET(ud))))),
                                 "cancel_btn")),
                             "item"));
            // Basitleştirilmiş: doğrudan DownloadItem'a ulaş
            (void)ud;
        }), nullptr);
    // Düzeltilmiş iptal bağlantısı — item pointer'ı doğrudan kullan
    g_object_set_data(G_OBJECT(item->cancel_btn), "item-ptr", item);
    g_object_set_data(G_OBJECT(item->cancel_btn), "dm-ptr",   this);
    g_signal_handlers_disconnect_by_func(item->cancel_btn,
        (gpointer)+[](GtkButton*, gpointer){}, nullptr);
    g_signal_connect(item->cancel_btn, "clicked",
        G_CALLBACK(+[](GtkButton* btn, gpointer) {
            auto* it   = static_cast<DownloadItem*>(g_object_get_data(G_OBJECT(btn), "item-ptr"));
            auto* self = static_cast<DownloadManager*>(g_object_get_data(G_OBJECT(btn), "dm-ptr"));
            if (!it || !self) return;
            webkit_download_cancel(it->download);
            it->done = true;
            gtk_label_set_text(GTK_LABEL(it->status), "✗ İptal edildi");
            gtk_widget_remove_css_class(it->status, "dim-label");
            gtk_widget_add_css_class(it->status, "error");
            gtk_widget_set_sensitive(GTK_WIDGET(btn), FALSE);
            if (it->open_btn) gtk_widget_set_visible(it->open_btn, FALSE);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(it->progress), 0.0);
            --self->active_count_;
            if (self->active_count_ == 0)
                gtk_widget_remove_css_class(self->panel_btn_, "suggested-action");
            gtk_widget_set_sensitive(self->clear_btn_, TRUE);
        }), nullptr);
    gtk_box_append(GTK_BOX(top), item->cancel_btn);

    // Dosyayı aç butonu (başta gizli)
    item->open_btn = gtk_button_new_from_icon_name("document-open-symbolic");
    gtk_widget_add_css_class(item->open_btn, "flat");
    gtk_widget_add_css_class(item->open_btn, "circular");
    gtk_widget_set_tooltip_text(item->open_btn, "Dosyayı aç");
    gtk_widget_set_valign(item->open_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(item->open_btn, FALSE);
    g_object_set_data(G_OBJECT(item->open_btn), "item-ptr", item);
    g_signal_connect(item->open_btn, "clicked",
        G_CALLBACK(+[](GtkButton* btn, gpointer) {
            auto* it = static_cast<DownloadItem*>(g_object_get_data(G_OBJECT(btn), "item-ptr"));
            if (!it || it->dest_path.empty()) return;
            std::string uri = "file://" + it->dest_path;
            g_app_info_launch_default_for_uri(uri.c_str(), nullptr, nullptr);
        }), nullptr);
    gtk_box_append(GTK_BOX(top), item->open_btn);

    // Progress bar
    item->progress = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(item->progress), 0.0);

    gtk_box_append(GTK_BOX(row_box), top);
    gtk_box_append(GTK_BOX(row_box), item->progress);

    item->row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(item->row), row_box);
    gtk_list_box_prepend(GTK_LIST_BOX(list_box_), item->row);

    items_.push_back(item);
    UpdateEmptyLabel();

    g_signal_connect(download, "decide-destination",        G_CALLBACK(OnDecideDestinationCb), item);
    g_signal_connect(download, "notify::estimated-progress", G_CALLBACK(OnProgressCb),         item);
    auto* fin_ctx  = new std::pair<DownloadItem*, DownloadManager*>(item, this);
    auto* fail_ctx = new std::pair<DownloadItem*, DownloadManager*>(item, this);
    g_signal_connect(download, "finished", G_CALLBACK(OnFinishedCb), fin_ctx);
    g_signal_connect(download, "failed",   G_CALLBACK(OnFailedCb),   fail_ctx);

    // Paneli otomatik aç & vurgula
    gtk_menu_button_popup(GTK_MENU_BUTTON(panel_btn_));
    gtk_widget_add_css_class(panel_btn_, "suggested-action");
}

gboolean DownloadManager::OnDecideDestinationCb(WebKitDownload* dl,
                                                 const char* suggested,
                                                 gpointer ud) {
    auto* item = static_cast<DownloadItem*>(ud);

    const std::string& cfg_dir = SettingsManager::Get().Prefs().download_dir;
    std::string dir;
    if (!cfg_dir.empty()) {
        dir = (cfg_dir[0]=='~')
            ? std::string(g_get_home_dir()) + cfg_dir.substr(1)
            : cfg_dir;
    } else {
        const char* d = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
        if (!d) d = g_get_home_dir();
        dir = d;
    }
    g_mkdir_with_parents(dir.c_str(), 0755);

    std::string filename = suggested ? suggested : "download";
    auto slash = filename.rfind('/');
    if (slash != std::string::npos) filename = filename.substr(slash + 1);
    if (filename.empty()) filename = "download";

    item->filename  = filename;
    item->dest_path = dir + "/" + filename;

    gtk_label_set_text(GTK_LABEL(item->label), filename.c_str());

    webkit_download_set_destination(dl, item->dest_path.c_str());
    webkit_download_set_allow_overwrite(dl, TRUE);
    return TRUE;  // handled — indirme devam etsin
}

void DownloadManager::OnProgressCb(WebKitDownload* dl, GParamSpec*, gpointer ud) {
    auto* item = static_cast<DownloadItem*>(ud);
    double p = webkit_download_get_estimated_progress(dl);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(item->progress), p);

    gint64 total    = webkit_download_get_estimated_progress(dl) > 0
                    ? (gint64)(webkit_download_get_received_data_length(dl) / p)
                    : -1;
    gint64 received = (gint64)webkit_download_get_received_data_length(dl);

    std::string sz;
    if (total > 0)
        sz = fmt_size(received) + " / " + fmt_size(total) + "  " +
             std::to_string((int)(p*100)) + "%";
    else
        sz = fmt_size(received) + "  " + std::to_string((int)(p*100)) + "%";

    gtk_label_set_text(GTK_LABEL(item->status), sz.c_str());
}

void DownloadManager::OnFinishedCb(WebKitDownload*, gpointer ud) {
    auto* ctx  = static_cast<std::pair<DownloadItem*, DownloadManager*>*>(ud);
    auto* item = ctx->first;
    auto* self = ctx->second;
    delete ctx;

    item->done = true;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(item->progress), 1.0);

    gint64 sz = (gint64)webkit_download_get_received_data_length(item->download);
    std::string msg = "✓ " + fmt_size(sz);
    gtk_label_set_text(GTK_LABEL(item->status), msg.c_str());
    gtk_widget_remove_css_class(item->status, "dim-label");
    gtk_widget_add_css_class(item->status, "success");

    if (item->cancel_btn) gtk_widget_set_sensitive(item->cancel_btn, FALSE);
    if (item->open_btn)   gtk_widget_set_visible(item->open_btn, TRUE);
    if (self->clear_btn_) gtk_widget_set_sensitive(self->clear_btn_, TRUE);

    --self->active_count_;
    if (self->active_count_ == 0)
        gtk_widget_remove_css_class(self->panel_btn_, "suggested-action");

    // Masaüstü bildirimi
    GNotification* notif = g_notification_new("İndirme Tamamlandı");
    g_notification_set_body(notif, (item->filename + " indirildi.").c_str());
    GApplication* gapp = g_application_get_default();
    if (gapp) g_application_send_notification(gapp, "download-done", notif);
    g_object_unref(notif);
}

void DownloadManager::OnFailedCb(WebKitDownload*, GError* err, gpointer ud) {
    auto* ctx  = static_cast<std::pair<DownloadItem*, DownloadManager*>*>(ud);
    auto* item = ctx->first;
    auto* self = ctx->second;
    delete ctx;

    if (item->done) return;  // iptal edildiyse tekrar işleme
    item->done = true;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(item->progress), 0.0);
    std::string msg = std::string("✗ ") + (err ? err->message : "Hata");
    gtk_label_set_text(GTK_LABEL(item->status), msg.c_str());
    gtk_widget_remove_css_class(item->status, "dim-label");
    gtk_widget_add_css_class(item->status, "error");
    if (item->cancel_btn) gtk_widget_set_sensitive(item->cancel_btn, FALSE);
    if (self->clear_btn_) gtk_widget_set_sensitive(self->clear_btn_, TRUE);
    --self->active_count_;
    if (self->active_count_ == 0)
        gtk_widget_remove_css_class(self->panel_btn_, "suggested-action");
}

void DownloadManager::ShowPanel() {
    gtk_menu_button_popup(GTK_MENU_BUTTON(panel_btn_));
}

} // namespace ferman
