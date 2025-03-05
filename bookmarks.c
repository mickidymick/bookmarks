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

//saving these for later
//

typedef struct bookmark_data {
    array_t rows;
}bookmark_data_t;

typedef char                        *yedrc_path_t;
use_tree_c(yedrc_path_t, bookmark_data_t, strcmp);
tree(yedrc_path_t, bookmark_data_t)  bookmarks;
yed_event_handler                    h;
int                                  bookmarks_initialized;
yed_plugin                          *Self;
static time_t                        last_time;
static time_t                        orig_time;
static time_t                        wait_time;
static int                           scomp_save = -1;

static yed_buffer *_get_or_make_buff(void);
static void        _bookmarks_init(void);
static void        _unload(yed_plugin *self);
static void        _init_bookmarks(yed_event *event);
static void        _write_back_bookmarks(void);
static void        _write_bookmarks(yed_event *event);
static void        _bookmarks_line_handler(yed_event *event);
static void        _set(yed_frame *frame, int row);
static void        _remove(yed_frame *frame, int row);
static int         _cmpintp(const void *a, const void *b);
static void        _update_bookmarks(yed_event *event);
static void        _update_special_buffer(yed_event *event);
static void        _special_buffer_line_handler(yed_event *event);
static void        _special_buffer_key_pressed_handler(yed_event *event);
static void        _line_numbers_line_handler(yed_event *event);
static void        _line_numbers_frame_pre_update(yed_event *event);
static int         _n_digits(int i);

void set_new_bookmark(int nargs, char **args);
void set_new_bookmark_on_line(int nargs, char **args);
void remove_bookmark(int nargs, char **args);
void remove_bookmark_on_line(int nargs, char **args);
void goto_next_bookmark(int nargs, char **args);
void goto_next_bookmark_in_buffer(int nargs, char **args);
void goto_prev_bookmark(int nargs, char **args);
void goto_prev_bookmark_in_buffer(int nargs, char **args);
void remove_all_bookmarks_in_buffer(int nargs, char **args);
void _bookmarks(int nargs, char **args);

int yed_plugin_boot(yed_plugin *self) {
    yed_event_handler h1;
    yed_event_handler h2;
    yed_event_handler h3;
    yed_event_handler h4;
    yed_event_handler h5;
    yed_event_handler h6;
    yed_event_handler line;
    yed_event_handler frame_pre_update;

    YED_PLUG_VERSION_CHECK();

    bookmarks_initialized = 0;
    Self                  = self;

    yed_plugin_set_unload_fn(self, _unload);

    bookmarks = tree_make(yedrc_path_t, bookmark_data_t);

    yed_plugin_set_command(self, "set-bookmark", set_new_bookmark);
    yed_plugin_set_command(self, "set-bookmark-on-line", set_new_bookmark_on_line);
    yed_plugin_set_command(self, "remove-bookmark", remove_bookmark);
    yed_plugin_set_command(self, "remove-bookmark-on-line", remove_bookmark_on_line);
    yed_plugin_set_command(self, "goto-next-bookmark", goto_next_bookmark);
    yed_plugin_set_command(self, "goto-next-bookmark-in-buffer", goto_next_bookmark_in_buffer);
    yed_plugin_set_command(self, "goto-prev-bookmark", goto_prev_bookmark);
    yed_plugin_set_command(self, "goto-prev-bookmark-in-buffer", goto_prev_bookmark_in_buffer);
    yed_plugin_set_command(self, "remove-all-bookmarks-in-buffer", remove_all_bookmarks_in_buffer);
    yed_plugin_set_command(self, "bookmarks", _bookmarks);

    if (yed_get_var("bookmark-character") == NULL) {
        yed_set_var("bookmark-character", "▓");
    }

    if (yed_get_var("bookmark-color") == NULL) {
        yed_set_var("bookmark-color", "&blue");
    }

    if (yed_get_var("bookmark-update-period") == NULL) {
        yed_set_var("bookmark-update-period", "5");
    }

    if (yed_get_var("bookmark-path-color") == NULL) {
        yed_set_var("bookmark-path-color", "&green");
    }

    if (yed_get_var("bookmark-row-color") == NULL) {
        yed_set_var("bookmark-row-color", "&purple");
    }

    if (yed_get_var("bookmark-line-color") == NULL) {
        yed_set_var("bookmark-line-color", "&gray");
    }

    if (yed_get_var("bookmark-use-line-numbers") == NULL) {
        yed_set_var("bookmark-use-line-numbers", "0");
    }

    wait_time = 0;
    last_time = time(NULL);
    orig_time = time(NULL);

    _bookmarks_init();

    h.kind = EVENT_BUFFER_PRE_LOAD;
    h.fn   = _init_bookmarks;
    yed_plugin_add_event_handler(self, h);

    h1.kind = EVENT_PRE_PUMP;
    h1.fn   = _update_special_buffer;
    yed_plugin_add_event_handler(self, h1);

    h2.kind = EVENT_LINE_PRE_DRAW;
    h2.fn   = _bookmarks_line_handler;
    yed_plugin_add_event_handler(self, h2);

    h3.kind = EVENT_BUFFER_POST_MOD;
    h3.fn   = _update_bookmarks;
    yed_plugin_add_event_handler(self, h3);

    h4.kind = EVENT_BUFFER_POST_WRITE;
    h4.fn   = _write_bookmarks;
    yed_plugin_add_event_handler(self, h4);

    h5.kind = EVENT_LINE_PRE_DRAW;
    h5.fn   = _special_buffer_line_handler;
    yed_plugin_add_event_handler(self, h5);

    h6.kind = EVENT_KEY_PRESSED;
    h6.fn   = _special_buffer_key_pressed_handler;
    yed_plugin_add_event_handler(self, h6);

    line.kind = EVENT_LINE_PRE_DRAW;
    line.fn   = _line_numbers_line_handler;
    yed_plugin_add_event_handler(self, line);

    frame_pre_update.kind = EVENT_FRAME_PRE_UPDATE;
    frame_pre_update.fn   = _line_numbers_frame_pre_update;
    yed_plugin_add_event_handler(self, frame_pre_update);

    if (yed_get_var("line-number-scomp") == NULL) {
        yed_set_var("line-number-scomp", "code-comment");
    }

    return 0;
}

void _bookmarks(int nargs, char **args) {
    YEXE("special-buffer-prepare-focus", "*bookmarks-list");

    if (ys->active_frame) {
        YEXE("buffer", "*bookmarks-list");
    }

    yed_set_cursor_far_within_frame(ys->active_frame, 1, 1);
}

static void _special_buffer_key_pressed_handler(yed_event *event) {
    yed_frame *eframe;
    int       *r_it;
    int        loc;

    eframe = ys->active_frame;

    if (event->key != ENTER
    ||  ys->interactive_command
    ||  !eframe
    ||  !eframe->buffer
    ||  !ys->active_frame
    ||  strcmp(eframe->buffer->name, "*bookmarks-list")) {
        return;
    }

    loc = 1;
    tree_it(yedrc_path_t, bookmark_data_t) it;
    tree_traverse(bookmarks, it) {
        array_traverse(tree_it_val(it).rows, r_it) {
            if (loc == ys->active_frame->cursor_line) {
                YEXE("special-buffer-prepare-jump-focus", tree_it_key(it));
                YEXE("buffer", tree_it_key(it));
                if (ys->active_frame) {
                    yed_set_cursor_far_within_frame(ys->active_frame, *r_it, ys->active_frame->cursor_col);
                }
                goto done;
            }
            loc++;
        }
    }
done:;
    event->cancel = 1;
}

static void _special_buffer_line_handler(yed_event *event) {
    yed_attrs *attr_tmp;
    yed_attrs  attr_path;
    yed_attrs  attr_row;
    yed_attrs  attr_line;
    char      *color_var;
    yed_line  *line;
    int        loc;
    int        idx;

    if (!event->frame
    ||  !event->frame->buffer) {
        return;
    }

    if (strcmp(event->frame->buffer->name, "*bookmarks-list") != 0) {
        return;
    }

    attr_path = ZERO_ATTR;
    attr_row  = ZERO_ATTR;
    attr_line = ZERO_ATTR;

    if ((color_var = yed_get_var("bookmark-path-color"))) {
        attr_path = yed_parse_attrs(color_var);
    }

    if ((color_var = yed_get_var("bookmark-row-color"))) {
        attr_row = yed_parse_attrs(color_var);
    }

    if ((color_var = yed_get_var("bookmark-line-color"))) {
        attr_line = yed_parse_attrs(color_var);
    }

    line = yed_buff_get_line(event->frame->buffer, event->row);
    if (line == NULL) { return; }

    idx = 0;
    for (loc = 1; loc <= line->visual_width; loc += 1) {
        if (yed_line_col_to_glyph(line, loc)->c ==  ' ') {
            idx++;
        }

        if (idx == 0) {
            attr_tmp = &attr_path;
        } else if (idx == 1) {
            attr_tmp = &attr_row;
        } else {
            attr_tmp = &attr_line;
        }

        yed_eline_combine_col_attrs(event, loc, attr_tmp);
    }
}

static void _update_special_buffer(yed_event *event) {
    yed_buffer *buff;
    yed_buffer *buffer;
    int        *r_it;
    int         new_idx;
    int         path_width;
    int         row_width;
    int         tmp;
    time_t      curr_time;
    char       *path;
    char        line[512];
    char       *str;

    curr_time = time(NULL);
    if (curr_time > last_time + wait_time) {
        buff = _get_or_make_buff();
        buff->flags &= ~BUFF_RD_ONLY;
        yed_buff_clear_no_undo(buff);
        new_idx = 1;
        tree_it(yedrc_path_t, bookmark_data_t) it;
        path_width = 0;
        row_width  = 0;
        tree_traverse(bookmarks, it) {
            array_traverse(tree_it_val(it).rows, r_it) {
                path = (char *)malloc(sizeof(char[512]));
                path = relative_path_if_subtree(tree_it_key(it), path);
                if (strlen(path) > path_width) {
                    path_width = strlen(path);
                }
                free(path);

                tmp = floor(log10(abs(*r_it))) + 1;
                if (tmp > row_width) {
                    row_width = tmp;
                }
            }
        }
        tree_traverse(bookmarks, it) {
            array_traverse(tree_it_val(it).rows, r_it) {
                yed_buff_insert_line_no_undo(buff, new_idx);
                memset(line, 0, sizeof(char[512]));
                path = (char *)malloc(sizeof(char[512]));
                path = relative_path_if_subtree(tree_it_key(it), path);
                buffer = yed_get_buffer_by_path(tree_it_key(it));
                if (!buffer) {
                    YEXE("buffer-hidden", tree_it_key(it));
                    buffer = yed_get_buffer_by_path(tree_it_key(it));
                }
                if (buffer) {
                    str = yed_get_line_text(buffer, *r_it);
                    if (str) {
                        sprintf(line, "%*s %-*d %s", path_width, path, row_width, *r_it, str);
                        free(path);
                        yed_buff_insert_string_no_undo(buff, line, new_idx, 1);
                        new_idx++;
                    }
                }
            }
        }

        buff->flags |= BUFF_RD_ONLY;
        last_time = curr_time;
    }

    if (orig_time && curr_time > orig_time + 10) {
        orig_time = 0;
        wait_time = atoi(yed_get_var("bookmark-update-period"));
    }
}

static yed_buffer *_get_or_make_buff(void) {
    yed_buffer *buff;

    buff = yed_get_buffer("*bookmarks-list");

    if (buff == NULL) {
        buff = yed_create_buffer("*bookmarks-list");
        buff->flags |= BUFF_RD_ONLY | BUFF_SPECIAL;
    }

    return buff;
}

static void _bookmarks_init(void) {
    yed_buffer *buff;

    buff = _get_or_make_buff();
    buff->flags &= ~BUFF_RD_ONLY;
    yed_buff_clear_no_undo(buff);
    buff->flags |= BUFF_RD_ONLY;
}

void _update_bookmarks(yed_event *event) {
    char        file_name[512];
    yed_buffer *buff;
    int        *r_it;
    int         loc;

    buff = event->buffer;

    if (!buff
    ||  buff->path == NULL
    ||  buff->kind != BUFF_KIND_FILE) {
        return;
    }

    if (event->buff_mod_event != BUFF_MOD_INSERT_LINE
    &&  event->buff_mod_event != BUFF_MOD_DELETE_LINE) {
        return;
    }

    tree_it(yedrc_path_t, bookmark_data_t) it;
    abs_path(buff->path, file_name);

    it = tree_lookup(bookmarks, file_name);
    if ( tree_it_good(it) ) {
        loc = 0;
        array_traverse(tree_it_val(it).rows, r_it) {
            if (*r_it >= event->row) {
                if (event->buff_mod_event == BUFF_MOD_INSERT_LINE) {
                    (*r_it)++;
                } else if (event->buff_mod_event == BUFF_MOD_DELETE_LINE) {
                    (*r_it)--;

                }
            }
            loc++;
        }
    }
}

void _bookmarks_line_handler(yed_event *event) {
    char             file_name[512];
    bookmark_data_t  tmp;
    int              n_lines;
    int              n_cols;
    char             num_buff[16];
    yed_attrs        attr;
    yed_attrs        attr_bm;
    yed_attrs       *dst;
    yed_frame       *frame;
    int             *r_it;
    int              found;
    char            *color_var;

    if (event->frame->buffer == NULL
    ||  (event->frame->buffer->name
    &&  event->frame->buffer->name[0] == '*')) {

        yed_frame_set_gutter_width(event->frame, 0);
        return;
    }

    n_lines = yed_buff_n_lines(event->frame->buffer);

    if (atoi(yed_get_var("bookmark-use-line-numbers")) == 1) {
        n_cols = _n_digits(n_lines) + 3;
    } else {
        n_cols = 3;
    }

    if (event->frame->gutter_width != n_cols) {
        yed_frame_set_gutter_width(event->frame, n_cols);
    }

    array_clear(event->gutter_glyphs);
    if (atoi(yed_get_var("bookmark-use-line-numbers")) == 1) {
        snprintf(num_buff, sizeof(num_buff),
                " %*d", _n_digits(n_lines), event->row);

        array_push_n(event->gutter_glyphs, num_buff, strlen(num_buff));

        attr = yed_get_active_style_scomp(scomp_save);

        array_traverse(event->gutter_attrs, dst) {
            yed_combine_attrs(dst, &attr);
        }
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
                if (atoi(yed_get_var("bookmark-use-line-numbers")) == 1) {
                    snprintf(num_buff, sizeof(num_buff),
                            "%*s ", 1, yed_get_var("bookmark-character"));
                } else {
                    snprintf(num_buff, sizeof(num_buff),
                            " %*s ", 1, yed_get_var("bookmark-character"));
                }
                found = 1;
                break;
            }
        }
    }

    if (!found) {
        if (atoi(yed_get_var("bookmark-use-line-numbers")) == 1) {
            snprintf(num_buff, sizeof(num_buff),
                    "  ");
        } else {
            snprintf(num_buff, sizeof(num_buff),
                    "   ");
        }
    }

    array_push_n(event->gutter_glyphs, num_buff, strlen(num_buff));

    attr_bm = yed_parse_attrs("&blue");

    if ((color_var = yed_get_var("bookmark-color"))) {
        attr_bm = yed_parse_attrs(color_var);
    }

    int j = 0;
    array_traverse(event->gutter_attrs, dst) {
        if (atoi(yed_get_var("bookmark-use-line-numbers")) == 1) {
            if (j >= n_cols - 2) {
                yed_combine_attrs(dst, &attr_bm);
            }
        } else {
            yed_combine_attrs(dst, &attr_bm);
        }
        j++;
    }
}

static void _unload(yed_plugin *self) {
    yed_frame **fit;

    array_traverse(ys->frames, fit) {
        yed_frame_set_gutter_width(*fit, 0);
    }
}

static int _cmpintp(const void *a, const void *b) {
    return ( *(int*)a - *(int*)b );
}

void goto_next_bookmark(int nargs, char **args) {
    char             file_name[512];
    char            *path;
    yed_frame       *frame;
    bookmark_data_t  tmp;
    int             *r_it;
    int              row;
    yed_buffer      *buff;

    frame = ys->active_frame;

    if (!frame
    ||  !frame->buffer
    ||  frame->buffer->path == NULL
    ||  frame->buffer->kind != BUFF_KIND_FILE) {
        return;
    }

    tree_it(yedrc_path_t, bookmark_data_t) it;
    tree_it(yedrc_path_t, bookmark_data_t) l_it;
    abs_path(frame->buffer->path, file_name);

    if (tree_len(bookmarks) > 0) {
        path = NULL;
        it = tree_lookup(bookmarks, file_name);
        row = 0;

        if (tree_it_good(it) && array_len(tree_it_val(it).rows) > 0) {
            tmp = tree_it_val(it);
            array_traverse(tmp.rows, r_it) {
                if (*r_it > frame->cursor_line) {
                    row = *r_it;
                    path = strdup(tree_it_key(it));
                    break;
                }
            }

            if (!row) {
                goto find_next;
            }

        } else {
find_next:;
            l_it = tree_last(bookmarks);
            if (!tree_it_good(it) || strcmp(tree_it_key(l_it), tree_it_key(it)) == 0) {
                it = tree_begin(bookmarks);
            } else if (tree_len(bookmarks) > 1) {
                tree_it_next(it);
            }

            for (it; tree_it_good(it); tree_it_next(it)) {
                tmp = tree_it_val(it);
                if (array_len(tmp.rows) > 0) {
                    row = *(int *) array_item(tmp.rows, 0);
                    path = strdup(tree_it_key(it));
                    break;
                }
            }
        }

        if (row && path) {
            if (strcmp(path, frame->buffer->path) != 0) {
                buff = yed_get_buffer_by_path(path);
                if (buff) {
                    yed_frame_set_buff(frame, buff);
                }
            }
            yed_set_cursor_far_within_frame(frame, row, frame->cursor_col);
        }
    }
}

void goto_next_bookmark_in_buffer(int nargs, char **args) {
    char             file_name[512];
    yed_frame       *frame;
    bookmark_data_t  tmp;
    int             *r_it;
    int              row;

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
    row = 0;
    if ( tree_it_good(it) ) {
        tmp = tree_it_val(it);
        if (array_len(tmp.rows) > 0) {
            array_traverse(tmp.rows, r_it) {
                if (*r_it > frame->cursor_line) {
                    row = *r_it;
                    break;
                }
            }

            if (!row) {
                row = *(int *) array_item(tmp.rows, 0);
            }

            yed_set_cursor_far_within_frame(frame, row, frame->cursor_col);
        }
    }
}

void goto_prev_bookmark(int nargs, char **args) {
    char             file_name[512];
    char            *path;
    yed_frame       *frame;
    bookmark_data_t  tmp;
    int             *r_it;
    int              row;
    yed_buffer      *buff;

    frame = ys->active_frame;

    if (!frame
    ||  !frame->buffer
    ||  frame->buffer->path == NULL
    ||  frame->buffer->kind != BUFF_KIND_FILE) {
        return;
    }

    tree_it(yedrc_path_t, bookmark_data_t) it;
    tree_it(yedrc_path_t, bookmark_data_t) l_it;
    abs_path(frame->buffer->path, file_name);

    if (tree_len(bookmarks) > 0) {
        path = NULL;
        it = tree_lookup(bookmarks, file_name);
        row = 0;

        if (tree_it_good(it) && array_len(tree_it_val(it).rows) > 0) {
            tmp = tree_it_val(it);
            array_rtraverse(tmp.rows, r_it) {
                if (*r_it < frame->cursor_line) {
                    row = *r_it;
                    path = strdup(tree_it_key(it));
                    break;
                }
            }

            if (!row) {
                goto find_prev;
            }

        } else {
find_prev:;
            l_it = tree_last(bookmarks);
            if (!tree_it_good(it) || strcmp(tree_it_key(l_it), tree_it_key(it)) == 0) {
                it = tree_begin(bookmarks);
            } else if (tree_len(bookmarks) > 1) {
                tree_it_next(it);
            }

            for (it; tree_it_good(it); tree_it_next(it)) {
                tmp = tree_it_val(it);
                if (array_len(tmp.rows) > 0) {
                    row = *(int *) array_last(tmp.rows);
                    path = strdup(tree_it_key(it));
                    break;
                }
            }
        }

        if (row && path) {
            if (strcmp(path, frame->buffer->path) != 0) {
                buff = yed_get_buffer_by_path(path);
                if (buff) {
                    yed_frame_set_buff(frame, buff);
                }
            }
            yed_set_cursor_far_within_frame(frame, row, frame->cursor_col);
        }
    }
}

void goto_prev_bookmark_in_buffer(int nargs, char **args) {
    char             file_name[512];
    yed_frame       *frame;
    bookmark_data_t  tmp;
    int             *r_it;
    int              row;

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
    row = 0;
    if ( tree_it_good(it) ) {
        tmp = tree_it_val(it);
        if (array_len(tmp.rows) > 0) {
            array_rtraverse(tmp.rows, r_it) {
                if (*r_it < frame->cursor_line) {
                    row = *r_it;
                    break;
                }
            }

            if (!row) {
                row = *(int *) array_last(tmp.rows);
            }

            yed_set_cursor_far_within_frame(frame, row, frame->cursor_col);
        }
    }
}

static void _remove(yed_frame *frame, int row) {
    char             file_name[512];
    bookmark_data_t  tmp;
    int              idx;
    int              j;
    int              found;
    int             *r_it;

    tree_it(yedrc_path_t, bookmark_data_t) it;
    abs_path(frame->buffer->path, file_name);

    it = tree_lookup(bookmarks, file_name);
    found = 0;
    if ( tree_it_good(it) ) {
        tmp = tree_it_val(it);
        idx = 0;
        array_traverse(tmp.rows, r_it) {
            if (*r_it == row) {
                array_delete(tree_it_val(it).rows, idx);
                found = 1;
                LOG_FN_ENTER();
                yed_log("Bookmark removed at line %d\n", row);
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

    _write_back_bookmarks();
}

void remove_all_bookmarks_in_buffer(int nargs, char **args) {
    yed_frame       *frame;
    char             file_name[512];
    bookmark_data_t  tmp;
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
        array_clear(tree_it_val(it).rows);
        LOG_FN_ENTER();
        yed_log("All bookmarks removed from %s\n", frame->name);
        LOG_EXIT();
    } else {
        yed_cerr("No bookmark exists for %s!\n", frame->name);
    }

    _write_back_bookmarks();
}

void remove_bookmark(int nargs, char **args) {
    yed_frame       *frame;

    frame = ys->active_frame;

    if (!frame
    ||  !frame->buffer
    ||  frame->buffer->path == NULL
    ||  frame->buffer->kind != BUFF_KIND_FILE) {
        return;
    }

    if (nargs != 1) {
        yed_cerr("Missing row parameter!\n");
        return;
    }
    _remove(frame, atoi(args[0]));
}

void remove_bookmark_on_line(int nargs, char **args) {
    yed_frame       *frame;

    frame = ys->active_frame;

    if (!frame
    ||  !frame->buffer
    ||  frame->buffer->path == NULL
    ||  frame->buffer->kind != BUFF_KIND_FILE) {
        return;
    }

    _remove(frame, ys->active_frame->cursor_line);
}

static void _set(yed_frame *frame, int row) {
    char             file_name[512];
    bookmark_data_t  tmp;
    int              i;
    int             *r_it;
    array_t         *arr;

    tree_it(yedrc_path_t, bookmark_data_t) it;
    abs_path(frame->buffer->path, file_name);

    it = tree_lookup(bookmarks, file_name);
    if ( tree_it_good(it) ) {
        tmp = tree_it_val(it);
        array_traverse(tmp.rows, r_it) {
            if (*r_it == row) {
                yed_cerr("A bookmark for this line already exists!\n");
                goto skip;
            }
        }
        array_push(tree_it_val(it).rows, row);
        qsort((void *)(tree_it_val(it).rows.data), array_len(tree_it_val(it).rows), sizeof(int), _cmpintp);
    } else {
        tmp.rows = array_make(int);
        array_push(tmp.rows, row);
        qsort((void *)tmp.rows.data, array_len(tmp.rows), sizeof(int), _cmpintp);
        tree_insert(bookmarks, strdup(frame->buffer->path), tmp);
    }

    LOG_FN_ENTER();
    yed_log("Bookmark set at line %d\n", row);
    LOG_EXIT();

skip:;

    _write_back_bookmarks();
}


void set_new_bookmark(int nargs, char **args) {
    yed_frame       *frame;

    frame = ys->active_frame;

    if (!frame
    ||  !frame->buffer
    ||  frame->buffer->path == NULL
    ||  frame->buffer->kind != BUFF_KIND_FILE) {
        return;
    }

    if (nargs != 1) {
        yed_cerr("Missing row parameter!\n");
        return;
    }

    _set(frame, atoi(args[0]));
}

void set_new_bookmark_on_line(int nargs, char **args) {
    yed_frame       *frame;

    frame = ys->active_frame;

    if (!frame
    ||  !frame->buffer
    ||  frame->buffer->path == NULL
    ||  frame->buffer->kind != BUFF_KIND_FILE) {
        return;
    }

    _set(frame, ys->active_frame->cursor_line);
}

static void _init_bookmarks(yed_event *event) {
    char        line[512];
    char        str[512];
    char        cwd[512];
    char        loc_file[1024];
    char       *file;
    const char  s[2] = " ";
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

    file = yed_get_var("bookmarks-file");
    if (file == NULL) {
        return;
    }

    if (!ys->options.no_init) {
        getcwd(cwd, sizeof(cwd));
        memset(loc_file, 0, sizeof(loc_file[1024]));
        strcat(loc_file, cwd);
        strcat(loc_file, "/");
        strcat(loc_file, file);
        fp   = fopen (loc_file, "r");
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
        } else if (strcmp(tmp_path, last_path) == 0) {
            row = atoi(tmp_row);
            array_push(tmp.rows, row);
        } else {
            qsort((void *)tmp.rows.data, array_len(tmp.rows), sizeof(int), _cmpintp);
            tree_insert(bookmarks, strdup(last_path), tmp);
            tmp.rows = array_make(int);
            row      = atoi(tmp_row);
            array_push(tmp.rows, row);
        }

        if(last_path != NULL) {
            free(last_path);
        }
        last_path = strdup(tmp_path);
    }

    if (first != 1 && tmp_path != NULL) {
        qsort((void *)tmp.rows.data, array_len(tmp.rows), sizeof(int), _cmpintp);
        tree_insert(bookmarks, strdup(tmp_path), tmp);
    }

    fclose(fp);
    yed_delete_event_handler(h);

    DBG("bookmarks initialized: %d files loaded", tree_len(bookmarks));
}

static void _write_bookmarks(yed_event *event) {
    _write_back_bookmarks();
}

void _write_back_bookmarks(void) {
    char        cwd[512];
    char        loc_file[1024];
    char       *file;
    FILE       *fp;
    int        *r_it;

    file = yed_get_var("bookmarks-file");
    if (file == NULL) {
        return;
    }

    if (!ys->options.no_init) {
        getcwd(cwd, sizeof(cwd));
        memset(loc_file, 0, sizeof(loc_file[1024]));
        strcat(loc_file, cwd);
        strcat(loc_file, "/");
        strcat(loc_file, file);
        fp = fopen (loc_file, "w+");
    }

    if (fp == NULL) {
        return;
    }

    tree_it(yedrc_path_t, bookmark_data_t) it;
    tree_traverse(bookmarks, it) {
        array_traverse(tree_it_val(it).rows, r_it) {
            fprintf(fp, "%s %d\n", tree_it_key(it), *r_it);
        }
    }

    fclose(fp);
}

static void _line_numbers_line_handler(yed_event *event) {
    int        n_lines;
    int        n_cols;
    char       num_buff[16];
    yed_attrs  attr;
    yed_attrs *dst;

    if (event->frame->buffer == NULL
    ||  (event->frame->buffer->name
    &&  event->frame->buffer->name[0] == '*')) {

        yed_frame_set_gutter_width(event->frame, 0);
        return;
    }

    n_lines = yed_buff_n_lines(event->frame->buffer);
    n_cols  = _n_digits(n_lines) + 2;

    if (event->frame->gutter_width != n_cols) {
        yed_frame_set_gutter_width(event->frame, n_cols);
    }

    snprintf(num_buff, sizeof(num_buff),
             " %*d ", n_cols - 2, event->row);

    array_clear(event->gutter_glyphs);
    array_push_n(event->gutter_glyphs, num_buff, strlen(num_buff));

    attr = yed_get_active_style_scomp(scomp_save);

    array_traverse(event->gutter_attrs, dst) {
        yed_combine_attrs(dst, &attr);
    }
}

static void _line_numbers_frame_pre_update(yed_event *event) {
    int scomp;

    scomp = yed_scomp_nr_by_name(yed_get_var("line-number-scomp"));
    scomp_save = scomp;

    if (event->frame->buffer == NULL) {
        yed_frame_set_gutter_width(event->frame, 0);
    }
}

static int _n_digits(int i) {
    int n;
    int x;

    if (i <= 0) { return 1; }

    n = 1;
    x = 10;

    while (i / x) {
        n += 1;
        x *= 10;
    }

    return n;
}
