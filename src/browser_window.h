#pragma once
#include "tab.h"
#include "session_manager.h"
#include "history_manager.h"
#include "bookmark_manager.h"
#include "download_manager.h"
#include "settings_manager.h"
#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <string>
#include <vector>

namespace ferzan {

class BrowserWindow {
public:
    explicit BrowserWindow(GtkApplication* app);

private:
    Tab*  NewTab(const std::string& url, bool load = true, bool switch_to = true);
    void  CloseTab(Tab* tab);
    void  SwitchToTab(Tab* tab);
    void  OpenInNewWindow(const std::string& url);
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
    bool  OnDecidePolicy(WebKitWebView* wv, WebKitPolicyDecision* dec, WebKitPolicyDecisionType type);
    bool  OnContextMenu(WebKitWebView* wv, WebKitContextMenu* menu, WebKitHitTestResult* hit);
    WebKitWebView* OnCreateWebView(WebKitWebView* wv, WebKitNavigationAction* action);
    void  OnPrint(WebKitWebView* wv, WebKitPrintOperation* op);
    void  ShowSettingsPage();
    void  UpdateFavButton();
    void  ShowUrlSuggestions(const std::string& text);
    void  HideUrlSuggestions();
    void  RebuildBookmarksBar();
    void  ToggleBookmarksBar();
    std::string BuildSettingsHTML();
    std::string BuildHistoryHTML();
    std::string BuildAboutHTML();
    void  HandleFerzanScheme(const std::string& uri);

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
    static void OnMenuActionCb(GSimpleAction*, GVariant*, gpointer);
    static void OnMenuActionWithParamCb(GSimpleAction*, GVariant*, gpointer);
    static gboolean OnDecidePolicyCb(WebKitWebView*, WebKitPolicyDecision*, WebKitPolicyDecisionType, gpointer);
    static gboolean OnContextMenuCb(WebKitWebView*, WebKitContextMenu*, WebKitHitTestResult*, gpointer);
    static WebKitWebView* OnCreateWebViewCb(WebKitWebView*, WebKitNavigationAction*, gpointer);
    static gboolean OnPrintCb(WebKitWebView*, WebKitPrintOperation*, gpointer);

    GtkWidget* window_          = nullptr;
    GtkWidget* tab_box_         = nullptr;
    GtkWidget* new_tab_btn_     = nullptr;
    GtkWidget* stack_           = nullptr;
    GtkWidget* url_entry_       = nullptr;
    GtkWidget* back_btn_        = nullptr;
    GtkWidget* fwd_btn_         = nullptr;
    GtkWidget* reload_btn_      = nullptr;
    GtkWidget* fav_btn_         = nullptr;
    GtkWidget* status_bar_      = nullptr;
    GtkWidget* menu_btn_        = nullptr;
    GtkWidget* download_btn_    = nullptr;
    GtkWidget* suggest_pop_     = nullptr;
    GtkWidget* suggest_list_    = nullptr;
    GtkWidget* bookmarks_bar_   = nullptr;
    GtkWidget* bookmarks_box_   = nullptr;
    GtkWidget* zoom_box_        = nullptr;
    bool       bookmarks_visible_ = false;

    GtkApplication*   app_         = nullptr;
    std::vector<Tab*> tabs_;
    Tab*              active_tab_ = nullptr;
    int               next_tab_id_ = 1;

    static constexpr const char* kHomePage  = "ferzan://home";
    static constexpr const char* kDefaultUrl = "ferzan://home";
    static constexpr const char* kAppName    = "Ferzan Browser";
};

} // namespace ferzan
