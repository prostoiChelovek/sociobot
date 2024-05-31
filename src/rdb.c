#include "rdb.h"

#include "panic.h"

#include <math.h>
#include <string.h>

/* undef at the bottom */
#define SOB_ASSERT_ST_(c, expected) \
    do { \
        if (c->st != expected) { \
            SOB_PANIC("unexpected state: got %i vs %i", c->st, expected); \
        } \
    } while(0)

enum st_ {
    st_idle_ = 0,
    st_key_,
    st_str_,
    st_long_str_,
    st_num_,
    st_bool_
};

struct rdb_ctx {
    char * str_out;
    size_t str_mlen;

    size_t pos;

    enum rdb_ty ty;
    enum st_ st;
    int is_in_arr;
    int got_key;
    int expect_colon;
    int got_first_val;
    union {
        struct sd_key_ {
            size_t len;
        } key;
        struct sd_str_ {
            size_t len;
            int is_escape;
        } str;
        struct sd_long_str_ {
            size_t len;
            int is_escape;
            int skip_whitespace;
            int keep_last_newline;
        } long_str;
        struct sd_num_ {
            int is_negative;
            int got_sign;
            int got_int_explicit;
            double num;
            int exp_is_negative;
            int exp;
            int pos;
            enum {
                num_part_int_,
                num_part_frac_,
                num_part_exp_
            } part;
        } num;
        struct sd_bool_ {
            char word[6];
            size_t wlen;
        } boolean;
    } sd;
};

static enum rdb_next_res next_idle_(struct rdb_ctx * c, char ch);
static enum rdb_next_res next_key_(struct rdb_ctx * c, char ch);
static enum rdb_next_res next_str_(struct rdb_ctx * c, char ch);
static enum rdb_next_res next_long_str_(struct rdb_ctx * c, char ch);
static enum rdb_next_res next_num_(struct rdb_ctx * c, char ch);
static enum rdb_next_res next_bool_(struct rdb_ctx * c, char ch);

static enum rdb_next_res next_num_int_(struct rdb_ctx * c, char ch);
static enum rdb_next_res next_num_frac_(struct rdb_ctx * c, char ch);
static enum rdb_next_res next_num_exp_(struct rdb_ctx * c, char ch);

static void set_st_(struct rdb_ctx * c, enum st_ st);
static char escape_ch_(char ch);
static int is_separator_(char ch);

void rdb_init(struct rdb_ctx * c, char * str_out_buf, size_t str_maxlen)
{
    c->str_out = str_out_buf;
    c->str_mlen = str_maxlen;
    c->pos = 0;
    c->ty = rdb_incomplete;
    c->st = st_idle_;
    c->is_in_arr = 0;
    c->got_key = 0;
    c->expect_colon = 0;
    c->got_first_val = 0;
    memset(&c->sd, 0, sizeof(c->sd));
}

enum rdb_next_res rdb_next(struct rdb_ctx * c, char ch)
{
    enum rdb_next_res r;
    c->ty = rdb_incomplete;
    switch (c->st) {
    case st_idle_:
        r = next_idle_(c, ch);
        break;
    case st_key_:
        r = next_key_(c, ch);
        break;
    case st_str_:
        r = next_str_(c, ch);
        break;
    case st_long_str_:
        r = next_long_str_(c, ch);
        break;
    case st_num_:
        r = next_num_(c, ch);
        break;
    case st_bool_:
        r = next_bool_(c, ch);
        break;
    }
    if (r != rdb_next_syntax) {
        c->pos++;
    }
    return r;
}

size_t rdb_pos(const struct rdb_ctx * c)
{
    return c->pos;
}

enum rdb_ty rdb_cur_ty(const struct rdb_ctx * c)
{
    return c->ty;
}
const char * rdb_cur_str(const struct rdb_ctx * c)
{
    return c->str_out;
}
double rdb_cur_num(const struct rdb_ctx * c)
{
    const struct sd_num_ * sd = &c->sd.num;
    int exponent = (sd->exp_is_negative ? -1 : 1) * sd->exp;
    return (sd->is_negative ? -1 : 1) * (sd->num * pow(10, exponent));
}
int rdb_cur_is_true(const struct rdb_ctx * c)
{
    return strncmp("true", c->sd.boolean.word, c->sd.boolean.wlen) == 0;
}

static enum rdb_next_res next_idle_(struct rdb_ctx * c, char ch)
{
    int is_val_expected = c->got_key
        && (c->is_in_arr || ! c->got_first_val)
        && ! c->expect_colon;

    SOB_ASSERT_ST_(c, st_idle_);

    if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.') {
        if (is_val_expected) {
            set_st_(c, st_num_);
            return next_num_(c, ch);
        } else {
            return rdb_next_syntax;
        }
    } else if (ch == ':') {
        if (c->expect_colon) {
            c->expect_colon = 0;
            return rdb_next_ok;
        } else {
            return rdb_next_syntax;
        }
    } else if (ch ==  '"') {
        if (is_val_expected) {
            set_st_(c, st_str_);
            return rdb_next_ok;
        } else {
            return rdb_next_syntax;
        }
    } else if (ch == '<') {
        if (is_val_expected) {
            set_st_(c, st_long_str_);
            return rdb_next_ok;
        } else {
            return rdb_next_syntax;
        }
    } else if (ch == ',') {
        c->is_in_arr = 1;
        return rdb_next_ok;
    } else if (ch == '\n') {
        c->is_in_arr = 0;
        c->got_key = 0;
        c->got_first_val = 0;
        return rdb_next_ok;
    } else if (ch == '\0') {
        c->is_in_arr = 0;
        c->got_key = 0;
        c->got_first_val = 0;
        return rdb_next_fin;
    } else if (ch == ' '|| ch == '\t') {
        return rdb_next_ok;
    } else {
        if (! c->got_key) {
            set_st_(c, st_key_);
            return next_key_(c, ch);
        } else if (is_val_expected) {
            set_st_(c, st_bool_);
            return next_bool_(c, ch);
        } else {
            return rdb_next_syntax;
        }
    }
}

static enum rdb_next_res next_key_(struct rdb_ctx * c, char ch)
{
    struct sd_key_ * sd = &c->sd.key;
    SOB_ASSERT_ST_(c, st_key_);

    if (ch == ':' || ch == ' ' || ch == '\t') {
        if (sd->len > 0) {
            if (sd->len < c->str_mlen) {
                c->str_out[sd->len] = '\0';
                sd->len++;
            } else {
                return rdb_next_syntax;
            }
            c->ty = rdb_key;
            set_st_(c, st_idle_);
            c->expect_colon = (ch != ':');
            c->got_key = 1;
            c->got_first_val = 0;
            c->is_in_arr = 0;
            return rdb_next_ok;
        } else {
            return rdb_next_syntax;
        }
    }

    if (! ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                || (ch >= '0' && ch <= '9')
                || ch == '.' || ch == '_' || ch == '-')) {
        return rdb_next_syntax;
    }

    if (sd->len < c->str_mlen) {
        c->str_out[sd->len] = ch;
        sd->len++;
        return rdb_next_ok;
    } else {
        return rdb_next_syntax;
    }
}

static enum rdb_next_res next_str_(struct rdb_ctx * c, char ch)
{
    struct sd_str_ * sd = &c->sd.str;
    SOB_ASSERT_ST_(c, st_str_);

    if ((ch <= 31 || ch >= 127) && ch != '\t') { /* control character */
        return rdb_next_syntax;
    }

    if (! sd->is_escape) {
        if (ch == '"') {
            if (sd->len < c->str_mlen) {
                c->str_out[sd->len] = '\0';
                sd->len++;
            } else {
                return rdb_next_syntax;
            }
            c->ty = rdb_str;
            set_st_(c, st_idle_);
            c->got_key = (ch != '\n');
            c->got_first_val = 1;
            return rdb_next_ok;
        } else if (ch == '\\') {
            sd->is_escape = 1;
            return rdb_next_ok;
        }
    } else {
        ch = escape_ch_(ch);
        sd->is_escape = 0;
    }

    if (sd->len < c->str_mlen) {
        c->str_out[sd->len] = ch;
        sd->len++;
        return rdb_next_ok;
    } else {
        return rdb_next_syntax;
    }
}

static enum rdb_next_res next_long_str_(struct rdb_ctx * c, char ch)
{
    struct sd_long_str_ * sd = &c->sd.long_str;
    SOB_ASSERT_ST_(c, st_long_str_);

    if ((ch <= 31 || ch >= 127) && ch != '\t' && ch != '\n') {
        /* control character */
        return rdb_next_syntax;
    }

    if (! sd->is_escape) {
        if (ch == '>') {
            if (sd->len > 0
                    && c->str_out[sd->len - 1] == '\n'
                    && ! sd->keep_last_newline) {
                sd->len -= 1; /* ignore newline before '>' */
            }
            if (sd->len < c->str_mlen) {
                c->str_out[sd->len] = '\0';
                sd->len++;
            } else {
                return rdb_next_syntax;
            }
            c->ty = rdb_str;
            set_st_(c, st_idle_);
            c->got_key = (ch != '\n');
            c->got_first_val = 1;
            return rdb_next_ok;
        } else if (ch == '\\') {
            sd->is_escape = 1;
            sd->skip_whitespace = 0;
            return rdb_next_ok;
        } else if (ch == ' ' || ch == '\t' || ch == '\n') {
            if (sd->skip_whitespace) {
                if (ch == '\n') {
                    sd->skip_whitespace = 0;
                }
                sd->keep_last_newline = 1;
                return rdb_next_ok;
            }
        }
        sd->keep_last_newline = 0;
    } else {
        ch = escape_ch_(ch);
        sd->is_escape = 0;
        /* keep manually inserted newline characters */
        sd->keep_last_newline = (ch == '\n');
    }

    if (sd->len < c->str_mlen) {
        c->str_out[sd->len] = ch;
        sd->len++;
        sd->skip_whitespace = 0;
        return rdb_next_ok;
    } else {
        return rdb_next_syntax;
    }
}

static enum rdb_next_res next_num_(struct rdb_ctx * c, char ch)
{
    SOB_ASSERT_ST_(c, st_num_);
    switch (c->sd.num.part) {
    case num_part_int_:
        return next_num_int_(c, ch);
    case num_part_frac_:
        return next_num_frac_(c, ch);
    case num_part_exp_:
        return next_num_exp_(c, ch);
    };
    SOB_PANIC("unreacheable");
    return rdb_next_syntax;
}

static enum rdb_next_res next_bool_(struct rdb_ctx * c, char ch)
{
    struct sd_bool_ * sd = &c->sd.boolean;
    SOB_ASSERT_ST_(c, st_bool_);

    if (is_separator_(ch)) {
        if (strncmp(sd->word, "true", sd->wlen) == 0
                || strncmp(sd->word, "false", sd->wlen) == 0) {
            c->ty = rdb_bool;
            set_st_(c, st_idle_);
            c->is_in_arr = (ch == ',');
            c->got_key = (ch != '\n');
            c->got_first_val = 1;
            if (ch == '\0') {
                return rdb_next_fin;
            } else {
                return rdb_next_ok;
            }
        } else {
            return rdb_next_syntax;
        }
    } else {
        if (sd->wlen < sizeof(sd->word)) {
            sd->word[sd->wlen] = ch;
            sd->wlen++;
            return rdb_next_ok;
        } else {
            return rdb_next_syntax;
        }
    }
}

static enum rdb_next_res next_num_int_(struct rdb_ctx * c, char ch)
{
    struct sd_num_ * sd = &c->sd.num;
    if (ch >= '0' && ch <= '9') {
        sd->got_int_explicit = 1;
        if (sd->pos == 0 && ch == '0') { /* skip leading zeroes */
            return rdb_next_ok;
        } else {
            sd->num = sd->num * 10 + (ch - '0');
            sd->pos++;
            return rdb_next_ok;
        }
    } else if (ch == '-' || ch == '+') {
        if (sd->pos == 0 && ! sd->got_sign) {
            sd->is_negative = (ch == '-');
            sd->got_sign = 1;
            return rdb_next_ok;
        } else {
            return rdb_next_syntax;
        }
    } else if (ch == '.') {
        if (sd->got_int_explicit || ! sd->got_sign) { /* make '-.1' invalid */
            sd->pos = 1; /* 1 because we multiply by pow(10, -pos) */
            sd->part = num_part_frac_;
            return rdb_next_ok;
        } else {
            return rdb_next_syntax;
        }
    } else if (ch == 'e' || ch == 'E') {
        if (sd->pos > 0 || ! sd->got_sign) { /* make '-e1' invalid */
            sd->pos = 0;
            sd->exp = 0;
            sd->exp_is_negative = 0;
            sd->got_sign = 0;
            sd->got_int_explicit = 0;
            sd->part = num_part_exp_;
            return rdb_next_ok;
        } else {
            return rdb_next_syntax;
        }
    } else if (is_separator_(ch)) {
        if (sd->pos > 0 || ! sd->got_sign) { /* make '-\n' invalid */
            c->ty = rdb_num;
            set_st_(c, st_idle_);
            c->is_in_arr = (ch == ',');
            c->got_key = (ch != '\n');
            c->got_first_val = 1;
            if (ch == '\0') {
                return rdb_next_fin;
            } else {
                return rdb_next_ok;
            }
        } else {
            return rdb_next_syntax;
        }
    } else {
        return rdb_next_syntax;
    }
}

static enum rdb_next_res next_num_frac_(struct rdb_ctx * c, char ch)
{
    struct sd_num_ * sd = &c->sd.num;
    if (ch >= '0' && ch <= '9') {
        sd->num += (ch - '0') * pow(10, -(sd->pos));
        sd->pos++;
        return rdb_next_ok;
    } else if (ch == 'e' || ch == 'E') {
        if (sd->pos >= 2) { /* make '.e1' invalid; '.1e1' is ok though */
            sd->pos = 0;
            sd->exp = 0;
            sd->exp_is_negative = 0;
            sd->got_sign = 0;
            sd->got_int_explicit = 0;
            sd->part = num_part_exp_;
            return rdb_next_ok;
        } else {
            return rdb_next_syntax;
        }
    } else if (is_separator_(ch)) {
        if (sd->pos >= 2 || sd->got_int_explicit) { /* make '.\n' invalid */
            c->ty = rdb_num;
            set_st_(c, st_idle_);
            c->is_in_arr = (ch == ',');
            c->got_key = (ch != '\n');
            c->got_first_val = 1;
            if (ch == '\0') {
                return rdb_next_fin;
            } else {
                return rdb_next_ok;
            }
        } else {
            return rdb_next_syntax;
        }
    } else {
        return rdb_next_syntax;
    }
}

static enum rdb_next_res next_num_exp_(struct rdb_ctx * c, char ch)
{
    struct sd_num_ * sd = &c->sd.num;
    if (ch >= '0' && ch <= '9') {
        sd->got_int_explicit = 1;
        if (sd->pos == 0 && ch == '0') { /* skip leading zeroes */
            return rdb_next_ok;
        } else {
            sd->exp = sd->exp * 10 + (ch - '0');
            sd->pos++;
            return rdb_next_ok;
        }
    } else if (ch == '-' || ch == '+') {
        if (sd->pos == 0 && ! sd->got_sign) {
            sd->exp_is_negative = (ch == '-');
            sd->got_sign = 1;
            return rdb_next_ok;
        } else {
            return rdb_next_syntax;
        }
    } else if (is_separator_(ch)) {
        if (sd->pos > 0) { /* make '1e\n' invalid */
            c->ty = rdb_num;
            set_st_(c, st_idle_);
            c->is_in_arr = (ch == ',');
            c->got_key = (ch != '\n');
            c->got_first_val = 1;
            if (ch == '\0') {
                return rdb_next_fin;
            } else {
                return rdb_next_ok;
            }
        } else {
            return rdb_next_syntax;
        }
    } else {
        return rdb_next_syntax;
    }
}

static void set_st_(struct rdb_ctx * c, enum st_ st)
{
    c->st = st;
    switch (c->st) {
    case st_idle_:
        c->expect_colon = 0;
        break;
    case st_key_:
        c->is_in_arr = 0;
        c->got_key = 0;
        c->expect_colon = 0;
        c->got_first_val = 0;
        c->sd.key.len = 0;
        c->str_out[0] = '\0';
        break;
    case st_str_:
        c->got_first_val = 1;
        c->sd.str.len = 0;
        c->sd.str.is_escape = 0;
        c->str_out[0] = '\0';
        break;
    case st_long_str_:
        c->got_first_val = 1;
        c->sd.long_str.len = 0;
        c->sd.long_str.is_escape = 0;
        c->sd.long_str.skip_whitespace = 1;
        c->sd.long_str.keep_last_newline = 0;
        c->str_out[0] = '\0';
        break;
    case st_num_:
        c->got_first_val = 1;
        c->sd.num.is_negative = 0;
        c->sd.num.got_sign = 0;
        c->sd.num.got_int_explicit = 0;
        c->sd.num.num = 0.0;
        c->sd.num.exp = 0;
        c->sd.num.exp_is_negative = 0;
        c->sd.num.pos = 0;
        c->sd.num.part = num_part_int_;
        break;
    case st_bool_:
        c->got_first_val = 1;
        c->sd.boolean.word[0] = '\0';
        c->sd.boolean.wlen = 0;
        break;
    };
}

static char escape_ch_(char ch)
{
    switch (ch) {
    case 'n':
        return '\n';
    case 'r':
        return '\r';
    case 't':
        return '\t';
    default:
        return ch;
    };
}

static int is_separator_(char ch)
{
    return ch == ' ' || ch == '\t' || ch == ',' || ch == '\n' || ch == '\0';
}

#undef SOB_ASSERT_ST_

#ifdef SOB_RDB_DEMO

#include <stdio.h>
#include <string.h>

enum {
    str_mlen_ = 128
};

int main(void) {
    size_t i;
    const char str[] = "id: 228337\n"
        "key  : \" hello:\tworld!\\n\"   \n"
        "multiline: <blah\n\n"
        "    blah\t\"blah\"\n"
        "\\<escaped\\>\n"
        ">\n"
        "multilines : <\n"
        "- foo\n"
        " - bar\n"
        "  - baz>, <second one, two\n"
        ">, <third one\n"
        "third two>\n"
        "list_thing\t: \"first\", \"second\"  ,\",commas,\",\n"
        "numbers:3.14, 314e-3,-0.420e3,1, 210 , -12 \n"
        "bools: true,false\n";
    struct rdb_ctx c;
    char str_buf[str_mlen_];

    printf("input: '%s'\n", str);

    rdb_init(&c, str_buf, str_mlen_);

    for (i = 0; i < strlen(str) + 1; i++) {
        int is_finish = 0;
        enum rdb_ty ty;
        enum rdb_next_res r = rdb_next(&c, str[i]);
        switch (r) {
        case rdb_next_ok:
            break;
        case rdb_next_fin:
            is_finish = 1;
            break;
        case rdb_next_syntax:
            printf("err @ %lu ('%c')\n", i, str[i]);
            return 1;
        };
        ty = rdb_cur_ty(&c);
        switch (ty) {
        case rdb_key:
            printf("\nkey: '%s'\n", rdb_cur_str(&c));
            break;
        case rdb_str:
            printf("str: '%s'\n", rdb_cur_str(&c));
            break;
        case rdb_num:
            printf("num: %f\n", rdb_cur_num(&c));
            break;
        case rdb_bool:
            printf("bool: '%s'\n", rdb_cur_is_true(&c) ? "true" : "false");
            break;
        case rdb_incomplete:
            break;
        };
        if (is_finish) {
            printf("fin\n");
            return 0;
        }
    }

    SOB_PANIC("no fin");
    return 1;
}

#endif /* SOB_RBD_DEMO */

