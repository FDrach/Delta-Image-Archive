#include "viewer.h"

gchar* read_file_from_zip(const char *zip_path, const char *inner_filename, gsize *size, GError **error) {
    if (!inner_filename) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Inner filename cannot be NULL");
        return NULL;
    }
    
    int err = 0;
    zip_t *archive = zip_open(zip_path, 0, &err);
    if (!archive) {
        zip_error_t zip_err;
        zip_error_init_with_code(&zip_err, err);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to open zip archive '%s': %s", 
                   zip_path, zip_error_strerror(&zip_err));
        zip_error_fini(&zip_err);
        return NULL;
    }
    
    struct zip_stat stat;
    zip_stat_init(&stat);
    if (zip_stat(archive, inner_filename, 0, &stat) < 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "File not found in zip archive: %s", inner_filename);
        zip_close(archive);
        return NULL;
    }
    
    zip_file_t* file_in_zip = zip_fopen(archive, inner_filename, 0);
    if (!file_in_zip) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to open file '%s' inside zip archive: %s", 
                   inner_filename, zip_strerror(archive));
        zip_close(archive);
        return NULL;
    }
    
    guint8 *buffer = g_malloc(stat.size);
    zip_int64_t bytes_read = zip_fread(file_in_zip, buffer, stat.size);
    zip_fclose(file_in_zip);
    zip_close(archive);
    
    if (bytes_read < 0 || (guint64)bytes_read != stat.size) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, 
                   "Failed to read all bytes from '%s' in zip archive.", inner_filename);
        g_free(buffer);
        return NULL;
    }
    
    *size = (gsize)bytes_read;
    return (gchar*)buffer;
}

GdkPixbuf* load_pixbuf_from_memory(const gchar *buffer, gsize size, GError **error) {
    g_autoptr(GdkPixbufLoader) loader = gdk_pixbuf_loader_new();
    
    if (!gdk_pixbuf_loader_write(loader, (const guint8*)buffer, size, error)) {
        return NULL;
    }
    
    if (!gdk_pixbuf_loader_close(loader, error)) {
        return NULL;
    }
    
    GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
    if (pixbuf) {
        g_object_ref(pixbuf);
    }
    return pixbuf;
}
