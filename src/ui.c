#include "viewer.h"

void on_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    AppData *data = (AppData*)user_data;
    if (row == NULL) return;
    
    gtk_widget_show(data->spinner);
    gtk_spinner_start(GTK_SPINNER(data->spinner));
    
    const char *image_id = g_object_get_data(G_OBJECT(row), "image-id");
    g_print("\n--- Row Selected: ID '%s' ---\n", image_id);
    
    GError *error = NULL;
    GdkPixbuf *pixbuf = render_composite_image(data, image_id, &error);
    
    if (pixbuf) {
        g_print("SUCCESS: Final image rendered. Displaying.\n");
        
        if (data->original_pixbuf) {
            g_object_unref(data->original_pixbuf);
        }
        data->original_pixbuf = g_object_ref(pixbuf);
        
        scale_image_to_fit(data);
        g_object_unref(pixbuf);
    } else {
        g_printerr("ERROR: Could not render '%s': %s\n", image_id, error ? error->message : "Unknown error");
        gtk_image_set_from_icon_name(GTK_IMAGE(data->image_display), "image-missing", GTK_ICON_SIZE_DIALOG);
        if (error) g_error_free(error);
    }
    
    gtk_spinner_stop(GTK_SPINNER(data->spinner));
    gtk_widget_hide(data->spinner);
}

void on_scrolled_window_size_allocate(GtkWidget *widget, GdkRectangle *allocation, gpointer user_data) {
    AppData *data = (AppData*)user_data;
    (void)widget; (void)allocation;
    
    if (data->original_pixbuf) {
        scale_image_to_fit(data);
    }
}

void scale_image_to_fit(AppData *data) {
    if (!data->original_pixbuf || !data->scrolled_image) return;
    
    GtkAllocation allocation;
    gtk_widget_get_allocation(data->scrolled_image, &allocation);
    
    int orig_width = gdk_pixbuf_get_width(data->original_pixbuf);
    int orig_height = gdk_pixbuf_get_height(data->original_pixbuf);
    
    int available_width = allocation.width - 5;
    int available_height = allocation.height - 5;
    
    if (available_width <= 0 || available_height <= 0) return;
    
    double scale_x = (double)available_width / orig_width;
    double scale_y = (double)available_height / orig_height;
    double scale = MIN(scale_x, scale_y);
    
    // Only scale down
    scale = MIN(scale, 1.0);
    
    int new_width = (int)(orig_width * scale);
    int new_height = (int)(orig_height * scale);
    
    if (new_width > 0 && new_height > 0) {
        GdkPixbuf *scaled_pixbuf = gdk_pixbuf_scale_simple(data->original_pixbuf, 
                                                          new_width, new_height, 
                                                          GDK_INTERP_BILINEAR);
        if (scaled_pixbuf) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(data->image_display), scaled_pixbuf);
            g_object_unref(scaled_pixbuf);
        }
    }
}
