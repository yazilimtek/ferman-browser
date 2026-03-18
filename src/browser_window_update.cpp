// ShowUpdateDialog implementation - browser_window.cpp'ye eklenecek

void BrowserWindow::ShowUpdateDialog(const UpdateInfo& info) {
    if (!info.is_newer) {
        // Güncelleme yok - bilgi dialog'u göster
        GtkAlertDialog* dlg = gtk_alert_dialog_new("Güncel Sürüm");
        gtk_alert_dialog_set_detail(dlg, 
            "Ferman Browser'ın en güncel sürümünü kullanıyorsunuz.\n\n"
            "Mevcut sürüm: " + UpdateManager::Get().GetCurrentVersion());
        
        const char* btns[] = { "Tamam", nullptr };
        gtk_alert_dialog_set_buttons(dlg, btns);
        
        gtk_alert_dialog_choose(dlg, GTK_WINDOW(window_), nullptr, nullptr, nullptr);
        g_object_unref(dlg);
        return;
    }
    
    // Yeni sürüm mevcut - güncelleme dialog'u
    std::string title = "Yeni Sürüm Mevcut: " + info.version;
    GtkAlertDialog* dlg = gtk_alert_dialog_new(title.c_str());
    
    std::string detail = "Ferman Browser'ın yeni bir sürümü yayınlandı!\n\n";
    detail += "Mevcut sürüm: " + UpdateManager::Get().GetCurrentVersion() + "\n";
    detail += "Yeni sürüm: " + info.version + "\n\n";
    
    if (!info.release_notes.empty()) {
        detail += "Sürüm Notları:\n" + info.release_notes.substr(0, 200);
        if (info.release_notes.length() > 200) detail += "...";
        detail += "\n\n";
    }
    
    detail += "Güncellemeyi indirmek ister misiniz?";
    gtk_alert_dialog_set_detail(dlg, detail.c_str());
    
    const char* btns[] = { "İptal", "DEB İndir", "TAR.GZ İndir", "GitHub'da Aç", nullptr };
    gtk_alert_dialog_set_buttons(dlg, btns);
    gtk_alert_dialog_set_cancel_button(dlg, 0);
    gtk_alert_dialog_set_default_button(dlg, 1);
    
    struct UpdateContext {
        BrowserWindow* self;
        std::string deb_url;
        std::string tar_url;
    };
    
    auto* ctx = new UpdateContext{this, info.download_url_deb, info.download_url_tar};
    
    gtk_alert_dialog_choose(dlg, GTK_WINDOW(window_), nullptr,
        [](GObject* source, GAsyncResult* res, gpointer data) {
            auto* ctx = static_cast<UpdateContext*>(data);
            GtkAlertDialog* dlg = GTK_ALERT_DIALOG(source);
            
            GError* error = nullptr;
            int response = gtk_alert_dialog_choose_finish(dlg, res, &error);
            
            if (error) {
                g_error_free(error);
            } else if (response == 1 && !ctx->deb_url.empty()) {
                // DEB indir
                ctx->self->NewTab(ctx->deb_url);
            } else if (response == 2 && !ctx->tar_url.empty()) {
                // TAR.GZ indir
                ctx->self->NewTab(ctx->tar_url);
            } else if (response == 3) {
                // GitHub'da aç
                ctx->self->NewTab("https://github.com/yazilimtek/ferman-browser/releases");
            }
            
            delete ctx;
        }, ctx);
    
    g_object_unref(dlg);
}
