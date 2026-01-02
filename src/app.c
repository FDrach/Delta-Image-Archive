#include "viewer.h"

// Simple natural comparator: compares sequences of digits numerically, otherwise lexicographically (ASCII)
static int natural_cmp(const char *a, const char *b) {
    const unsigned char *s1 = (const unsigned char*)a;
    const unsigned char *s2 = (const unsigned char*)b;
    while (*s1 && *s2) {
        if (g_ascii_isdigit(*s1) && g_ascii_isdigit(*s2)) {
            // skip leading zeros
            while (*s1 == '0') s1++;
            while (*s2 == '0') s2++;
            const unsigned char *p1 = s1;
            const unsigned char *p2 = s2;
            while (g_ascii_isdigit(*p1)) p1++;
            while (g_ascii_isdigit(*p2)) p2++;
            int len1 = p1 - s1;
            int len2 = p2 - s2;
            if (len1 != len2) return len1 - len2;
            int cmp = g_ascii_strncasecmp((const char*)s1, (const char*)s2, len1);
            if (cmp != 0) return cmp;
            s1 = p1;
            s2 = p2;
            continue;
        }
        if (*s1 != *s2) return (int)*s1 - (int)*s2;
        s1++; s2++;
    }
    return (int)*s1 - (int)*s2;
}

static GtkTreeIter* append_path(GtkTreeStore *store, GHashTable *node_map, const char *path, const char *id_str) {
    // node_map keyed by full path segment, value GtkTreeIter*
    gchar **parts = g_strsplit(path, "/", -1);
    if (!parts) return NULL;
    gchar *accum = g_strdup("");
    GtkTreeIter *parent_iter = NULL;
    for (int i = 0; parts[i] != NULL; i++) {
        gboolean is_leaf = parts[i+1] == NULL;
        gchar *prev = accum;
        accum = g_strconcat(accum[0] ? accum : "", accum[0] ? "/" : "", parts[i], NULL);
        g_free(prev);
        GtkTreeIter *existing = g_hash_table_lookup(node_map, accum);
        if (!existing) {
            GtkTreeIter *iter = g_new0(GtkTreeIter, 1);
            gtk_tree_store_append(store, iter, parent_iter);
            gtk_tree_store_set(store, iter, 0, is_leaf ? id_str : NULL, 1, parts[i], -1);
            g_hash_table_insert(node_map, g_strdup(accum), iter);
            existing = iter;
        }
        parent_iter = existing;
    }
    g_strfreev(parts);
    g_free(accum);
    return parent_iter;
}

static gboolean find_iter_for_id(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer user_data) {
    const gchar *target_id = (const gchar*)user_data;
    gchar *id = NULL;
    gtk_tree_model_get(model, iter, 0, &id, -1);
    gboolean stop = FALSE;
    if (id && target_id && g_strcmp0(id, target_id) == 0) {
        GtkTreePath **out_path = (GtkTreePath**)g_object_get_data(G_OBJECT(model), "find_path_ptr");
        if (out_path) {
            *out_path = gtk_tree_path_copy(path);
        }
        stop = TRUE;
    }
    g_free(id);
    return stop;
}

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
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_list), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkTreeStore *store = gtk_tree_store_new(2, G_TYPE_STRING, G_TYPE_STRING); // id, label
    GHashTable *node_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    GList *id_list = g_hash_table_get_keys(data->image_map);
    // Build auxiliary list of (path,id) to sort naturally by path
    GList *path_list = NULL;
    for (GList *l = id_list; l; l = l->next) {
        const gchar *image_id = l->data;
        const gchar *filename = g_hash_table_lookup(data->image_map, image_id);
        if (filename) {
            path_list = g_list_prepend(path_list, g_strdup_printf("%s\t%s", filename, image_id));
        }
    }
    path_list = g_list_sort(path_list, (GCompareFunc)natural_cmp);

    gchar *first_id = NULL;
    for (GList *l = path_list; l; l = l->next) {
        gchar **parts = g_strsplit(l->data, "\t", 2);
        if (parts && parts[0] && parts[1]) {
            append_path(store, node_map, parts[0], parts[1]);
            if (!first_id) first_id = g_strdup(parts[1]);
        }
        g_strfreev(parts);
    }

    g_hash_table_destroy(node_map);
    g_list_free_full(path_list, g_free);
    g_list_free(id_list);

    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Images", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), FALSE);

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);
    g_signal_connect(selection, "changed", G_CALLBACK(on_tree_selection_changed), data);

    gtk_tree_view_expand_all(GTK_TREE_VIEW(tree));
    if (first_id) {
        GtkTreePath *found_path = NULL;
        g_object_set_data(G_OBJECT(gtk_tree_view_get_model(GTK_TREE_VIEW(tree))), "find_path_ptr", &found_path);
        gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)), find_iter_for_id, (gpointer)first_id);
        if (found_path) {
            gtk_tree_selection_select_path(selection, found_path);
            gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(tree), found_path, NULL, FALSE, 0, 0);
            gtk_tree_path_free(found_path);
        }
        g_object_set_data(G_OBJECT(gtk_tree_view_get_model(GTK_TREE_VIEW(tree))), "find_path_ptr", NULL);
    }

    g_free(first_id);

    gtk_container_add(GTK_CONTAINER(scrolled_list), tree);
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

    gtk_widget_show_all(data->main_window);
    gtk_widget_hide(data->spinner);
}
