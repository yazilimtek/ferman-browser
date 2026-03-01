#pragma once
#include <webkit/webkit.h>
#include <gtk/gtk.h>
#include <string>
#include <vector>

namespace ferman {

struct DownloadItem {
    WebKitDownload* download  = nullptr;
    GtkWidget*      row       = nullptr;
    GtkWidget*      label     = nullptr;
    GtkWidget*      progress  = nullptr;
    GtkWidget*      status    = nullptr;
    GtkWidget*      cancel_btn= nullptr;
    GtkWidget*      open_btn  = nullptr;
    std::string     filename;
    std::string     dest_path;  // tam dosya yolu
    std::string     url;
    bool            done      = false;
};

class DownloadManager {
public:
    static DownloadManager& Get();

    void Init(GtkWindow* parent_window);

    void OnDownloadStarted(WebKitDownload* download);

    GtkWidget* GetPanelButton() const { return panel_btn_; }
    void ShowPanel();

private:
    DownloadManager() = default;
    void BuildPanel();
    void UpdateEmptyLabel();
    static gboolean OnDecideDestinationCb(WebKitDownload*, const char* suggested, gpointer);
    static void OnProgressCb(WebKitDownload*, GParamSpec*, gpointer);
    static void OnFinishedCb(WebKitDownload*, gpointer);
    static void OnFailedCb(WebKitDownload*, GError*, gpointer);

    GtkWindow*  parent_    = nullptr;
    GtkWidget*  panel_     = nullptr;
    GtkWidget*  panel_btn_ = nullptr;
    GtkWidget*  list_box_  = nullptr;
    GtkWidget*  empty_lbl_ = nullptr;
    GtkWidget*  clear_btn_ = nullptr;
    int         active_count_ = 0;
    std::vector<DownloadItem*> items_;
};

} // namespace ferman
