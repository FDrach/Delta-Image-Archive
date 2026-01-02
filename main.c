#include "src/viewer.h"

int main(int argc, char **argv) {
    AppData *data = g_new0(AppData, 1);
    GtkApplication *app = gtk_application_new("com.example.compositebrowser", G_APPLICATION_HANDLES_COMMAND_LINE | G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "command-line", G_CALLBACK(on_command_line), data);
    g_signal_connect(app, "activate", G_CALLBACK(activate), data);
    
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    
    g_object_unref(app);
    g_free(data->zip_path);
    if (data->image_map) g_hash_table_destroy(data->image_map);
    if (data->dependencies) g_hash_table_destroy(data->dependencies);
    if (data->alpha_map) g_hash_table_destroy(data->alpha_map);
    if (data->original_pixbuf) g_object_unref(data->original_pixbuf);
    g_free(data);
    return status;
}

