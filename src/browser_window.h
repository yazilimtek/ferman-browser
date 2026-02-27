#pragma once
#include "tab.h"
#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <string>
#include <vector>

namespace ferzan {

class BrowserWindow {
public:
    explicit BrowserWindow(GtkApplication* app);

private:
    Tab*  NewTab(const std::string& url);
    void  CloseTab(Tab* tab);
    void  SwitchToTab(Tab* tab);
    void  UpdateNavButtons();
    void  UpdateUrlEntry();
    Tab*  TabForWebView(WebKitWebView* wv);
    Tab*  ActiveTab();

    void  OnLoadChanged(WebKitWebView* wv, WebKitLoadEvent event);
    void  OnUriChanged(WebKitWebView* wv);
    void  OnTitleChanged(WebKitWebView* wv);
    void  OnFaviconChanged(WebKitWebView* wv);
    void  OnMouseTargetChanged(WebKitWebView* wv, WebKitHitTestResult* hit);
    void  OnUrlActivate();
    void  OnBack();
    void  OnForward();
    void  OnReload();
    void  UpdateTabLabel(Tab* tab);

    static void OnLoadChangedCb(WebKitWebView*, WebKitLoadEvent, gpointer);
    static void OnUriChangedCb(WebKitWebView*, GParamSpec*, gpointer);
    static void OnTitleChangedCb(WebKitWebView*, GParamSpec*, gpointer);
    static void OnFaviconChangedCb(WebKitWebView*, GParamSpec*, gpointer);
    static void OnMouseTargetChangedCb(WebKitWebView*, WebKitHitTestResult*, guint, gpointer);
    static void OnBackCb(GtkButton*, gpointer);
    static void OnForwardCb(GtkButton*, gpointer);
    static void OnReloadCb(GtkButton*, gpointer);
    static void OnUrlActivateCb(GtkEntry*, gpointer);
    static void OnNewTabCb(GtkButton*, gpointer);
    static void OnClearUrlCb(GtkButton*, gpointer);
    static void OnCopyUrlCb(GtkButton*, gpointer);

    GtkWidget* window_       = nullptr;
    GtkWidget* tab_box_      = nullptr;
    GtkWidget* new_tab_btn_  = nullptr;
    GtkWidget* stack_        = nullptr;
    GtkWidget* url_entry_    = nullptr;
    GtkWidget* back_btn_     = nullptr;
    GtkWidget* fwd_btn_      = nullptr;
    GtkWidget* reload_btn_   = nullptr;
    GtkWidget* fav_btn_      = nullptr;
    GtkWidget* status_bar_   = nullptr;

    std::vector<Tab*> tabs_;
    Tab*              active_tab_ = nullptr;
    int               next_tab_id_ = 1;

    static constexpr const char* kHomePage  = "ferzan://home";
    static constexpr const char* kDefaultUrl = "ferzan://home";
    static constexpr const char* kAppName    = "Ferzan Browser";
};

} // namespace ferzan
