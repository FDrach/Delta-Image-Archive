#include "viewer.h"

static GdkPixbuf* load_alpha_for_id(AppData *data, const gchar *image_id, GError **error);
static gboolean apply_alpha_map_to_pixbuf(GdkPixbuf *pixbuf, GdkPixbuf *alpha_map_pixbuf, gboolean combine_with_existing, GError **error);

GdkPixbuf* render_composite_image(AppData *data, const gchar *image_id, GError **error) {
    GQueue *chain = g_queue_new();
    GHashTable *visited = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gchar *current_id = g_strdup(image_id);
    
    // Build dependency chain
    while (current_id) {
        // Check for cycles
        if (g_hash_table_contains(visited, current_id)) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Circular dependency detected involving ID '%s'", current_id);
            g_free(current_id);
            g_queue_free_full(chain, g_free);
            g_hash_table_destroy(visited);
            return NULL;
        }
        
        g_hash_table_insert(visited, g_strdup(current_id), GINT_TO_POINTER(1));
        g_queue_push_head(chain, current_id);
        
        if (g_hash_table_contains(data->dependencies, current_id)) {
            const gchar *dependency = g_hash_table_lookup(data->dependencies, current_id);
            current_id = g_strdup(dependency);
        } else {
            current_id = NULL;
        }
    }
    
    g_hash_table_destroy(visited);
    
    // Start with the base image
    gchar *base_id = g_queue_pop_head(chain);
    const gchar *base_filename = g_hash_table_lookup(data->image_map, base_id);
    if (!base_filename) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Could not find filename for ID '%s'", base_id);
        g_free(base_id);
        g_queue_free_full(chain, g_free);
        return NULL;
    }
    
    gsize base_size = 0;
    g_autofree gchar *base_buffer = read_file_from_zip(data->zip_path, base_filename, &base_size, error);
    if (!base_buffer) {
        g_free(base_id);
        g_queue_free_full(chain, g_free);
        return NULL;
    }
    
    GdkPixbuf *canvas_pixbuf = load_pixbuf_from_memory(base_buffer, base_size, error);
    if (!canvas_pixbuf) {
        g_free(base_id);
        g_queue_free_full(chain, g_free);
        return NULL;
    }
    
    if (!gdk_pixbuf_get_has_alpha(canvas_pixbuf)) {
        GdkPixbuf *temp = gdk_pixbuf_add_alpha(canvas_pixbuf, FALSE, 0, 0, 0);
        g_object_unref(canvas_pixbuf);
        canvas_pixbuf = temp;
    }

    GdkPixbuf *base_alpha_pixbuf = load_alpha_for_id(data, base_id, error);
    if (base_alpha_pixbuf) {
        if (!apply_alpha_map_to_pixbuf(canvas_pixbuf, base_alpha_pixbuf, FALSE, error)) {
            g_object_unref(base_alpha_pixbuf);
            g_free(base_id);
            g_queue_free_full(chain, g_free);
            g_object_unref(canvas_pixbuf);
            return NULL;
        }
        g_object_unref(base_alpha_pixbuf);
    }
    
    g_free(base_id);
    
    // Apply overlays in order
    while (!g_queue_is_empty(chain)) {
        gchar *overlay_id = g_queue_pop_head(chain);
        const gchar *overlay_filename = g_hash_table_lookup(data->image_map, overlay_id);
        
        if (!overlay_filename) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Could not find filename for overlay ID '%s'", overlay_id);
            g_free(overlay_id);
            g_object_unref(canvas_pixbuf);
            g_queue_free_full(chain, g_free);
            return NULL;
        }
        
        gsize overlay_size = 0;
        g_autofree gchar *overlay_buffer = read_file_from_zip(data->zip_path, overlay_filename, &overlay_size, error);
        if (!overlay_buffer) {
            g_free(overlay_id);
            g_object_unref(canvas_pixbuf);
            g_queue_free_full(chain, g_free);
            return NULL;
        }
        
        g_autoptr(GdkPixbuf) overlay_pixbuf_orig = load_pixbuf_from_memory(overlay_buffer, overlay_size, error);
        if (!overlay_pixbuf_orig) {
            g_free(overlay_id);
            g_object_unref(canvas_pixbuf);
            g_queue_free_full(chain, g_free);
            return NULL;
        }
        
        GdkPixbuf *overlay_to_composite = overlay_pixbuf_orig;
        g_autoptr(GdkPixbuf) temp_alpha_pixbuf = NULL;
        
        if (!gdk_pixbuf_get_has_alpha(overlay_to_composite)) {
            temp_alpha_pixbuf = gdk_pixbuf_add_alpha(overlay_to_composite, FALSE, 0, 0, 0);
            overlay_to_composite = temp_alpha_pixbuf;
        }

        g_autoptr(GdkPixbuf) overlay_alpha_pixbuf = load_alpha_for_id(data, overlay_id, error);
        
        int canvas_width = gdk_pixbuf_get_width(canvas_pixbuf);
        int canvas_height = gdk_pixbuf_get_height(canvas_pixbuf);
        int overlay_width = gdk_pixbuf_get_width(overlay_to_composite);
        int overlay_height = gdk_pixbuf_get_height(overlay_to_composite);
        
        // Composite overlay
        int composite_width = MIN(overlay_width, canvas_width);
        int composite_height = MIN(overlay_height, canvas_height);
        
        if (composite_width > 0 && composite_height > 0) {
            gdk_pixbuf_composite(overlay_to_composite, canvas_pixbuf,
                               0, 0, composite_width, composite_height,
                               0, 0, 1.0, 1.0, GDK_INTERP_NEAREST, 255);
        }

        if (overlay_alpha_pixbuf) {
            if (!apply_alpha_map_to_pixbuf(canvas_pixbuf, overlay_alpha_pixbuf, TRUE, error)) {
                g_free(overlay_id);
                g_object_unref(canvas_pixbuf);
                g_queue_free_full(chain, g_free);
                return NULL;
            }
        }
        
        g_free(overlay_id);
    }
    
    g_queue_free(chain);
    return canvas_pixbuf;
}

static GdkPixbuf* load_alpha_for_id(AppData *data, const gchar *image_id, GError **error) {
    if (!data->alpha_map) return NULL;

    const gchar *alpha_path = g_hash_table_lookup(data->alpha_map, image_id);
    if (!alpha_path) return NULL;

    gsize alpha_size = 0;
    g_autofree gchar *alpha_buffer = read_file_from_zip(data->zip_path, alpha_path, &alpha_size, error);
    if (!alpha_buffer) return NULL;

    GdkPixbuf *alpha_pixbuf = load_pixbuf_from_memory(alpha_buffer, alpha_size, error);
    return alpha_pixbuf;
}

static gboolean apply_alpha_map_to_pixbuf(GdkPixbuf *pixbuf, GdkPixbuf *alpha_map_pixbuf, gboolean combine_with_existing, GError **error) {
    if (!pixbuf || !alpha_map_pixbuf) return FALSE;

    int w = gdk_pixbuf_get_width(pixbuf);
    int h = gdk_pixbuf_get_height(pixbuf);
    int aw = gdk_pixbuf_get_width(alpha_map_pixbuf);
    int ah = gdk_pixbuf_get_height(alpha_map_pixbuf);

    if (w != aw || h != ah) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Alpha map size mismatch (%dx%d vs %dx%d)", aw, ah, w, h);
        return FALSE;
    }

    if (!gdk_pixbuf_get_has_alpha(pixbuf)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Target pixbuf missing alpha channel");
        return FALSE;
    }

    int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    int alpha_rowstride = gdk_pixbuf_get_rowstride(alpha_map_pixbuf);
    int n_channels = gdk_pixbuf_get_n_channels(pixbuf);
    int alpha_channels = gdk_pixbuf_get_n_channels(alpha_map_pixbuf);

    guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
    guchar *alpha_pixels = gdk_pixbuf_get_pixels(alpha_map_pixbuf);

    for (int y = 0; y < h; y++) {
        guchar *row = pixels + y * rowstride;
        guchar *alpha_row = alpha_pixels + y * alpha_rowstride;
        for (int x = 0; x < w; x++) {
            guchar *p = row + x * n_channels;
            guchar a_existing = p[3];
            guchar a_map = alpha_row[x * alpha_channels];
            if (combine_with_existing) {
                // Combine delta mask alpha with original alpha map
                p[3] = (guchar)((a_existing * a_map) / 255);
            } else {
                p[3] = a_map;
            }
        }
    }

    return TRUE;
}
