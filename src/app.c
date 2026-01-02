#include "viewer.h"
#include <ctype.h>

static void copy_to_hashtable_cb(JsonObject *object, const gchar *member_name, JsonNode *member_node, gpointer user_data) {
    GHashTable *hash_table = (GHashTable*)user_data;
    
    if (!JSON_NODE_HOLDS_VALUE(member_node) || json_node_get_value_type(member_node) != G_TYPE_STRING) {
        g_warning("Skipping non-string value for key '%s'", member_name);
        return;
    }
    
    const gchar *value_str = json_node_get_string(member_node);
    if (value_str) {
        g_hash_table_insert(hash_table, g_strdup(member_name), g_strdup(value_str));
    }
}

void debug_print_stored_data(AppData *data) {
    g_print("\n\n--- VERIFYING STORED DATA FROM HASH TABLES ---\n");
    g_print("--- Image Map ---\n");
    
    GHashTableIter iter;
    gpointer key, value;
    
    g_hash_table_iter_init(&iter, data->image_map);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        g_print("  ID '%s' -> Filename '%s'\n", (gchar*)key, (gchar*)value);
    }
    
    g_print("--- Dependencies ---\n");
    g_hash_table_iter_init(&iter, data->dependencies);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        g_print("  ID '%s' -> Depends on '%s'\n", (gchar*)key, (gchar*)value);
    }

    g_print("--- Alpha Map ---\n");
    g_hash_table_iter_init(&iter, data->alpha_map);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        g_print("  ID '%s' -> Alpha file '%s'\n", (gchar*)key, (gchar*)value);
    }
    g_print("--- END OF VERIFICATION ---\n\n");
}

static gint natural_compare_strings(const gchar *a, const gchar *b) {
    const gchar *p = a;
    const gchar *q = b;
    while (*p && *q) {
        if (g_ascii_isdigit(*p) && g_ascii_isdigit(*q)) {
            const gchar *p_start = p;
            const gchar *q_start = q;

            while (*p_start == '0') p_start++;
            while (*q_start == '0') q_start++;

            const gchar *p_end = p_start;
            const gchar *q_end = q_start;
            while (g_ascii_isdigit(*p_end)) p_end++;
            while (g_ascii_isdigit(*q_end)) q_end++;

            gint len_p = p_end - p_start;
            gint len_q = q_end - q_start;
            if (len_p != len_q)
                return (len_p < len_q) ? -1 : 1;

            gint cmp = g_strndup(p_start, len_p) ? memcmp(p_start, q_start, len_p) : 0;
            if (cmp != 0)
                return (cmp < 0) ? -1 : 1;

            /* numbers equal, advance pointers past the digit sequences */
            p = p_end;
            q = q_end;
            continue;
        }

        gint ca = g_ascii_tolower(*p);
        gint cb = g_ascii_tolower(*q);
        if (ca != cb)
            return (ca < cb) ? -1 : 1;
        p++;
        q++;
    }
    if (!*p && !*q) return 0;
    return (*p) ? 1 : -1;
}

static gint natural_compare(gconstpointer a, gconstpointer b, gpointer user_data) {
    const gchar *id_a = (const gchar*)a;
    const gchar *id_b = (const gchar*)b;
    GHashTable *map = (GHashTable*)user_data;
    const gchar *fa = g_hash_table_lookup(map, id_a);
    const gchar *fb = g_hash_table_lookup(map, id_b);
    if (!fa) fa = id_a;
    if (!fb) fb = id_b;
    return natural_compare_strings(fa, fb);
}

int on_command_line(GtkApplication *app, GApplicationCommandLine *cmdline, gpointer user_data) {
    AppData *data = (AppData*)user_data;
    gchar **argv;
    gint argc;
    
    argv = g_application_command_line_get_arguments(cmdline, &argc);
    g_print("[dia] command-line argc=%d\n", argc);
    if (argc < 2) {
        g_printerr("Usage: composite_browser <path/to/archive.dia>\n");
        g_strfreev(argv);
        return 1;
    }
    
    data->zip_path = g_strdup(argv[1]);
    g_print("[dia] zip path: %s\n", data->zip_path);
    g_application_activate(G_APPLICATION(app));
    g_strfreev(argv);
    return 0;
}

void activate(GtkApplication *app, gpointer user_data) {
    AppData *data = (AppData*)user_data;
    g_autoptr(GError) error = NULL;
    gsize map_size = 0;

    g_print("[dia] activate begin\n");

    // Parse the JSON
    g_autofree gchar *map_contents = read_file_from_zip(data->zip_path, "optimization_map.json", &map_size, &error);
    if (!map_contents) {
        g_printerr("ERROR: Could not read optimization_map.json: %s\n", error ? error->message : "Unknown error");
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                                  "Could not read optimization_map.json: %s", error ? error->message : "Unknown error");
        g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
        gtk_widget_show(dialog);
        return;
    }

    g_autoptr(JsonParser) parser = json_parser_new();
    if (!json_parser_load_from_data(parser, map_contents, map_size, &error)) {
        g_printerr("ERROR: Could not parse JSON: %s\n", error ? error->message : "Unknown error");
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                                  "Could not parse JSON: %s", error ? error->message : "Unknown error");
        g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
        gtk_widget_show(dialog);
        return;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_printerr("ERROR: JSON root is not an object\n");
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                                  "optimization_map.json root is not an object");
        g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
        gtk_widget_show(dialog);
        return;
    }
    
    JsonObject *root_obj = json_node_get_object(root);

    data->image_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    data->dependencies = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    data->alpha_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    g_print("EXTRACTING data from temporary JSON object into permanent hash tables...\n");
    
    if (json_object_has_member(root_obj, "image_map")) {
        JsonObject *temp_image_map = json_object_get_object_member(root_obj, "image_map");
        if (temp_image_map) {
            json_object_foreach_member(temp_image_map, copy_to_hashtable_cb, data->image_map);
        }
    }

    if (json_object_has_member(root_obj, "dependencies")) {
        JsonObject *temp_deps = json_object_get_object_member(root_obj, "dependencies");
        if (temp_deps) {
            json_object_foreach_member(temp_deps, copy_to_hashtable_cb, data->dependencies);
        }
    }

    if (json_object_has_member(root_obj, "alpha_map")) {
        JsonObject *temp_alpha = json_object_get_object_member(root_obj, "alpha_map");
        if (temp_alpha) {
            json_object_foreach_member(temp_alpha, copy_to_hashtable_cb, data->alpha_map);
        }
    }
    
    g_print("EXTRACTION complete.\n");

    debug_print_stored_data(data);
    g_print("[dia] activate finished init\n");

    // Build the UI
    data->main_window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(data->main_window), g_path_get_basename(data->zip_path));
    gtk_window_set_default_size(GTK_WINDOW(data->main_window), 800, 600);
    
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_add(GTK_CONTAINER(data->main_window), paned);
    
    GtkWidget *scrolled_list = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *list_box = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(scrolled_list), list_box);
    gtk_paned_add1(GTK_PANED(paned), scrolled_list);
    
    GtkWidget *scrolled_image = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_image), 
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    GtkWidget *overlay = gtk_overlay_new();
    data->image_display = gtk_image_new();
    
    g_signal_connect(scrolled_image, "size-allocate", G_CALLBACK(on_scrolled_window_size_allocate), data);
    
    gtk_container_add(GTK_CONTAINER(overlay), data->image_display);
    
    data->spinner = gtk_spinner_new();
    gtk_widget_set_halign(data->spinner, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(data->spinner, GTK_ALIGN_CENTER);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), data->spinner);
    gtk_container_add(GTK_CONTAINER(scrolled_image), overlay);
    data->scrolled_image = scrolled_image;
    gtk_paned_add2(GTK_PANED(paned), scrolled_image);
    gtk_paned_set_position(GTK_PANED(paned), 200);

    GList *id_list = g_hash_table_get_keys(data->image_map);
    id_list = g_list_sort_with_data(id_list, (GCompareDataFunc)natural_compare, data->image_map);
    
    for (GList *l = id_list; l != NULL; l = l->next) {
        const gchar *image_id = (const gchar*)l->data;
        const gchar *filename = g_hash_table_lookup(data->image_map, image_id);
        
        GtkWidget *row = gtk_list_box_row_new();
        gtk_container_add(GTK_CONTAINER(row), gtk_label_new(filename ? filename : image_id));
        g_object_set_data_full(G_OBJECT(row), "image-id", g_strdup(image_id), g_free);
        gtk_list_box_insert(GTK_LIST_BOX(list_box), row, -1);
    }
    g_list_free(id_list);

    g_signal_connect(list_box, "row-selected", G_CALLBACK(on_row_selected), data);
    gtk_widget_show_all(data->main_window);
    gtk_widget_hide(data->spinner);
}
