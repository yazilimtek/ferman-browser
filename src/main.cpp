#include "browser_window.h"
#include <gtk/gtk.h>

static void on_activate(GtkApplication* app, gpointer) {
    // Uygulama ikonunu ayarla
    gtk_window_set_default_icon_name("ferman-browser");
    new ferman::BrowserWindow(app);
}

int main(int argc, char** argv) {
    GtkApplication* app = gtk_application_new("com.ferman.browser",
                                               G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    int ret = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return ret;
}
