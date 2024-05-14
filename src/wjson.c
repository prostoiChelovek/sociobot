#include <stdio.h> /* for snprintf */

#include "wjson.h"
#include "panic.h"

/* #undef is at the bottom */
#define WJSON_ADD_CH_(ch) SOB_WJSON_CHECK(add_ch_(c, (ch)))

enum {
    max_depth_ = 24,
    indentation_ = 4
};

enum st_ {
    st_none_ = 0,
    st_want_key_,
    st_want_val_
};

enum lvl_ty_ {
    lvl_none_ = 0,
    lvl_obj_,
    lvl_arr_
};

static enum wjson_res add_ch_(struct wjson_ctx * c, char ch);
static enum wjson_res maybe_comma_(struct wjson_ctx * c);
static enum wjson_res maybe_indent_(struct wjson_ctx * c);

static enum wjson_res add_literal_(struct wjson_ctx * c, char * str);

static void add_lvl_(struct wjson_ctx * c, enum lvl_ty_ ty);
static void pop_lvl_(struct wjson_ctx * c);
static enum lvl_ty_ lvl_(const struct wjson_ctx * c);

static int escape_ch_(char * ch);

struct wjson_ctx {
    int is_pretty;
    char * str;
    size_t mlen;
    size_t len;

    enum lvl_ty_ lvls[max_depth_];
    size_t lvls_len;

    enum st_ st;
    int need_comma;
    int is_first;
};

void wjson_init(struct wjson_ctx * c, char * out_str, size_t out_str_max_len,
    int is_pretty)
{
    c->is_pretty = is_pretty;
    c->str = out_str;
    c->mlen = out_str_max_len;
    c->len = 0;

    c->st = st_none_;
    c->need_comma = 0;

    if (c->mlen >= 1) {
        c->str[0] = '\0';
    }
    if (c->mlen >= 2) {
        c->str[c->mlen - 1] = '\0';
    }
}

const char * wjson_out_str(const struct wjson_ctx * c)
{
    return c->str;
}

enum wjson_res wjson_str(struct wjson_ctx * c, char * str)
{
    SOB_WJSON_CHECK(maybe_comma_(c));
    if (c->is_pretty && c->is_first) {
        SOB_WJSON_CHECK(add_ch_(c, '\n'));
    }
    SOB_WJSON_CHECK(maybe_indent_(c));
    c->is_first = 0;

    WJSON_ADD_CH_('"');

    while (*str != '\0') {
        char ch = *str;
        int need_escape = escape_ch_(&ch);
        if (need_escape) {
            WJSON_ADD_CH_('\\');
        }
        WJSON_ADD_CH_(ch);
        str++;
    }

    WJSON_ADD_CH_('"');

    if (c->st == st_want_val_) {
        c->st = st_want_key_;
        c->need_comma = 1;
    } else if (c->st == st_want_key_) {
        c->need_comma = 0;
        if (lvl_(c) == lvl_obj_) {
            WJSON_ADD_CH_(':');
            if (c->is_pretty) {
                WJSON_ADD_CH_(' ');
            }
            c->st = st_want_val_;
        } else {
            SOB_PANIC("st_want_key can only be in obj");
        }
    } else {
        c->need_comma = 1;
    }

    return wjson_ok;
}

enum wjson_res wjson_int(struct wjson_ctx * c, long long num)
{
    char buf[50];
    int written;

    if (c->st != st_none_ && c->st != st_want_val_) {
        return wjson_syntax;
    }

    written = snprintf(buf, sizeof(buf), "%lli", num);
    if (written >= sizeof(buf)) {
        return wjson_overflow;
    }
    return add_literal_(c, buf);
}

enum wjson_res wjson_double(struct wjson_ctx * c, double num)
{
    char buf[100];
    int written;

    if (c->st != st_none_ && c->st != st_want_val_) {
        return wjson_syntax;
    }

    written = snprintf(buf, sizeof(buf), "%f", num);
    if (written >= sizeof(buf)) {
        return wjson_overflow;
    }
    return add_literal_(c, buf);
}

enum wjson_res wjson_bool(struct wjson_ctx * c, int is_true)
{
    if (is_true) {
        return add_literal_(c, "true");
    } else {
        return add_literal_(c, "false");
    }
}

enum wjson_res wjson_null(struct wjson_ctx * c)
{
    return add_literal_(c, "null");
}

enum wjson_res wjson_obj_start(struct wjson_ctx * c)
{
    if (c->st != st_none_ && c->st != st_want_val_) {
        return wjson_syntax;
    }

    SOB_WJSON_CHECK(maybe_comma_(c));
    if (c->is_pretty && c->is_first) {
        SOB_WJSON_CHECK(add_ch_(c, '\n'));
    }
    SOB_WJSON_CHECK(maybe_indent_(c));

    WJSON_ADD_CH_('{');

    c->st = st_want_key_;
    c->is_first = 1;
    c->need_comma = 0;
    add_lvl_(c, lvl_obj_);

    return wjson_ok;
}

enum wjson_res wjson_obj_end(struct wjson_ctx * c)
{
    if (lvl_(c) != lvl_obj_) {
        return wjson_syntax;
    }
    if (c->st == st_want_val_) {
        return wjson_syntax;
    }

    pop_lvl_(c);

    if (c->is_pretty && ! c->is_first) {
        WJSON_ADD_CH_('\n');
        SOB_WJSON_CHECK(maybe_indent_(c));
    }

    c->is_first = 0;
    c->st = st_none_;
    if (lvl_(c) == lvl_obj_) {
        c->st = st_want_key_;
    }
    c->need_comma = 1;

    WJSON_ADD_CH_('}');

    return wjson_ok;
}

enum wjson_res wjson_arr_start(struct wjson_ctx * c)
{
    if (c->st == st_want_key_) {
        return wjson_syntax;
    }

    SOB_WJSON_CHECK(maybe_comma_(c));
    if (c->is_pretty && c->is_first) {
        SOB_WJSON_CHECK(add_ch_(c, '\n'));
    }
    SOB_WJSON_CHECK(maybe_indent_(c));
    WJSON_ADD_CH_('[');

    add_lvl_(c, lvl_arr_);
    c->need_comma = 0;
    c->st = st_none_;
    c->is_first = 1;

    return wjson_ok;
}

enum wjson_res wjson_arr_end(struct wjson_ctx * c)
{
    if (lvl_(c) != lvl_arr_) {
        return wjson_syntax;
    }

    pop_lvl_(c);

    if (c->is_pretty && ! c->is_first) {
        SOB_WJSON_CHECK(add_ch_(c, '\n'));
        SOB_WJSON_CHECK(maybe_indent_(c));
    }

    c->is_first = 0;
    if (lvl_(c) == lvl_obj_) {
        c->st = st_want_key_;
        c->need_comma = 0;
    } else if (lvl_(c) == lvl_arr_) {
        c->need_comma = 1;
    }

    WJSON_ADD_CH_(']');

    return wjson_ok;
}

static enum wjson_res add_ch_(struct wjson_ctx * c, char ch)
{
    if (c->len + 2 > c->mlen) {
        return wjson_overflow;
    }
    c->str[c->len] = ch;
    c->len++;
    return wjson_ok;
}

static enum wjson_res maybe_comma_(struct wjson_ctx * c)
{
    if (c->need_comma) {
        WJSON_ADD_CH_(',');
        if (c->is_pretty) {
            WJSON_ADD_CH_('\n');
        }
        c->need_comma = 0;
    }
    return wjson_ok;
}

static enum wjson_res maybe_indent_(struct wjson_ctx * c)
{
    if (c->is_pretty) {
        if (c->st != st_want_val_) {
            int i;
            for (i = 0; i < c->lvls_len; i++) {
                int j;
                if (c->len + indentation_ + 1 > c->mlen) {
                    return wjson_overflow;
                }
                for (j = 0; j < indentation_; j++) {
                    c->str[c->len] = ' ';
                    c->len++;
                }
            }
        }
    }
    return wjson_ok;
}

static enum wjson_res add_literal_(struct wjson_ctx * c, char * str)
{
    if (c->st != st_none_ && c->st != st_want_val_) {
        return wjson_syntax;
    }
    SOB_WJSON_CHECK(maybe_comma_(c));
    if (c->is_pretty && c->is_first) {
        SOB_WJSON_CHECK(add_ch_(c, '\n'));
    }
    SOB_WJSON_CHECK(maybe_indent_(c));
    c->is_first = 0;
    c->need_comma = 1;

    while (*str != '\0') {
        WJSON_ADD_CH_(*str);
        str++;
    }

    if (c->st == st_want_val_) {
        c->st = st_want_key_;
    }

    return wjson_ok;
}

static void add_lvl_(struct wjson_ctx * c, enum lvl_ty_ lvl)
{
    if (c->lvls_len == max_depth_) {
        /* XXX: not exactly panic-worthy but I don't care enough to handle it */
        SOB_PANIC("add_lvl_: max depth reached");
    }
    c->lvls[c->lvls_len] = lvl;
    c->lvls_len++;
}

static void pop_lvl_(struct wjson_ctx * c)
{
    if (c->lvls_len == 0) { /* cannot happen during normal operation */
        SOB_PANIC("pop_lvl_: no levels to pop");
    }
    c->lvls_len--;
}

static enum lvl_ty_ lvl_(const struct wjson_ctx * c)
{
    if (c->lvls_len == 0) {
        return lvl_none_;
    }
    return c->lvls[c->lvls_len - 1];
}

static int escape_ch_(char * ch)
{
    switch (*ch) {
    case '\b':
        *ch = 'b';
        return 1;
    case '\f':
        *ch = 'f';
        return 1;
    case '\n':
        *ch = 'n';
        return 1;
    case '\r':
        *ch = 'r';
        return 1;
    case '\t':
        *ch = 't';
        return 1;
    case '"':
        *ch = '"';
        return 1;
    case '\\':
        *ch = '\\';
        return 1;
    default:
        return 0;
    };
}

#undef WJSON_ADD_CH_

#ifdef SOB_WJSON_DEMO

#include <stdio.h>

#define WJSON_MY_CHECK_(stm) \
    do { \
        enum wjson_res r = (stm); \
        if (r != wjson_ok) { \
            fprintf(stderr, "Error %i @ %i : %s\n", r, __LINE__, #stm); \
            fprintf(stderr, "'%s'\n", wjson_out_str(&c)); \
            return 1; \
        } \
    } while (0)

enum {
    str_mlen_ = 1024
};

int main(void) {
    char str[str_mlen_];
    struct wjson_ctx c;

    wjson_init(&c, str, str_mlen_, 1);
    WJSON_MY_CHECK_(wjson_obj_start(&c));
    WJSON_MY_CHECK_(wjson_str(&c, "hello"));
    WJSON_MY_CHECK_(wjson_str(&c, "world\n"));
    WJSON_MY_CHECK_(wjson_str(&c, "a"));

    WJSON_MY_CHECK_(wjson_obj_start(&c));
    WJSON_MY_CHECK_(wjson_str(&c, "b"));
    WJSON_MY_CHECK_(wjson_obj_start(&c));
    WJSON_MY_CHECK_(wjson_obj_end(&c));
    WJSON_MY_CHECK_(wjson_str(&c, "d"));

    WJSON_MY_CHECK_(wjson_arr_start(&c));
    WJSON_MY_CHECK_(wjson_str(&c, "first"));
    WJSON_MY_CHECK_(wjson_str(&c, "second"));
    WJSON_MY_CHECK_(wjson_str(&c, "third"));

    WJSON_MY_CHECK_(wjson_arr_start(&c));
    WJSON_MY_CHECK_(wjson_str(&c, "obj"));
    WJSON_MY_CHECK_(wjson_obj_start(&c));
    WJSON_MY_CHECK_(wjson_str(&c, "second"));
    WJSON_MY_CHECK_(wjson_null(&c));
    WJSON_MY_CHECK_(wjson_str(&c, "third"));
    WJSON_MY_CHECK_(wjson_arr_start(&c));
    WJSON_MY_CHECK_(wjson_int(&c, 228));
    WJSON_MY_CHECK_(wjson_str(&c, "777"));
    WJSON_MY_CHECK_(wjson_double(&c, 3.14));
    WJSON_MY_CHECK_(wjson_int(&c, 420));
    WJSON_MY_CHECK_(wjson_null(&c));
    WJSON_MY_CHECK_(wjson_arr_end(&c));
    WJSON_MY_CHECK_(wjson_obj_end(&c));
    WJSON_MY_CHECK_(wjson_arr_end(&c));

    WJSON_MY_CHECK_(wjson_arr_end(&c));

    WJSON_MY_CHECK_(wjson_obj_end(&c));

    WJSON_MY_CHECK_(wjson_obj_end(&c));

    printf("'%s'\n", wjson_out_str(&c));

    return 0;
}

#undef WJSON_MY_CHECK_

#endif /* SOB_WJSON_DEMO */

