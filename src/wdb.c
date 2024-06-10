#include "wdb.h"

#include <string.h> /* for strlen */
#include <stdio.h> /* for snprintf */

#define SOB_WDB_CHECK_RESTORE_LEN_(stmt) \
    do { \
        const enum wdb_res SOB_WDB_CHECK_res_ = (stmt); \
        if (SOB_WDB_CHECK_res_ != wdb_ok) { \
            c->len = last_len; \
            return SOB_WDB_CHECK_res_; \
        } \
    } while (0)
#define SOB_WDB_CHECK_KEY_() \
    do { \
        if (! c->got_key) { \
            return wdb_syntax; \
        } \
    } while (0)
#define SOB_WDB_MAYBE_ARR_() SOB_WDB_CHECK_RESTORE_LEN_(maybe_arr_(c))

struct wdb_ctx {
    char * out;
    size_t mlen;
    size_t len;

    int got_key;
    int is_first_val;
};

static enum wdb_res maybe_arr_(struct wdb_ctx * c);
static enum wdb_res add_literal_(struct wdb_ctx * c, const char * str);
static enum wdb_res add_ch_(struct wdb_ctx * c, char ch);

void wdb_init(struct wdb_ctx * c, char * out, size_t out_mlen)
{
    c->out = out;
    c->mlen = out_mlen;
    c->len = 0;
    c->got_key = 0;
    c->is_first_val = 0;
    if (c->mlen > 0) {
        c->out[0] = '\0';
    }
}

const char * wdb_out_str(const struct wdb_ctx * c)
{
    return c->out;
}

enum wdb_res wdb_key(struct wdb_ctx * c, const char * v)
{
    size_t last_len = c->len;
    if ((! c->got_key) || (! c->is_first_val)) {
        size_t i;

        if (c->got_key) {
            SOB_WDB_CHECK_RESTORE_LEN_(add_ch_(c, '\n'));
        }
        for (i = 0; i < strlen(v); i++) {
            char ch = v[i];
            if (! ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                        || (ch >= '0' && ch <= '9')
                        || ch == '.' || ch == '_' || ch == '-')) {
                c->len = last_len;
                return wdb_syntax;
            }
        }

        SOB_WDB_CHECK_RESTORE_LEN_(add_literal_(c, v));
        SOB_WDB_CHECK_RESTORE_LEN_(add_literal_(c, ": "));

        c->got_key = 1;
        c->is_first_val = 1;

        return wdb_ok;
    } else {
        c->len = last_len;
        return wdb_syntax;
    }
}

enum wdb_res wdb_str(struct wdb_ctx * c, const char * v)
{
    size_t last_len = c->len;
    size_t i;

    SOB_WDB_CHECK_KEY_();
    SOB_WDB_MAYBE_ARR_();
    c->is_first_val = 0;

    SOB_WDB_CHECK_RESTORE_LEN_(add_ch_(c, '"'));
    for (i = 0; i < strlen(v); i++) {
        char ch = v[i];
        int is_escape = 0;
        switch (ch) {
            case '"':
                ch = '"';
                is_escape = 1;
                break;
            case '\n':
                ch = 'n';
                is_escape = 1;
                break;
            case '\r':
                ch = 'r';
                is_escape = 1;
                break;
            default:
                if ((ch <= 31 || ch >= 127) && ch != '\t') {
                    /* control character */
                    c->len = last_len;
                    return wdb_syntax;
                }
                break;
        };
        if (is_escape ) {
            SOB_WDB_CHECK_RESTORE_LEN_(add_ch_(c, '\\'));
            /* ch was modified */
        }
        SOB_WDB_CHECK_RESTORE_LEN_(add_ch_(c, ch));
    }
    SOB_WDB_CHECK_RESTORE_LEN_(add_ch_(c, '"'));

    return wdb_ok;
}

enum wdb_res wdb_long_str(struct wdb_ctx * c, const char * v)
{
    size_t last_len = c->len;
    size_t i;

    SOB_WDB_CHECK_KEY_();
    SOB_WDB_MAYBE_ARR_();
    c->is_first_val = 0;

    SOB_WDB_CHECK_RESTORE_LEN_(add_literal_(c, "<\n"));
    for (i = 0; i < strlen(v); i++) {
        char ch = v[i];
        int is_escape = 0;
        switch (ch) {
            case '<':
            case '>':
                is_escape = 1;
                break;
            case '\r':
                ch = 'r';
                is_escape = 1;
                break;
            default:
                if ((ch <= 31 || ch >= 127) && ch != '\t' && ch != '\n') {
                    /* control character */
                    c->len = last_len;
                    return wdb_syntax;
                }
                break;
        };
        if (is_escape) {
            SOB_WDB_CHECK_RESTORE_LEN_(add_ch_(c, '\\'));
            /* ch was modified */
        }
        SOB_WDB_CHECK_RESTORE_LEN_(add_ch_(c, ch));
    }
    SOB_WDB_CHECK_RESTORE_LEN_(add_literal_(c, "\n>"));

    return wdb_ok;
}

enum wdb_res wdb_int(struct wdb_ctx * c, long v)
{
    size_t last_len = c->len;
    char buf[50];
    int written;

    SOB_WDB_CHECK_KEY_();
    SOB_WDB_MAYBE_ARR_();
    c->is_first_val = 0;

    written = snprintf(buf, sizeof(buf), "%li", v);
    if (written >= sizeof(buf)) {
        c->len = last_len;
        return wdb_overflow;
    }
    SOB_WDB_CHECK_RESTORE_LEN_(add_literal_(c, buf));
    return wdb_ok;
}

enum wdb_res wdb_num(struct wdb_ctx * c, double v)
{
    size_t last_len = c->len;
    char buf[100];
    int written;

    SOB_WDB_CHECK_KEY_();
    SOB_WDB_MAYBE_ARR_();
    c->is_first_val = 0;

    written = snprintf(buf, sizeof(buf), "%f", v);
    if (written >= sizeof(buf)) {
        c->len = last_len;
        return wdb_overflow;
    }
    SOB_WDB_CHECK_RESTORE_LEN_(add_literal_(c, buf));
    return wdb_ok;
}

enum wdb_res wdb_bool(struct wdb_ctx * c, int v)
{
    size_t last_len = c->len;
    SOB_WDB_CHECK_KEY_();
    SOB_WDB_MAYBE_ARR_();
    c->is_first_val = 0;
    SOB_WDB_CHECK_RESTORE_LEN_(add_literal_(c, v ? "true" : "false"));
    return wdb_ok;
}

enum wdb_res wdb_fin(struct wdb_ctx * c) {
    size_t last_len = c->len;
    if (c->got_key && c->is_first_val) {
        c->len = last_len;
        return wdb_syntax;
    } else {
        SOB_WDB_CHECK_RESTORE_LEN_(add_ch_(c, '\n'));
        SOB_WDB_CHECK_RESTORE_LEN_(add_ch_(c, '\0'));
        return wdb_ok;
    }
}

static enum wdb_res maybe_arr_(struct wdb_ctx * c)
{
    if (! c->is_first_val) {
        return add_literal_(c, ", ");
    } else {
        return wdb_ok;
    }
}

static enum wdb_res add_literal_(struct wdb_ctx * c, const char * str)
{
    size_t i;
    for (i = 0; i < strlen(str); i++) {
        SOB_WDB_CHECK(add_ch_(c, str[i]));
    }
    return wdb_ok;
}

static enum wdb_res add_ch_(struct wdb_ctx * c, char ch)
{
    if (c->len < c->mlen) {
        c->out[c->len] = ch;
        c->len++;
        return wdb_ok;
    } else {
        return wdb_overflow;
    }
}

#undef SOB_WDB_CHECK_KEY_
#undef SOB_WDB_MAYBE_ARR_

#ifdef SOB_WDB_DEMO

#include <stdio.h>

#define MY_WDB_CHECK_(stmt) \
    do { \
        const enum wdb_res MY_WDB_CHECK_res_ = (stmt); \
        if (MY_WDB_CHECK_res_ != wdb_ok) { \
            printf("err @ %s (%i): %i\n", \
                #stmt, __LINE__, MY_WDB_CHECK_res_); \
            printf("'%s'\n", wdb_out_str(&c)); \
            return 1; \
        } \
    } while (0)

enum {
    str_mlen_ = 4
};

int main(void) {
    struct wdb_ctx c;
    char str[str_mlen_];

    wdb_init(&c, str, str_mlen_);

    MY_WDB_CHECK_(wdb_key(&c, "hello"));
    MY_WDB_CHECK_(wdb_str(&c, " , \"world\"!\n"));

    MY_WDB_CHECK_(wdb_key(&c, "long"));
    MY_WDB_CHECK_(wdb_long_str(&c, "\nlong\n    - loong\n        - looong\n"));

    MY_WDB_CHECK_(wdb_key(&c, "list"));
    MY_WDB_CHECK_(wdb_str(&c, "one"));
    MY_WDB_CHECK_(wdb_int(&c, 22));
    MY_WDB_CHECK_(wdb_num(&c, 3.14));
    MY_WDB_CHECK_(wdb_bool(&c, 1));
    MY_WDB_CHECK_(wdb_bool(&c, 0));

    MY_WDB_CHECK_(wdb_key(&c, "multilist"));
    MY_WDB_CHECK_(wdb_long_str(&c, "first\nsecond\nthird"));
    MY_WDB_CHECK_(wdb_long_str(&c, "\nfirstfirst\nsecondsecond"));
    MY_WDB_CHECK_(wdb_long_str(&c, "<secondfirst>\n\"secondsecond\"\n\n"));
    MY_WDB_CHECK_(wdb_str(&c, "last"));
    MY_WDB_CHECK_(wdb_fin(&c));

    printf("'%s'\n", wdb_out_str(&c));

    return 0;
}

#endif /* SOB_WDB_DEMO */

