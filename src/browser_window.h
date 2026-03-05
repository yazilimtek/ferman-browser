#pragma once
#include "tab.h"
#include "session_manager.h"
#include "history_manager.h"
#include "bookmark_manager.h"
#include "download_manager.h"
#include "settings_manager.h"
#include "ai_manager.h"
#include "file_extractor.h"
#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <string>
#include <vector>

namespace ferman {

class BrowserWindow {
public:
    explicit BrowserWindow(GtkApplication* app);
    void  RebuildBookmarksBar();

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
    void  ToggleBookmarksBar();
    std::string BuildHomeHTML();
    std::string BuildSettingsHTML(const std::string& page = "genel");
    std::string BuildPrivacyHTML();
    std::string BuildTabsSettingsHTML();
    std::string BuildAppearanceSettingsHTML();
    std::string BuildAdvancedSettingsHTML();
    std::string BuildHistoryHTML();
    std::string BuildAboutHTML();
    std::string BuildSetupHTML(const std::string& error = "");
    void  HandlefermanScheme(const std::string& uri);
    void  ShowAiSetupWarning();
    void  SaveTabSession();
    void  RestoreTabSession();
    void  ShowFindBar();
    void  HideFindBar();
    void  FindNext();
    void  FindPrev();

    // ── AI Panel ──
    void  BuildAiPanel();
    void  ToggleAiPanel();
    void  ToggleAiHistory();
    void  ToggleAiFilter();
    void  RefreshAiChatList();
    void  LoadAiChat(int64_t id);
    void  SendAiMessage();
    void  DoSendAiMessage(const std::string& input, const std::string& provider,
                          const std::string& model, const std::string& api_key,
                          const std::string& api_url);
    void  AppendAiBubble(const std::string& role, const std::string& text);
    void  ShowAiLoading(bool show);
    void  ShowAiQuickPopup(const std::string& title, const std::string& prompt);
    void  AddAttachmentChip(const std::string& path);
    std::string CollectAiInputText();
    void  ParseAndHighlightTokens();
    void  UpdateAiAgentCombo();
    void  ShowAgentAutocomplete(const std::string& prefix);
    void  HideAgentAutocomplete();
    std::string BuildSettingsAiHTML();
    std::string BuildSettingsBookmarksHTML();

    static void OnAiSendCb(GtkButton*, gpointer);
    static void OnAiNewChatCb(GtkButton*, gpointer);
    static void OnAiToggleHistoryCb(GtkButton*, gpointer);
    static void OnAiToggleFilterCb(GtkButton*, gpointer);
    static void OnAiAttachCb(GtkButton*, gpointer);
    static void OnAiSearchChangedCb(GtkSearchEntry*, gpointer);
    static void OnAiDateFilterCb(GtkDropDown*, GParamSpec*, gpointer);
    static void OnAiInputChangedCb(GtkTextBuffer*, gpointer);
    static gboolean OnAiKeyPressCb(GtkEventControllerKey*, guint, guint, GdkModifierType, gpointer);

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
    GtkWidget* zoom_reset_btn_  = nullptr;
    bool       bookmarks_visible_ = false;
    std::string saved_url_;   // clear butonundan önce kaydedilen URL (ESC ile geri gelir)
    bool is_dragging_ = false; // sekme sürükle-bırak sırasında close_btn'i engelle
    std::vector<size_t> ai_agent_combo_order_; // combo sırası → agents indeksi eşlemesi
    std::vector<std::string> closed_tabs_;     // Ctrl+Shift+T ile geri açmak için kapatılan URL'ler

    // ── Sayfa İçi Arama (Сtrl+F) ──
    GtkWidget* find_bar_        = nullptr;  // GtkRevealer
    GtkWidget* find_entry_      = nullptr;  // GtkSearchEntry
    GtkWidget* find_count_lbl_  = nullptr;  // “3/12” etiketi
    bool       find_visible_    = false;

    // ── AI Panel widget'ları ──
    GtkWidget* ai_btn_              = nullptr;  // URL bar'daki AI butonu
    GtkWidget* ai_outer_            = nullptr;  // dış kapsayıcı (GtkBox yatay)
    GtkWidget* ai_history_revealer_ = nullptr;  // sol sidebar revealer
    GtkWidget* ai_filter_revealer_  = nullptr;  // filtre barı revealer
    GtkWidget* ai_chat_list_        = nullptr;  // GtkListBox — sohbet listesi
    GtkWidget* ai_search_entry_     = nullptr;  // arama kutusu
    GtkWidget* ai_date_filter_      = nullptr;  // tarih dropdown
    GtkWidget* ai_chat_box_         = nullptr;  // mesaj bubble kutusu
    GtkWidget* ai_chat_scroll_      = nullptr;  // mesaj scroll
    GtkWidget* ai_input_            = nullptr;  // GtkTextView
    GtkWidget* ai_input_buffer_     = nullptr;  // GtkTextBuffer
    GtkWidget* ai_title_label_      = nullptr;  // sohbet başlığı
    GtkWidget* ai_attach_box_       = nullptr;  // dosya chip'leri
    GtkWidget* ai_loading_label_    = nullptr;  // "..." göstergesi
    GtkWidget* ai_history_btn_      = nullptr;  // ☰ toggle
    GtkWidget* ai_agent_combo_      = nullptr;  // ajan seçim dropdown
    GtkWidget* ai_autocomplete_pop_ = nullptr;  // @ autocomplete popover
    GtkWidget* ai_autocomplete_list_= nullptr;  // autocomplete liste
    bool       ai_panel_visible_    = false;
    bool       ai_history_visible_  = false;
    bool       ai_filter_visible_   = false;
    int64_t    ai_current_chat_id_  = 0;
    AiChat     ai_current_chat_;
    std::vector<std::string> ai_attachments_;  // dosya yolları

    GtkApplication*   app_         = nullptr;
    std::vector<Tab*> tabs_;
    Tab*              active_tab_ = nullptr;
    int               next_tab_id_ = 1;

    static constexpr const char* kHomePage  = "ferman://home";
    static constexpr const char* kDefaultUrl = "ferman://home";
    static constexpr const char* kAppName    = "Ferman Browser";
};

} // namespace ferman
