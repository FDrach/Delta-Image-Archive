#pragma once

#include <gtk/gtk.h>
#include <zip.h>
#include <json-glib/json-glib.h>
#include <errno.h>

// Shared application state
typedef struct {
    GtkWidget *main_window;
    GtkWidget *image_display;
    GtkWidget *spinner;
    GtkWidget *scrolled_image;
    gchar *zip_path;
    GHashTable *image_map;
    GHashTable *dependencies;
    GHashTable *alpha_map;
    GdkPixbuf *original_pixbuf;
} AppData;

// Core entry points
int on_command_line(GtkApplication *app, GApplicationCommandLine *cmdline, gpointer user_data);
void activate(GtkApplication *app, gpointer user_data);

// Diagnostics
void debug_print_stored_data(AppData *data);

// Rendering
GdkPixbuf* render_composite_image(AppData *data, const gchar *image_id, GError **error);

// IO helpers
gchar* read_file_from_zip(const char *zip_path, const char *inner_filename, gsize *size, GError **error);
GdkPixbuf* load_pixbuf_from_memory(const gchar *buffer, gsize size, GError **error);

// UI helpers
void scale_image_to_fit(AppData *data);
void on_tree_selection_changed(GtkTreeSelection *selection, gpointer user_data);
void on_scrolled_window_size_allocate(GtkWidget *widget, GdkRectangle *allocation, gpointer user_data);
