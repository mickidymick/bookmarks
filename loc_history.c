#include <yed/plugin.h>
#include <yed/tree.h>
typedef char *yedrc_path_t;
typedef struct loc_data{
    int row;
    int col;
    int can_update;
}loc_data_t;
use_tree_c(yedrc_path_t, loc_data_t, strcmp);

void init_loc_history(yed_event *event);
void set_start_loc_from_history(yed_event *event);
void update_loc_history(yed_event *event);
void write_back_loc_history(yed_event *event);
void unload(yed_plugin *self);

int loc_history_initialized;
tree(yedrc_path_t, loc_data_t) hist;
yed_event_handler h1;

int yed_plugin_boot(yed_plugin *self) {
    yed_event_handler h;

    YED_PLUG_VERSION_CHECK();
    loc_history_initialized = 0;

    yed_plugin_set_unload_fn(self, unload);
    hist = tree_make(yedrc_path_t, loc_data_t);

    h1.kind = EVENT_BUFFER_PRE_LOAD;
    h1.fn   = init_loc_history;
    yed_plugin_add_event_handler(self, h1);

    h.kind = EVENT_FRAME_POST_SET_BUFFER;
    h.fn   = set_start_loc_from_history;
    yed_plugin_add_event_handler(self, h);

    h.kind = EVENT_CURSOR_POST_MOVE;
    h.fn   = update_loc_history;
    yed_plugin_add_event_handler(self, h);

    h.kind = EVENT_PRE_QUIT;
    h.fn   = write_back_loc_history;
    yed_plugin_add_event_handler(self, h);

    return 0;
}

void init_loc_history(yed_event *event) {
    char line[512];
    char str[512];
    char app[512];
    char *path;
    FILE *fp;

    if (loc_history_initialized == 0) {
        loc_history_initialized = 1;
    } else {
      return;
    }

    strcpy(app, "/my_loc_history.txt");

    LOG_FN_ENTER();
    if (!ys->options.no_init) {
        path = get_config_item_path("my_loc_history.txt");
        fp = fopen (path, "r");
/*         yed_cerr("path: %s\n", path); */
	    free(path);
    }

    if (fp == NULL) {
        return;
    }

    const char s[2] = " ";
    char *tmp_path;
    char *tmp_row;
    char *tmp_col;
    while( fgets( line, 512, fp ) != NULL ) {
        tmp_path = strtok(line, s);
        tmp_row = strtok(NULL, s);
        tmp_col = strtok(NULL, s);
	
	if (tmp_path == NULL || tmp_row == NULL || tmp_col == NULL){
	    continue;
	}

        loc_data_t tmp;
        tmp.row = atoi(tmp_row);
        tmp.col = atoi(tmp_col);
        tmp.can_update = 0;
        tree_insert(hist, strdup(tmp_path), tmp);
    }
    fclose(fp);
    yed_delete_event_handler(h1);

    yed_log("loc_history initialized: %d files loaded", tree_len(hist));
    LOG_EXIT();
}

void set_start_loc_from_history(yed_event *event) {
    char file_name[512];
    loc_data_t tmp;

    yed_frame *frame;
    frame = event->frame;

    LOG_FN_ENTER();

    if (!frame
    ||  !frame->buffer
    ||  frame->buffer->path == NULL
    ||  frame->buffer->kind != BUFF_KIND_FILE) {
        return;
    }

    tree_it(yedrc_path_t, loc_data_t) it;
    abs_path(frame->buffer->path, file_name);

    it = tree_lookup(hist, file_name);
    if( tree_it_good(it) ) {
        tmp = tree_it_val(it);
        if(tmp.can_update == 0) {
            yed_set_cursor_within_frame(frame, tmp.row, tmp.col);
            tree_it_val(it).can_update = 1;
            yed_log("%s: %d:%d", frame->buffer->path, tmp.row, tmp.col);
        }
    }
    LOG_EXIT();
}

void update_loc_history(yed_event *event) {
    char file_name[512];
    loc_data_t tmp;
    yed_frame *frame;
    frame = event->frame;


    if (!frame
    ||  !frame->buffer
    ||  frame->buffer->kind != BUFF_KIND_FILE) {
        return;
    }

    tree_it(yedrc_path_t, loc_data_t) it;
    abs_path(frame->buffer->path, file_name);

    it = tree_lookup(hist, file_name);
    if( tree_it_good(it) ) {
        if(!tree_it_val(it).can_update) {
            return;
        }
        tree_it_val(it).row = frame->cursor_line;
        tree_it_val(it).col = frame->cursor_col;
    }else{
        tmp.row = frame->cursor_line;
        tmp.col = frame->cursor_col;
        tmp.can_update = 1;
        tree_insert(hist, strdup(file_name), tmp);
    }
}

void write_back_loc_history(yed_event *event) {
    char str[512];
    char app[512];
    char *path;
    FILE *fp;
    tree_it(yedrc_path_t, loc_data_t) it;

    strcpy(app, "/my_loc_history.txt");

    if (!ys->options.no_init) {
        path = get_config_item_path("my_loc_history.txt");
        fp = fopen (path, "r");
	    free(path);
    }

    /* grab latest adds */
    if (fp == NULL) {
        return;
    }

    const char s[2] = " ";
    char *tmp_path;
    char *tmp_row;
    char *tmp_col;
    char line[512];

    while( fgets( line, 512, fp ) != NULL ) {
        tmp_path = strtok(line, s);
        tmp_row = strtok(NULL, s);
        tmp_col = strtok(NULL, s);
	
	if (tmp_path == NULL || tmp_row == NULL || tmp_col == NULL){
	    continue;
	}

        loc_data_t tmp;
        tmp.row = atoi(tmp_row);
        tmp.col = atoi(tmp_col);
        tmp.can_update = 0;

        it = tree_lookup(hist, tmp_path);
        if( tree_it_good(it) ) {
            if(tree_it_val(it).can_update == 0) {
                tree_insert(hist, strdup(tmp_path), tmp);
            }
        }else {
            tree_insert(hist, strdup(tmp_path), tmp);
        }
    }
    fclose(fp);

    if (!ys->options.no_init) {
        path = get_config_item_path("my_loc_history.txt");
        fp = fopen (path, "w+");
    	free(path);
    }

    if (fp == NULL) {
        return;
    }

    tree_traverse(hist, it) {
        fprintf(fp, "%s %d %d\n", tree_it_key(it), tree_it_val(it).row, tree_it_val(it).col);
    }
    fclose(fp);
}

void unload(yed_plugin *self) {
    tree_it(yedrc_path_t, loc_data_t)  it;
    char                              *tmp;

    while (tree_len(hist) > 0) {
        it = tree_begin(hist);
        tmp = tree_it_key(it);
        tree_delete(hist, tmp);
        free(tmp);
    }

    tree_free(hist);
}
