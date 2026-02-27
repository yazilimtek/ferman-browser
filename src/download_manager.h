#pragma once
#include <webkit/webkit.h>
#include <gtk/gtk.h>
#include <string>
#include <vector>

namespace ferzan {

struct DownloadItem {
    WebKitDownload* download = nullptr;
    GtkWidget*      row      = nullptr;
    GtkWidget*      label    = nullptr;
    GtkWidget*      progress = nullptr;
    GtkWidget*      status   = nullptr;
    std::string     filename;
    std::string     url;
};

class DownloadManager {
public:
    static DownloadManager& Get();

    void Init(GtkWindow* parent_window);

    // NetworkSession "download-started" sinyalinden çağrılır
    void OnDownloadStarted(WebKitDownload* download);

    // Header bar ikonu / panel toggle
    GtkWidget* GetPanelButton() const { return panel_btn_; }
    void ShowPanel();

private:
    DownloadManager() = default;
    void BuildPanel();
    void UpdateItem(DownloadItem* item);
    static void OnDecideDestinationCb(WebKitDownload*, const char* suggested, gpointer);
    static void OnProgressCb(WebKitDownload*, GParamSpec*, gpointer);
    static void OnFinishedCb(WebKitDownload*, gpointer);
    static void OnFailedCb(WebKitDownload*, GError*, gpointer);

    GtkWindow*  parent_   = nullptr;
    GtkWidget*  panel_    = nullptr;  // GtkPopover
    GtkWidget*  panel_btn_= nullptr;  // GtkMenuButton (header bar)
    GtkWidget*  list_box_ = nullptr;  // GtkListBox inside panel
    std::vector<DownloadItem*> items_;
};

} // namespace ferzan
