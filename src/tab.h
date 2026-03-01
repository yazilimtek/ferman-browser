#pragma once
#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <string>

namespace ferman {

struct Tab {
    int         id        = 0;
    GtkWidget*  webview   = nullptr;
    GtkWidget*  tab_btn   = nullptr;
    GtkWidget*  label     = nullptr;
    GtkWidget*  favicon   = nullptr;
    std::string url;
    std::string title;
};

} // namespace ferman
