#include "download_manager.h"
#include <gio/gio.h>
#include <cstring>
#include <string>

namespace ferzan {

DownloadManager& DownloadManager::Get() {
    static DownloadManager inst;
    return inst;
}

void DownloadManager::Init(GtkWindow* parent_window) {
    parent_ = parent_window;
    BuildPanel();
}

void DownloadManager::BuildPanel() {
    // Panel butonu (header bar'a eklenecek)
    panel_btn_ = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(panel_btn_), "folder-download-symbolic");
    gtk_widget_set_tooltip_text(panel_btn_, "İndirmeler");
    gtk_widget_add_css_class(panel_btn_, "flat");

    // Popover panel
    panel_ = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(panel_), FALSE);
    gtk_widget_set_size_request(panel_, 380, -1);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(panel_btn_), panel_);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);

    GtkWidget* title_lbl = gtk_label_new("İndirmeler");
    gtk_widget_add_css_class(title_lbl, "title-4");
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), title_lbl);

    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), sep);

    list_box_ = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box_), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(list_box_, "boxed-list");
    gtk_box_append(GTK_BOX(box), list_box_);

    GtkWidget* empty_lbl = gtk_label_new("Henüz indirme yok");
    gtk_widget_set_name(empty_lbl, "dl-empty");
    gtk_widget_add_css_class(empty_lbl, "dim-label");
    gtk_widget_set_halign(empty_lbl, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), empty_lbl);

    gtk_popover_set_child(GTK_POPOVER(panel_), box);
}

void DownloadManager::OnDownloadStarted(WebKitDownload* download) {
    auto* item    = new DownloadItem();
    item->download = download;
    item->url     = webkit_uri_request_get_uri(webkit_download_get_request(download));

    // Liste satırı
    GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(row_box, 8);
    gtk_widget_set_margin_end(row_box, 8);
    gtk_widget_set_margin_top(row_box, 6);
    gtk_widget_set_margin_bottom(row_box, 6);

    GtkWidget* top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    item->label = gtk_label_new("İndiriliyor...");
    gtk_label_set_ellipsize(GTK_LABEL(item->label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(item->label, TRUE);
    gtk_widget_set_halign(item->label, GTK_ALIGN_START);

    item->status = gtk_label_new("0%");
    gtk_widget_add_css_class(item->status, "dim-label");
    gtk_widget_set_halign(item->status, GTK_ALIGN_END);

    gtk_box_append(GTK_BOX(top_row), item->label);
    gtk_box_append(GTK_BOX(top_row), item->status);

    item->progress = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(item->progress), 0.0);

    gtk_box_append(GTK_BOX(row_box), top_row);
    gtk_box_append(GTK_BOX(row_box), item->progress);

    item->row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(item->row), row_box);
    gtk_list_box_append(GTK_LIST_BOX(list_box_), item->row);

    // "Henüz indirme yok" etiketini gizle
    GtkWidget* empty_lbl = gtk_widget_get_last_child(
        gtk_popover_get_child(GTK_POPOVER(panel_)));
    if (empty_lbl) gtk_widget_set_visible(empty_lbl, FALSE);

    items_.push_back(item);

    g_signal_connect(download, "decide-destination",
        G_CALLBACK(OnDecideDestinationCb), item);
    g_signal_connect(download, "notify::estimated-progress",
        G_CALLBACK(OnProgressCb), item);
    g_signal_connect(download, "finished",
        G_CALLBACK(OnFinishedCb), item);
    g_signal_connect(download, "failed",
        G_CALLBACK(OnFailedCb), item);

    // Paneli otomatik aç
    gtk_menu_button_popup(GTK_MENU_BUTTON(panel_btn_));

    // Header bar ikonunu vurgula
    gtk_widget_add_css_class(panel_btn_, "suggested-action");
}

void DownloadManager::OnDecideDestinationCb(WebKitDownload* dl,
                                              const char* suggested,
                                              gpointer ud) {
    auto* item = static_cast<DownloadItem*>(ud);

    // ~/Downloads/ferzan/ klasörüne kaydet
    const char* downloads_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (!downloads_dir) downloads_dir = g_get_home_dir();

    std::string dir = std::string(downloads_dir) + "/ferzan";
    g_mkdir_with_parents(dir.c_str(), 0755);

    std::string filename = suggested ? suggested : "download";
    // Sadece dosya adını al (path içeriyorsa)
    auto slash = filename.rfind('/');
    if (slash != std::string::npos) filename = filename.substr(slash + 1);
    if (filename.empty()) filename = "download";

    item->filename = filename;
    gtk_label_set_text(GTK_LABEL(item->label), filename.c_str());

    std::string dest = "file://" + dir + "/" + filename;
    webkit_download_set_destination(dl, dest.c_str());
    webkit_download_set_allow_overwrite(dl, TRUE);
}

void DownloadManager::OnProgressCb(WebKitDownload* dl, GParamSpec*, gpointer ud) {
    auto* item = static_cast<DownloadItem*>(ud);
    double p = webkit_download_get_estimated_progress(dl);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(item->progress), p);

    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f%%", p * 100.0);
    gtk_label_set_text(GTK_LABEL(item->status), buf);
}

void DownloadManager::OnFinishedCb(WebKitDownload*, gpointer ud) {
    auto* item = static_cast<DownloadItem*>(ud);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(item->progress), 1.0);
    gtk_label_set_text(GTK_LABEL(item->status), "✓ Tamamlandı");
    gtk_widget_add_css_class(item->status, "success");

    // Masaüstü bildirimi
    GNotification* notif = g_notification_new("İndirme Tamamlandı");
    std::string body = item->filename + " indirildi.";
    g_notification_set_body(notif, body.c_str());
    g_notification_set_priority(notif, G_NOTIFICATION_PRIORITY_NORMAL);

    GApplication* gapp = g_application_get_default();
    if (gapp) {
        g_application_send_notification(gapp, "download-done", notif);
    }
    g_object_unref(notif);
}

void DownloadManager::OnFailedCb(WebKitDownload*, GError* err, gpointer ud) {
    auto* item = static_cast<DownloadItem*>(ud);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(item->progress), 0.0);
    std::string msg = std::string("✗ Hata: ") + (err ? err->message : "bilinmiyor");
    gtk_label_set_text(GTK_LABEL(item->status), msg.c_str());
    gtk_widget_add_css_class(item->status, "error");
}

void DownloadManager::ShowPanel() {
    gtk_menu_button_popup(GTK_MENU_BUTTON(panel_btn_));
}

} // namespace ferzan
