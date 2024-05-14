#include "rjson.h"

#include "panic.h"

#include <stddef.h> /* for size_t */
#include <string.h> /* for strchr */
#include <math.h> /* for pow */
#include <stdio.h> /* for fprintf */

enum {
    max_depth_ = 24
};

enum level_ty_ {
    lvl_none_ = 0,
    lvl_obj_,
    lvl_arr_
};

enum st_ {
    st_idle_,
    st_want_key_,
    st_want_colon_,
    st_str_,
    st_num_,
    st_true_,
    st_false_,
    st_null_
};

static void set_st_(struct rjson_ctx * c, enum st_ st);

static void add_lvl_(struct rjson_ctx * c, enum level_ty_ ty);
static void pop_lvl_(struct rjson_ctx * c);
static enum level_ty_ lvl_(const struct rjson_ctx * c);
static void assert_lvl_(const struct rjson_ctx * c, enum level_ty_ expected);
static void assert_st_(const struct rjson_ctx * c, enum st_ expected);
static void assert_is_val_expected_(const struct rjson_ctx * c, int expected);

static int is_whitespace_(char ch);
static int is_num_start_(char ch);

static enum rjson_next_res next_idle_(struct rjson_ctx * c, char ch);
static enum rjson_next_res next_want_key_(struct rjson_ctx * c, char ch);
static enum rjson_next_res next_want_colon_(struct rjson_ctx * c, char ch);
static enum rjson_next_res next_str_(struct rjson_ctx * c, char ch);
static enum rjson_next_res next_num_(struct rjson_ctx * c, char ch);
static enum rjson_next_res next_true_(struct rjson_ctx * c, char ch);
static enum rjson_next_res next_false_(struct rjson_ctx * c, char ch);
static enum rjson_next_res next_null_(struct rjson_ctx * c, char ch);

struct rjson_ctx {
    char * str;
    size_t str_mlen;

    enum level_ty_ lvls[max_depth_];
    size_t lvls_len;

    union {
        struct {
            size_t len;
            int is_escape;
            int is_key;
        } str;

        struct {
            double num;
            int digit_pos;
            int is_neg;
            int exp;
            enum num_part_ty_ {
                num_part_leading_zero_,
                num_part_int_,
                num_part_frac_,
                num_part_exp_
            } part;
        } num;

        struct {
            int word_pos;
            int bool_is_true;
        } special; /* for true, false, null */
    } sd;
    enum st_ st;

    int buffered_ch; /* -1 means nothing buffered */

    enum rjson_ty cur;

    int is_val_expected;
    size_t pos;
};

void rjson_init(struct rjson_ctx * c, char * str_out_buf, size_t str_mlen)
{
    c->str = str_out_buf;
    c->str_mlen = str_mlen;
    c->lvls_len = 0;
    c->buffered_ch = -1;
    c->cur = rjson_incomplete;
    set_st_(c, st_idle_);
    c->is_val_expected = 1; /* otherwise toplevel obj will be return an erorr */
    c->pos = 0;
}

enum rjson_next_res rjson_next(struct rjson_ctx * c, char ch)
{
    int processed_buffered = 0;
    enum rjson_next_res r;

    if (c->buffered_ch != -1) { /* one behind after num ended */
        char prev_ch = ch;
        ch = c->buffered_ch;
        c->buffered_ch = (int) prev_ch;
        processed_buffered = 1;
    }

    if (ch == '\0') {
        /* see also simmilar check with buffered_ch at the bottom */
        if (lvl_(c) == lvl_none_) {
            return rjson_next_fin;
        } else {
            return rjson_next_syntax;
        }
    }

    switch (c->st) {
    case st_idle_:
        r = next_idle_(c, ch);
        break;
    case st_want_key_:
        r = next_want_key_(c, ch);
        break;
    case st_want_colon_:
        r = next_want_colon_(c, ch);
        break;
    case st_str_:
        r = next_str_(c, ch);
        break;
    case st_num_:
        r = next_num_(c, ch);
        break;
    case st_true_:
        r = next_true_(c, ch);
        break;
    case st_false_:
        r = next_false_(c, ch);
        break;
    case st_null_:
        r = next_null_(c, ch);
        break;
    };

    /* XXX: this whole thing with buffered_ch feels like a hack */
    if (processed_buffered || c->buffered_ch == -1) {
        /* removed one from buffer or not added this char to buffer */
        if (r != rjson_next_syntax) { /* to report pos of the error cause */
            c->pos++;
        }
    }
    if (c->buffered_ch != -1) {
        if (r == rjson_next_ok && c->cur == rjson_incomplete) {
            /* nothing to report so process buffered to not fall behind more */
            char last_buffered_ch = (char) c->buffered_ch; /* cannot be -1 */
            c->buffered_ch = -1;
            return rjson_next(c, last_buffered_ch);
        } else if (r == rjson_next_ok && c->buffered_ch == '\0') {
            /* like {"a": [42]\0 - arr_end will be emited at '\0' so we 
             * will have no other chance to report missing '}' */
            /* see also similar check with ch at the top */
            if (lvl_(c) != lvl_none_) {
                /* discard whatever c->cur we had */
                return rjson_next_syntax;
            }
        }
    }

    return r;
}

size_t rjson_pos(const struct rjson_ctx * c)
{
    return c->pos;
}

enum rjson_ty rjson_cur_ty(const struct rjson_ctx * c)
{
    return c->cur;
}
const char * rjson_cur_str(const struct rjson_ctx * c)
{
    return c->str;
}
double rjson_cur_num(const struct rjson_ctx * c)
{
    return c->sd.num.num;
}
int rjson_cur_is_true(const struct rjson_ctx * c)
{
    return c->sd.special.bool_is_true;
}

static void set_st_(struct rjson_ctx * c, enum st_ st)
{
    if (c->st != st) {
        switch (st) {
        case st_str_:
            c->sd.str.len = 0;
            c->sd.str.is_escape = 0;
            c->sd.str.is_key = 0;
            break;
        case st_num_:
            c->sd.num.num = 0.0;
            c->sd.num.digit_pos = 0;
            c->sd.num.is_neg = 0;
            c->sd.num.exp = 0;
            c->sd.num.part = num_part_int_;
            break;
        case st_true_:
            c->sd.special.bool_is_true = 1;
            c->sd.special.word_pos = 0;
            break;
        case st_false_:
            c->sd.special.bool_is_true = 0;
            c->sd.special.word_pos = 0;
            break;
        case st_null_:
            c->sd.special.word_pos = 0;
            break;
        case st_idle_:
        case st_want_key_:
        case st_want_colon_:
            break;
        };
    }
    c->st = st;
}

static void add_lvl_(struct rjson_ctx * c, enum level_ty_ lvl)
{
    if (c->lvls_len == max_depth_) {
        SOB_PANIC("add_lvl_ in rjson: max depth reached");
    }
    c->lvls[c->lvls_len] = lvl;
    c->lvls_len++;
}

static void pop_lvl_(struct rjson_ctx * c)
{
    if (c->lvls_len == 0) { /* cannot happen during normal operation */
        SOB_PANIC("pop_lvl_: no levels to pop");
    }
    c->lvls_len--;
}

static enum level_ty_ lvl_(const struct rjson_ctx * c)
{
    if (c->lvls_len == 0) {
        return lvl_none_;
    }
    return c->lvls[c->lvls_len - 1];
}

static void assert_lvl_(const struct rjson_ctx * c, enum level_ty_ expected)
{
    if (lvl_(c) != expected) {
        SOB_PANIC("assert_lvl_: expected %i but got %i", lvl_(c), expected);
    }
}

static void assert_st_(const struct rjson_ctx * c, enum st_ expected)
{
    if (c->st != expected) {
        SOB_PANIC("assert_st_: expected %i but got %i", c->st, expected);
    }
}

static void assert_is_val_expected_(const struct rjson_ctx * c, int expected)
{
    if (c->is_val_expected != expected) {
        SOB_PANIC("assert_is_val_expected_: %i != %i in state %i",
                c->is_val_expected, expected, c->st);
    }
}

static int is_whitespace_(char ch)
{
    return ch == ' '
        || ch == '\n'
        || ch == '\r'
        || ch == '\t';
}

static int is_num_start_(char ch)
{
    return (ch >= '0' && ch <= '9')
        || ch == '-';
}

static enum rjson_next_res next_idle_(struct rjson_ctx * c, char ch)
{
    assert_st_(c, st_idle_);

    c->cur = rjson_incomplete;

    if (is_whitespace_(ch)) {
        return rjson_next_ok;
    }

    if (lvl_(c) == lvl_none_ && ch != '{') {
        return rjson_next_syntax;
    }

    if (c->is_val_expected) {
        if (strchr("true", ch)) {
            set_st_(c, st_true_);
            return next_true_(c, ch);
        } else if (strchr("false", ch)) {
            set_st_(c, st_false_);
            return next_false_(c, ch);
        } else if (strchr("null", ch)) {
            set_st_(c, st_null_);
            return next_null_(c, ch);
        } else if (is_num_start_(ch)) {
            set_st_(c, st_num_);
            return next_num_(c, ch);
        }
    }

    switch (ch) {
    case '{':
        if (c->is_val_expected) {
            c->is_val_expected = 1;
            add_lvl_(c, lvl_obj_);
            set_st_(c, st_want_key_);
            c->cur = rjson_obj_start;
            return rjson_next_ok;
        } else {
            return rjson_next_syntax;
        }
    case '[':
        if (c->is_val_expected) {
            add_lvl_(c, lvl_arr_);
            set_st_(c, st_idle_);
            c->cur = rjson_arr_start;
            c->is_val_expected = 1;
            return rjson_next_ok;
        } else {
            return rjson_next_syntax;
        }
    case '}':
        if (lvl_(c) == lvl_obj_) {
            if (! c->is_val_expected) {
                pop_lvl_(c);
                set_st_(c, st_idle_);
                c->cur = rjson_obj_end;
                if (lvl_(c) == lvl_none_) {
                    return rjson_next_fin;
                } else {
                    return rjson_next_ok;
                }
            } else {
                return rjson_next_syntax;
            }
        } else {
            return rjson_next_syntax;
        }
    case ']':
        if (lvl_(c) == lvl_arr_) {
            if (! c->is_val_expected) {
                pop_lvl_(c);
                set_st_(c, st_idle_);
                c->cur = rjson_arr_end;
                return rjson_next_ok;
            } else {
                return rjson_next_syntax;
            }
        } else {
            return rjson_next_syntax;
        }
    case '"':
        if (c->is_val_expected) {
            set_st_(c, st_str_);
            c->cur = rjson_incomplete;
            c->is_val_expected = 0;
            return rjson_next_ok;
        } else {
            return rjson_next_syntax;
        }
    case ',':
        if (! c->is_val_expected) {
            c->is_val_expected = 1;
            if (lvl_(c) == lvl_obj_) {
                set_st_(c, st_want_key_);
                c->cur = rjson_incomplete;
                return rjson_next_ok;
            } else if (lvl_(c) == lvl_arr_) {
                /* no action required */
                c->cur = rjson_incomplete;
                return rjson_next_ok;
            } else {
                return rjson_next_syntax;
            }
        } else {
            return rjson_next_syntax;
        }
    case ':':
    default:
        return rjson_next_syntax;
    };
}

enum rjson_next_res next_want_key_(struct rjson_ctx * c, char ch)
{
    assert_st_(c, st_want_key_);
    assert_lvl_(c, lvl_obj_);
    assert_is_val_expected_(c, 1);

    if (is_whitespace_(ch)) {
        return rjson_next_ok;
    }

    switch (ch) {
    case '"':
        set_st_(c, st_str_); /* resets str.is_key */
        c->sd.str.is_key = 1;
        c->cur = rjson_incomplete;
        return rjson_next_ok;
    case '}': /* empty obj */
        set_st_(c, st_idle_);
        pop_lvl_(c);
        c->cur = rjson_obj_end;
        c->is_val_expected = 0;
        if (lvl_(c) == lvl_none_) {
            return rjson_next_fin;
        } else {
            return rjson_next_ok;
        }
    case '{':
    case '[':
    case ']':
    case ':':
    case ',':
    default:
        return rjson_next_syntax;
    };
}

enum rjson_next_res next_want_colon_(struct rjson_ctx * c, char ch)
{
    assert_st_(c, st_want_colon_);
    assert_lvl_(c, lvl_obj_);
    assert_is_val_expected_(c, 0);

    if (is_whitespace_(ch)) {
        return rjson_next_ok;
    }

    switch (ch) {
    case ':':
        set_st_(c, st_idle_);
        c->cur = rjson_incomplete;
        c->is_val_expected = 1;
        return rjson_next_ok;
    case '{':
    case '[':
    case '}':
    case ']':
    case ',':
    case '"':
    default:
        return rjson_next_syntax;
    };
}

static enum add_str_ch_res_ {
    add_ch_overflow_ = -1,
    add_ch_ok_ = 1
} add_str_ch_(struct rjson_ctx * c, char ch);

static enum str_escape_res_ {
    str_escape_invalid_ = -1,
    str_escaped_ = 1,
    str_escape_utf16_ = 2
} str_escape_(char * ch);

static enum rjson_next_res next_str_(struct rjson_ctx * c, char ch)
{
    assert_st_(c, st_str_);
    if (c->sd.str.is_key) {
        assert_lvl_(c, lvl_obj_);
    }

    c->cur = rjson_incomplete;

    if (ch <= 31 || ch >= 127) { /* control character */
        return rjson_next_syntax;
    }

    if (c->sd.str.is_escape) {
        enum str_escape_res_ r = str_escape_(&ch);
        switch (r) {
        case str_escaped_:
            c->sd.str.is_escape = 0;
            break; /* ch has escaped value */
        case str_escape_utf16_:
            return rjson_next_syntax; /* XXX: not implemented */
        case str_escape_invalid_:
            return rjson_next_syntax;
        };
    } else if (ch == '\\') {
        c->sd.str.is_escape = 1;
        return rjson_next_ok;
    } else if (ch == '"') { /* end of string */
        ch = '\0';
        c->cur = rjson_str;
        c->is_val_expected = 0;
        if (c->sd.str.is_key) {
            set_st_(c, st_want_colon_);
        } else {
            set_st_(c, st_idle_);
        }
    }

    {
        enum add_str_ch_res_ r = add_str_ch_(c, ch);
        switch (r) {
        case add_ch_ok_:
            return rjson_next_ok;
        case add_ch_overflow_:
            /* XXX: should not print */
            fprintf(stderr,
                "WARNING @ %s:%i : string buffer overflow (str_mlen = %lu)\n",
                __FILE__, __LINE__, c->str_mlen);
            return rjson_next_syntax;
        };
    }
    SOB_PANIC("unreachable");
    return rjson_next_syntax;
}

static enum rjson_next_res next_num_(struct rjson_ctx * c, char ch)
{
    assert_st_(c, st_num_);
    assert_is_val_expected_(c, 1);

    if (is_whitespace_(ch) || ch == '}' || ch == ']' || ch == ',') {
        if (c->sd.num.digit_pos > 0) {
            if (c->sd.num.part == num_part_exp_) {
                c->sd.num.num *= pow(10.0, c->sd.num.exp);
            }
            c->cur = rjson_num;
            if (c->buffered_ch != -1) {
                SOB_PANIC("buffered_ch should've been consumed before num");
            }
            c->buffered_ch = ch;
            c->is_val_expected = 0;
            set_st_(c, st_idle_);
            return rjson_next_ok;
        } else {
            return rjson_next_syntax;
        }
    }

    if (c->sd.num.part == num_part_int_ && ch == '0' && c->sd.num.digit_pos == 0) {
        c->sd.num.part = num_part_leading_zero_;
        return rjson_next_ok;
    }

    switch (c->sd.num.part) {
    case num_part_leading_zero_:
        if (ch == '.') {
            c->sd.num.part = num_part_frac_;
            return rjson_next_ok;
        } else {
            return rjson_next_syntax;
        }
        break;
    case num_part_int_:
        if (ch == '-') {
            if (c->sd.num.digit_pos == 0) {
                c->sd.num.is_neg = 1;
                return rjson_next_ok;
            } else {
                return rjson_next_syntax;
            }
        } else if (ch == '.') {
            if (c->sd.num.digit_pos > 0) {
                c->sd.num.digit_pos = 0;
                c->sd.num.part = num_part_frac_;
                return rjson_next_ok;
            } else {
                return rjson_next_syntax;
            }
        } else if (ch == 'e' || ch == 'E') {
            if (c->sd.num.digit_pos > 0) {
                c->sd.num.digit_pos = 0;
                c->sd.num.part = num_part_exp_;
                return rjson_next_ok;
            } else {
                return rjson_next_syntax;
            }
        } else if (ch >= '0' && ch <= '9') {
            c->sd.num.num *= 10;
            c->sd.num.num += (ch - '0') * (c->sd.num.is_neg ? -1 : 1);
            c->sd.num.digit_pos++;
            return rjson_next_ok;
        } else {
            return rjson_next_syntax;
        }
        break;
    case num_part_frac_:
        if (ch == 'e' || ch == 'E') {
            if (c->sd.num.digit_pos > 0) {
                c->sd.num.digit_pos = 0;
                c->sd.num.is_neg = 0;
                c->sd.num.exp = 0;
                c->sd.num.part = num_part_exp_;
                return rjson_next_ok;
            } else {
                return rjson_next_syntax;
            }
        } else if (ch >= '0' && ch <= '9') {
            c->sd.num.num += (ch - '0')
                * pow(10.0, -(c->sd.num.digit_pos + 1))
                * (c->sd.num.is_neg ? -1 : 1);
            c->sd.num.digit_pos++;
            return rjson_next_ok;
        } else {
            return rjson_next_syntax;
        }
        break;
    case num_part_exp_:
        if (ch == '+') {
            if (c->sd.num.digit_pos == 0) {
                c->sd.num.is_neg = 0;
                return rjson_next_ok;
            } else {
                return rjson_next_syntax;
            }
        } else if (ch == '-') {
            if (c->sd.num.digit_pos == 0) {
                c->sd.num.is_neg = 1;
                return rjson_next_ok;
            } else {
                return rjson_next_syntax;
            }
        } else if (ch >= '0' && ch <= '9') {
            c->sd.num.exp *= 10;
            c->sd.num.exp += (ch - '0') * (c->sd.num.is_neg ? -1 : 1);
            c->sd.num.digit_pos++;
            return rjson_next_ok;
        } else {
            return rjson_next_syntax;
        }
        break;
    };

    return rjson_next_ok;
}

static enum rjson_next_res next_bool_(struct rjson_ctx * c, char ch,
        const char * word);

static enum rjson_next_res next_true_(struct rjson_ctx * c, char ch)
{
    assert_st_(c, st_true_);
    return next_bool_(c, ch, "true");
}

static enum rjson_next_res next_false_(struct rjson_ctx * c, char ch)
{
    assert_st_(c, st_false_);
    return next_bool_(c, ch, "false");
}

static enum rjson_next_res next_null_(struct rjson_ctx * c, char ch)
{
    assert_st_(c, st_null_);
    assert_is_val_expected_(c, 1);
    const char * word = "null";

    if (ch != word[c->sd.special.word_pos]) {
        return rjson_next_syntax;
    }
    if (c->sd.special.word_pos == strlen(word) - 1) {
        c->cur = rjson_null;
        c->is_val_expected = 0;
        set_st_(c, st_idle_);
    } else {
        c->sd.special.word_pos++;
    }

    return rjson_next_ok;
}

static enum rjson_next_res next_bool_(struct rjson_ctx * c, char ch,
    const char * word)
{
    assert_is_val_expected_(c, 1);

    if (ch != word[c->sd.special.word_pos]) {
        return rjson_next_syntax;
    }
    if (c->sd.special.word_pos == strlen(word) - 1) {
        c->cur = rjson_bool;
        c->is_val_expected = 0;
        set_st_(c, st_idle_);
    } else {
        c->sd.special.word_pos++;
    }

    return rjson_next_ok;
}

static enum add_str_ch_res_ add_str_ch_(struct rjson_ctx * c, char ch)
{
    if (c->sd.str.len == c->str_mlen) {
        return add_ch_overflow_;
    } else {
        c->str[c->sd.str.len] = ch;
        c->sd.str.len++;
        return add_ch_ok_;
    }
}

static enum str_escape_res_ str_escape_(char * ch)
{
    switch (*ch) {
    case 'u':
        return str_escape_utf16_;
    case '\"':
        *ch = '\"';
        return str_escaped_;
    case '\\':
        *ch = '\\';
        return str_escaped_;
    case '/':
        *ch = '/';
        return str_escaped_;
    case 'b':
        *ch = '\b';
        return str_escaped_;
    case 'f':
        *ch = '\f';
        return str_escaped_;
    case 'n':
        *ch = '\n';
        return str_escaped_;
    case 'r':
        *ch = '\r';
        return str_escaped_;
    case 't':
        *ch = '\t';
        return str_escaped_;
    default:
        return str_escape_invalid_;
    };
}

#ifdef SOB_RJSON_DEMO

#include <stdio.h>

enum {
    str_buf_mlen_ = 120
};

int main(int argc, char ** argv)
{
    const char * str = "{\"hello\": \"world\",\n"
        "\t\"a\": {\"b\": true, \"c\": [[-228, 123], 0.314e1, 420228e-03]}}";

    int i;
    char str_buf[str_buf_mlen_];
    struct rjson_ctx c;

    if (argc > 1) {
        str = argv[1];
    }

    rjson_init(&c, str_buf, str_buf_mlen_);

    printf("str: '%s'\n", str);

    for (i = 0; i < strlen(str) + 1; i++) { /* want to include the '\0' */
        enum rjson_next_res nres = rjson_next(&c, str[i]);

        if (nres == rjson_next_syntax) {
            fprintf(stderr, "syntax error @ %lu ('%c')\n",
                rjson_pos(&c), str[rjson_pos(&c)]);
            return 1;
        } else if (nres == rjson_next_fin) {
            /* handled at the end of the block */
        } else { }

        switch (rjson_cur_ty(&c)) {
            case rjson_incomplete:
                break;
            case rjson_str:
                printf("str: '%s'\n", rjson_cur_str(&c));
                break;
            case rjson_num:
                printf("num: '%e'\n", rjson_cur_num(&c));
                break;
            case rjson_bool:
                printf("bool: '%s'\n",
                    rjson_cur_is_true(&c) ? "true" : "false");
                break;
            case rjson_null:
                printf("null\n");
                break;
            case rjson_obj_start:
                printf("obj {\n");
                break;
            case rjson_obj_end:
                printf("} obj\n");
                break;
            case rjson_arr_start:
                printf("arr [\n");
                break;
            case rjson_arr_end:
                printf("] arr\n");
                break;
        };

        if (nres == rjson_next_fin) {
            printf("fin\n");
            break;
        }
    }


    return 0;
}

#endif /* SOB_RJSON_DEMO */

