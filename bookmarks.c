#include <yed/plugin.h>

#define DO_LOG
#define DBG__XSTR(x) #x
#define DBG_XSTR(x) DBG__XSTR(x)
#ifdef DO_LOG
#define DBG(...)                                           \
do {                                                       \
    LOG_FN_ENTER();                                        \
    yed_log(__FILE__ ":" XSTR(__LINE__) ": " __VA_ARGS__); \
    LOG_EXIT();                                            \
} while (0)
#else
#define DBG(...) ;
#endif

typedef struct bookmark_data {
    array_t rows;
}bookmark_data_t;

typedef char                        *yedrc_path_t;
use_tree_c(yedrc_path_t, bookmark_data_t, strcmp);
tree(yedrc_path_t, bookmark_data_t)  bookmarks;
yed_event_handler                    h;
int                                  bookmarks_initialized;
yed_plugin                          *Self;

static void _unload(yed_plugin *self);
static void _init_bookmarks(yed_event *event);
static void _add_bookmarks_to_buffer(yed_event *event);
static void _write_back_bookmarks(void);
static void _bookmarks_line_handler(yed_event *event);

void set_new_bookmark(int nargs, char **args);
void remove_bookmark(int nargs, char **args);

int yed_plugin_boot(yed_plugin *self) {
    yed_event_handler h1;
    yed_event_handler h2;

    YED_PLUG_VERSION_CHECK();

    bookmarks_initialized = 0;
    Self                  = self;

    yed_plugin_set_unload_fn(self, _unload);

    bookmarks = tree_make(yedrc_path_t, bookmark_data_t);

    h.kind = EVENT_BUFFER_PRE_LOAD;
    h.fn   = _init_bookmarks;
    yed_plugin_add_event_handler(self, h);

    h1.kind = EVENT_FRAME_POST_SET_BUFFER;
    h1.fn   = _add_bookmarks_to_buffer;
    yed_plugin_add_event_handler(self, h1);

    h2.kind = EVENT_LINE_PRE_DRAW;
    h2.fn   = _bookmarks_line_handler;
    yed_plugin_add_event_handler(self, h2);

    yed_plugin_set_command(self, "set-bookmark", set_new_bookmark);
    yed_plugin_set_command(self, "remove-bookmark", remove_bookmark);

    return 0;
}

void _bookmarks_line_handler(yed_event *event) {
    char             file_name[512];
    bookmark_data_t  tmp;
    int              n_lines;
    int              n_cols;
    char             num_buff[16];
    yed_attrs        attr;
    yed_attrs       *dst;
    yed_frame       *frame;
    int             *r_it;
    int              found;

    if (event->frame->buffer == NULL
    ||  (event->frame->buffer->name
    &&  event->frame->buffer->name[0] == '*')) {

        yed_frame_set_gutter_width(event->frame, 0);
        return;
    }

    n_lines = yed_buff_n_lines(event->frame->buffer);
/*     n_cols  = n_digits(n_lines) + 2; */
    n_cols  = 3;

    if (event->frame->gutter_width != n_cols) {
        yed_frame_set_gutter_width(event->frame, n_cols);
    }

    frame = event->frame;

    if (!frame
    ||  !frame->buffer
    ||  frame->buffer->path == NULL
    ||  frame->buffer->kind != BUFF_KIND_FILE) {
        return;
    }

    tree_it(yedrc_path_t, bookmark_data_t) it;
    abs_path(frame->buffer->path, file_name);

    it = tree_lookup(bookmarks, file_name);
    found = 0;
    if ( tree_it_good(it) ) {
        tmp = tree_it_val(it);
        array_traverse(tmp.rows, r_it) {
            if (*r_it == event->row) {
                snprintf(num_buff, sizeof(num_buff),
                        " %*s ", n_cols - 2, "▓");
                found = 1;
                break;
            }
        }
    }

    if (!found) {
        snprintf(num_buff, sizeof(num_buff),
                " %*s ", n_cols - 2, "");
    }

    array_clear(event->gutter_glyphs);
    array_push_n(event->gutter_glyphs, num_buff, strlen(num_buff));

/*     attr = yed_get_active_style_scomp(scomp_save); */

/*     array_traverse(event->gutter_attrs, dst) { */
/*         yed_combine_attrs(dst, &attr); */
/*     } */
}

static void _unload(yed_plugin *self) {

}

void remove_bookmark(int nargs, char **args) {
    char             file_name[512];
    bookmark_data_t  tmp;
    yed_frame       *frame;
    int              idx;
    int              j;
    int              found;
    int             *r_it;

    frame = ys->active_frame;

    if (!frame
    ||  !frame->buffer
    ||  frame->buffer->path == NULL
    ||  frame->buffer->kind != BUFF_KIND_FILE) {
        return;
    }

    tree_it(yedrc_path_t, bookmark_data_t) it;
    abs_path(frame->buffer->path, file_name);

    it = tree_lookup(bookmarks, file_name);
    found = 0;
    if ( tree_it_good(it) ) {
        tmp = tree_it_val(it);
        idx = 0;
        array_traverse(tmp.rows, r_it) {
            if (*r_it == ys->active_frame->cursor_line) {
                array_delete(tree_it_val(it).rows, idx);
                found = 1;
                LOG_FN_ENTER();
                yed_log("Bookmark removed at line %d\n", ys->active_frame->cursor_line);
                LOG_EXIT();
                break;
            }
            idx++;
        }

        if (!found) {
            yed_cerr("No bookmark exists for this line!\n");
        }
    } else {
        yed_cerr("No bookmark exists for this line!\n");
    }

/*     tree_traverse(bookmarks, it) { */
/*         array_traverse(tree_it_val(it).rows, r_it) { */
/*             DBG("%s %d\n", tree_it_key(it), *r_it); */
/*         } */
/*     } */

    _write_back_bookmarks();
}

void set_new_bookmark(int nargs, char **args) {
    char             file_name[512];
    bookmark_data_t  tmp;
    yed_frame       *frame;
    int              i;
    int             *r_it;

    frame = ys->active_frame;

    if (!frame
    ||  !frame->buffer
    ||  frame->buffer->path == NULL
    ||  frame->buffer->kind != BUFF_KIND_FILE) {
        return;
    }

    tree_it(yedrc_path_t, bookmark_data_t) it;
    abs_path(frame->buffer->path, file_name);

    it = tree_lookup(bookmarks, file_name);
    if ( tree_it_good(it) ) {
        tmp = tree_it_val(it);
        array_traverse(tmp.rows, r_it) {
            if (*r_it == ys->active_frame->cursor_line) {
                yed_cerr("A bookmark for this line already exists!\n");
                goto skip;
            }
        }
        array_push(tree_it_val(it).rows, ys->active_frame->cursor_line);
    } else {
        tmp.rows = array_make(int);
        array_push(tmp.rows, ys->active_frame->cursor_line);
        tree_insert(bookmarks, strdup(frame->buffer->path), tmp);
    }

    LOG_FN_ENTER();
    yed_log("Bookmark set at line %d\n", ys->active_frame->cursor_line);
    LOG_EXIT();

skip:;

/*     tree_traverse(bookmarks, it) { */
/*         array_traverse(tree_it_val(it).rows, r_it) { */
/*             DBG("%s %d\n", tree_it_key(it), *r_it); */
/*         } */
/*     } */

    _write_back_bookmarks();
}

static void _add_bookmarks_to_buffer(yed_event *event) {
    char             file_name[512];
    bookmark_data_t  tmp;
    yed_frame       *frame;
    int             *r_it;

    frame = event->frame;

    if (!frame
    ||  !frame->buffer
    ||  frame->buffer->path == NULL
    ||  frame->buffer->kind != BUFF_KIND_FILE) {
        return;
    }

    tree_it(yedrc_path_t, bookmark_data_t) it;
    abs_path(frame->buffer->path, file_name);

    it = tree_lookup(bookmarks, file_name);
    if ( tree_it_good(it) ) {
        tmp = tree_it_val(it);
        array_traverse(tree_it_val(it).rows, r_it) {
        yed_frame_set_gutter_width(frame, frame->gutter_width+10);
            DBG("%s %d\n", frame->buffer->path, *r_it);
        }
    }

}

static void _init_bookmarks(yed_event *event) {
    char       line[512];
    char       str[512];
    const char s[2] = " ";
    char       *path;
    char       *last_path;
    char       *tmp_path;
    char       *tmp_row;
    FILE       *fp;
    int         first;
    int         row;
    int         insert;

    if (bookmarks_initialized == 0) {
        bookmarks_initialized = 1;
    } else {
      return;
    }

    if (!ys->options.no_init) {
        path = get_config_item_path("my_bookmarks.txt");
        fp   = fopen (path, "r");
	    free(path);
    }

    if (fp == NULL) {
        return;
    }

    bookmark_data_t tmp;
    last_path = NULL;
    first     = 1;
    while( fgets( line, 512, fp ) != NULL ) {
        tmp_path = strtok(line, s);
        tmp_row  = strtok(NULL, s);
	
    	if (tmp_path == NULL || tmp_row == NULL){
    	    continue;
    	}

        if (first) {
            tmp.rows = array_make(int);
            row      = atoi(tmp_row);
            array_push(tmp.rows, row);
            first = 0;
        } else if (tmp_path == last_path) {
            row = atoi(tmp_row);
            array_push(tmp.rows, row);
        } else {
            tree_insert(bookmarks, strdup(tmp_path), tmp);
            tmp.rows = array_make(int);
            row      = atoi(tmp_row);
            array_push(tmp.rows, row);
        }

        last_path = tmp_path;
    }
    tree_insert(bookmarks, strdup(tmp_path), tmp);
    fclose(fp);
    yed_delete_event_handler(h);

    DBG("bookmarks initialized: %d files loaded", tree_len(bookmarks));
}

void _write_back_bookmarks(void) {
    char        line[512];
    char        str[512];
    const char  s[2] = " ";
    char       *path;
    char       *last_path;
    char       *tmp_path;
    char       *tmp_row;
    FILE       *fp;
    int         i;
    int         row;
    int         first;
    int        *r_it;


    tree_it(yedrc_path_t, bookmark_data_t) it;

    if (!ys->options.no_init) {
        path = get_config_item_path("my_bookmarks.txt");
        fp   = fopen (path, "r");
	    free(path);
    }

    if (fp == NULL) {
        goto cont;
/*         return; */
    }

    bookmark_data_t tmp;
    last_path = NULL;
    while( fgets( line, 512, fp ) != NULL ) {
        tmp_path = strtok(line, s);
        tmp_row  = strtok(NULL, s);
	
    	if (tmp_path == NULL || tmp_row == NULL){
    	    continue;
    	}

        if (first) {
            tmp.rows = array_make(int);
            row = atoi(tmp_row);
            array_push(tmp.rows, row);
            first = 0;
        } else if (tmp_path == last_path) {
            row = atoi(tmp_row);
            array_push(tmp.rows, row);
        } else {
            tree_insert(bookmarks, strdup(tmp_path), tmp);
            tmp.rows = array_make(int);
            row = atoi(tmp_row);
            array_push(tmp.rows, row);
        }


        last_path = tmp_path;
    }

    fclose(fp);

cont:;
    if (!ys->options.no_init) {
        path = get_config_item_path("my_bookmarks.txt");
        fp = fopen (path, "w+");
    	free(path);
    }

    if (fp == NULL) {
        return;
    }

    tree_traverse(bookmarks, it) {
        array_traverse(tree_it_val(it).rows, r_it) {
            fprintf(fp, "%s %d\n", tree_it_key(it), *r_it);
        }
    }

    fclose(fp);
}