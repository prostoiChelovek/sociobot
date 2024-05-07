#include "rjson.h"

#include "panic.h"

#include <stddef.h> /* for size_t */
#include <string.h> /* for strchr */
#include <math.h> /* for pow */
#include <stdio.h> /* for fprintf */

enum {
    _max_depth = 24
};

enum _level_ty {
    _lvl_none = 0,
    _lvl_obj,
    _lvl_arr
};

enum _st {
    _st_idle,
    _st_want_key,
    _st_want_colon,
    _st_str,
    _st_num,
    _st_true,
    _st_false,
    _st_null
};

void _set_st(struct rjson_ctx * c, enum _st st);

void _add_lvl(struct rjson_ctx * c, enum _level_ty ty);
void _pop_lvl(struct rjson_ctx * c);
enum _level_ty _lvl(struct rjson_ctx * c);
void _assert_lvl(struct rjson_ctx * c, enum _level_ty expected);
void _assert_st(struct rjson_ctx * c, enum _st expected);

int _is_whitespace(char ch);
int _is_num_start(char ch);

enum rjson_next_res _next_idle(struct rjson_ctx * c, char ch);
enum rjson_next_res _next_want_key(struct rjson_ctx * c, char ch);
enum rjson_next_res _next_want_colon(struct rjson_ctx * c, char ch);
enum rjson_next_res _next_str(struct rjson_ctx * c, char ch);
enum rjson_next_res _next_num(struct rjson_ctx * c, char ch);
enum rjson_next_res _next_true(struct rjson_ctx * c, char ch);
enum rjson_next_res _next_false(struct rjson_ctx * c, char ch);
enum rjson_next_res _next_null(struct rjson_ctx * c, char ch);

struct rjson_ctx {
    char * str;
    size_t str_mlen;

    enum _level_ty lvls[_max_depth];
    size_t lvls_len;

    size_t str_len;
    int str_is_escape;
    int str_is_key;

    double num;
    int num_digit_pos;
    int num_is_neg;
    int num_exp;
    enum _num_part_ty {
        _num_part_leading_zero,
        _num_part_int,
        _num_part_frac,
        _num_part_exp
    } num_part;

    int special_word_pos; /* for true, false, null */
    int bool_is_true;

    char buffered_ch;

    enum rjson_ty cur;

    enum _st st;
    int is_val_expected;
    size_t pos;
};

void rjson_init(struct rjson_ctx * c, char * str_out_buf, size_t str_mlen) {
    c->str = str_out_buf;
    c->str_mlen = str_mlen;
    c->lvls_len = 0;
    c->str_len = 0;
    c->str_is_escape = 0;
    c->str_is_key = 0;
    c->num = 0.0;
    c->num_digit_pos = 0;
    c->num_is_neg = 0;
    c->num_exp = 0;
    c->num_part = _num_part_int;
    c->special_word_pos = 0;
    c->bool_is_true = 0;
    c->buffered_ch = '\0';
    c->cur = rjson_incomplete;
    _set_st(c, _st_idle);
    c->is_val_expected = 0;
    c->pos = 0;
}

enum rjson_next_res rjson_next(struct rjson_ctx * c, char ch) {
    enum rjson_next_res r;

    if (c->buffered_ch != '\0') {
        char prev_ch = ch;
        ch = c->buffered_ch;
        c->buffered_ch = prev_ch;
        c->pos++;
    }

    if (ch == '\0') {
        if (_lvl(c) == _lvl_none) {
            return rjson_next_fin;
        } else {
            return rjson_next_syntax;
        }
    }

    switch (c->st) {
    case _st_idle:
        r = _next_idle(c, ch);
        break;
    case _st_want_key:
        r = _next_want_key(c, ch);
        break;
    case _st_want_colon:
        r = _next_want_colon(c, ch);
        break;
    case _st_str:
        r = _next_str(c, ch);
        break;
    case _st_num:
        r = _next_num(c, ch);
        break;
    case _st_true:
        r = _next_true(c, ch);
        break;
    case _st_false:
        r = _next_false(c, ch);
        break;
    case _st_null:
        r = _next_null(c, ch);
        break;
    };

    if (c->buffered_ch == '\0') {
        c->pos++;
    } else if (r == rjson_next_ok && c->cur == rjson_incomplete) {
        c->pos++;
        char last_buffered_ch = c->buffered_ch;
        c->buffered_ch = '\0';
        return rjson_next(c, last_buffered_ch);
    }
    return r;
}

size_t rjson_pos(const struct rjson_ctx * c) {
    return c->pos;
}

enum rjson_ty rjson_cur_ty(const struct rjson_ctx * c) {
    return c->cur;
}
const char * rjson_cur_str(const struct rjson_ctx * c) {
    return c->str;
}
double rjson_cur_num(const struct rjson_ctx * c) {
    return c->num;
}
int rjson_cur_is_true(const struct rjson_ctx * c) {
    return c->bool_is_true;
}

void _set_st(struct rjson_ctx * c, enum _st st) {
    if (c->st != st) {
        switch (st) {
        case _st_str:
            c->str_len = 0;
            c->str_is_escape = 0;
            c->str_is_key = 0;
            break;
        case _st_num:
            c->num = 0.0;
            c->num_digit_pos = 0;
            c->num_is_neg = 0;
            c->num_exp = 0;
            c->num_part = _num_part_int;
            break;
        case _st_true:
            c->bool_is_true = 1;
            c->special_word_pos = 0;
            break;
        case _st_false:
            c->bool_is_true = 0;
            c->special_word_pos = 0;
            break;
        case _st_null:
            c->special_word_pos = 0;
            break;
        case _st_idle:
        case _st_want_key:
        case _st_want_colon:
            break;
        };
    }
    c->st = st;
}

void _add_lvl(struct rjson_ctx * c, enum _level_ty lvl) {
    if (c->lvls_len == _max_depth) {
        SOB_PANIC("_add_lvl in rjson: max depth reached");
    }
    c->lvls[c->lvls_len] = lvl;
    c->lvls_len++;
}

void _pop_lvl(struct rjson_ctx * c) {
    if (c->lvls_len == 0) { /* cannot happen during normal operation */
        SOB_PANIC("_pop_lvl: no levels to pop");
    }
    c->lvls_len--;
}

enum _level_ty _lvl(struct rjson_ctx * c) {
    if (c->lvls_len == 0) {
        return _lvl_none;
    }
    return c->lvls[c->lvls_len - 1];
}

void _assert_lvl(struct rjson_ctx * c, enum _level_ty expected) {
    if (_lvl(c) != expected) {
        SOB_PANIC("_assert_lvl: expected %i but got %i", _lvl(c), expected);
    }
}

void _assert_st(struct rjson_ctx * c, enum _st expected) {
    if (c->st != expected) {
        SOB_PANIC("_assert_st: expected %i but got %i", c->st, expected);
    }
}

int _is_whitespace(char ch) {
    return ch == ' '
        || ch == '\n'
        || ch == '\r'
        || ch == '\t';
}

int _is_num_start(char ch) {
    return (ch >= '0' && ch <= '9')
        || ch == '-';
}

enum rjson_next_res _next_idle(struct rjson_ctx * c, char ch) {
    _assert_st(c, _st_idle);

    c->cur = rjson_incomplete;

    if (_is_whitespace(ch)) {
        return rjson_next_ok;
    }

    if (_lvl(c) == _lvl_none && ch != '{') {
        return rjson_next_syntax;
    }

    if (strchr("true", ch)) {
        c->is_val_expected = 0;
        _set_st(c, _st_true);
        return _next_true(c, ch);
    } else if (strchr("false", ch)) {
        c->is_val_expected = 0;
        _set_st(c, _st_false);
        return _next_false(c, ch);
    } else if (strchr("null", ch)) {
        c->is_val_expected = 0;
        _set_st(c, _st_null);
        return _next_null(c, ch);
    } else if (_is_num_start(ch)) {
        c->is_val_expected = 0;
        _set_st(c, _st_num);
        return _next_num(c, ch);
    }

    switch (ch) {
    case '{':
        _add_lvl(c, _lvl_obj);
        _set_st(c, _st_want_key);
        c->cur = rjson_obj_start;
        c->is_val_expected = 0;
        return rjson_next_ok;
    case '[':
        _add_lvl(c, _lvl_arr);
        _set_st(c, _st_idle);
        c->cur = rjson_arr_start;
        c->is_val_expected = 0;
        return rjson_next_ok;
    case '}':
        if (_lvl(c) == _lvl_obj) {
            if (! c->is_val_expected) {
                _pop_lvl(c);
                _set_st(c, _st_idle);
                c->cur = rjson_obj_end;
                if (_lvl(c) == _lvl_none) {
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
        if (_lvl(c) == _lvl_arr) {
            if (! c->is_val_expected) {
                _pop_lvl(c);
                _set_st(c, _st_idle);
                c->cur = rjson_arr_end;
                return rjson_next_ok;
            } else {
                return rjson_next_syntax;
            }
        } else {
            return rjson_next_syntax;
        }
    case '"':
        _set_st(c, _st_str);
        c->cur = rjson_incomplete;
        c->is_val_expected = 0;
        return rjson_next_ok;
    case ',':
        if (! c->is_val_expected) {
            c->is_val_expected = 1;
            if (_lvl(c) == _lvl_obj) {
                _set_st(c, _st_want_key);
                c->cur = rjson_incomplete;
                return rjson_next_ok;
            } else if (_lvl(c) == _lvl_arr) {
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

enum rjson_next_res _next_want_key(struct rjson_ctx * c, char ch) {
    _assert_st(c, _st_want_key);
    _assert_lvl(c, _lvl_obj);

    if (_is_whitespace(ch)) {
        return rjson_next_ok;
    }

    switch (ch) {
    case '"':
        _set_st(c, _st_str); /* resets str_is_key */
        c->str_is_key = 1;
        c->cur = rjson_incomplete;
        return rjson_next_ok;
    case '}': /* empty obj */
        _set_st(c, _st_idle);
        _pop_lvl(c);
        c->cur = rjson_obj_end;
        return rjson_next_ok;
    case '{':
    case '[':
    case ']':
    case ':':
    case ',':
    default:
        return rjson_next_syntax;
    };
}

enum rjson_next_res _next_want_colon(struct rjson_ctx * c, char ch) {
    _assert_st(c, _st_want_colon);
    _assert_lvl(c, _lvl_obj);

    if (_is_whitespace(ch)) {
        return rjson_next_ok;
    }

    switch (ch) {
    case ':':
        _set_st(c, _st_idle);
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

enum _add_str_ch_res {
    _add_ch_overflow = -1,
    _add_ch_ok = 1
} _add_str_ch(struct rjson_ctx * c, char ch);

enum _str_escape_res {
    _str_escape_invalid = -1,
    _str_escaped = 1,
    _str_escape_utf16 = 2
} _str_escape(char * ch);

enum rjson_next_res _next_str(struct rjson_ctx * c, char ch) {
    _assert_st(c, _st_str);
    if (c->str_is_key) {
        _assert_lvl(c, _lvl_obj);
    }

    c->cur = rjson_incomplete;

    if (ch <= 31 || ch >= 127) { /* control character */
        return rjson_next_syntax;
    }

    if (c->str_is_escape) {
        enum _str_escape_res r = _str_escape(&ch);
        switch (r) {
        case _str_escaped:
            c->str_is_escape = 0;
            break; /* ch has escaped value */
        case _str_escape_utf16:
            return rjson_next_syntax; /* XXX: not implemented */
        case _str_escape_invalid:
            return rjson_next_syntax;
        };
    } else if (ch == '\\') {
        c->str_is_escape = 1;
        return rjson_next_ok;
    } else if (ch == '"') { /* end of string */
        ch = '\0';
        c->cur = rjson_str;
        if (c->str_is_key) {
            _set_st(c, _st_want_colon);
        } else {
            _set_st(c, _st_idle);
        }
    }

    {
        enum _add_str_ch_res r = _add_str_ch(c, ch);
        switch (r) {
        case _add_ch_ok:
            return rjson_next_ok;
        case _add_ch_overflow:
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

enum rjson_next_res _next_num(struct rjson_ctx * c, char ch) {
    _assert_st(c, _st_num);

    if (_is_whitespace(ch) || (strchr("}],", ch) && ch != '\0')) {
        if (c->num_digit_pos > 0) {
            if (c->num_part == _num_part_exp) {
                c->num *= pow(10.0, c->num_exp);
            }
            c->cur = rjson_num;
            if (c->buffered_ch != '\0') {
                SOB_PANIC("buffered_ch should've been consumed before num");
            }
            c->buffered_ch = ch;
            _set_st(c, _st_idle);
            return rjson_next_ok;
        } else {
            return rjson_next_syntax;
        }
    }

    if (c->num_part == _num_part_int && ch == '0' && c->num_digit_pos == 0) {
        c->num_part = _num_part_leading_zero;
        return rjson_next_ok;
    }

    switch (c->num_part) {
    case _num_part_leading_zero:
        if (ch == '.') {
            c->num_part = _num_part_frac;
            return rjson_next_ok;
        } else {
            return rjson_next_syntax;
        }
        break;
    case _num_part_int:
        if (ch == '-') {
            if (c->num_digit_pos == 0) {
                c->num_is_neg = 1;
                return rjson_next_ok;
            } else {
                return rjson_next_syntax;
            }
        } else if (ch == '.') {
            if (c->num_digit_pos > 0) {
                c->num_digit_pos = 0;
                c->num_part = _num_part_frac;
                return rjson_next_ok;
            } else {
                return rjson_next_syntax;
            }
        } else if (ch == 'e' || ch == 'E') {
            if (c->num_digit_pos > 0) {
                c->num_digit_pos = 0;
                c->num_part = _num_part_exp;
                return rjson_next_ok;
            } else {
                return rjson_next_syntax;
            }
        } else if (ch >= '0' && ch <= '9') {
            c->num *= 10;
            c->num += (ch - '0') * (c->num_is_neg ? -1 : 1);
            c->num_digit_pos++;
            return rjson_next_ok;
        } else {
            return rjson_next_syntax;
        }
        break;
    case _num_part_frac:
        if (ch == 'e' || ch == 'E') {
            if (c->num_digit_pos > 0) {
                c->num_digit_pos = 0;
                c->num_is_neg = 0;
                c->num_exp = 0;
                c->num_part = _num_part_exp;
                return rjson_next_ok;
            } else {
                return rjson_next_syntax;
            }
        } else if (ch >= '0' && ch <= '9') {
            c->num += (ch - '0')
                * pow(10.0, -(c->num_digit_pos + 1))
                * (c->num_is_neg ? -1 : 1);
            c->num_digit_pos++;
            return rjson_next_ok;
        } else {
            return rjson_next_syntax;
        }
        break;
    case _num_part_exp:
        if (ch == '+') {
            if (c->num_digit_pos == 0) {
                c->num_is_neg = 0;
                return rjson_next_ok;
            } else {
                return rjson_next_syntax;
            }
        } else if (ch == '-') {
            if (c->num_digit_pos == 0) {
                c->num_is_neg = 1;
                return rjson_next_ok;
            } else {
                return rjson_next_syntax;
            }
        } else if (ch >= '0' && ch <= '9') {
            c->num_exp *= 10;
            c->num_exp += (ch - '0') * (c->num_is_neg ? -1 : 1);
            c->num_digit_pos++;
            return rjson_next_ok;
        } else {
            return rjson_next_syntax;
        }
        break;
    };

    return rjson_next_ok;
}

enum rjson_next_res _next_bool(struct rjson_ctx * c, char ch,
        const char * word);

enum rjson_next_res _next_true(struct rjson_ctx * c, char ch) {
    _assert_st(c, _st_true);
    return _next_bool(c, ch, "true");
}

enum rjson_next_res _next_false(struct rjson_ctx * c, char ch) {
    _assert_st(c, _st_false);
    return _next_bool(c, ch, "false");
}

enum rjson_next_res _next_null(struct rjson_ctx * c, char ch) {
    const char * word = "null";

    if (ch != word[c->special_word_pos]) {
        return rjson_next_syntax;
    }
    if (c->special_word_pos == strlen(word) - 1) {
        c->cur = rjson_null;
        _set_st(c, _st_idle);
    } else {
        c->special_word_pos++;
    }

    return rjson_next_ok;
}

enum rjson_next_res _next_bool(struct rjson_ctx * c, char ch,
        const char * word) {
    if (ch != word[c->special_word_pos]) {
        return rjson_next_syntax;
    }
    if (c->special_word_pos == strlen(word) - 1) {
        c->cur = rjson_bool;
        _set_st(c, _st_idle);
    } else {
        c->special_word_pos++;
    }

    return rjson_next_ok;
}

enum _add_str_ch_res _add_str_ch(struct rjson_ctx * c, char ch) {
    if (c->str_len == c->str_mlen) {
        return _add_ch_overflow;
    } else {
        c->str[c->str_len] = ch;
        c->str_len++;
        return _add_ch_ok;
    }
}

enum _str_escape_res _str_escape(char * ch) {
    switch (*ch) {
    case 'u':
        return _str_escape_utf16;
    case '\"':
        *ch = '\"';
        return _str_escaped;
    case '\\':
        *ch = '\\';
        return _str_escaped;
    case '/':
        *ch = '/';
        return _str_escaped;
    case 'b':
        *ch = '\b';
        return _str_escaped;
    case 'f':
        *ch = '\f';
        return _str_escaped;
    case 'n':
        *ch = '\n';
        return _str_escaped;
    case 'r':
        *ch = '\r';
        return _str_escaped;
    case 't':
        *ch = '\t';
        return _str_escaped;
    default:
        return _str_escape_invalid;
    };
}

#ifdef SOB_RJSON_DEMO

#include <stdio.h>

enum {
    _str_buf_mlen = 120
};

int main(int argc, char ** argv) {
    const char * str = "{\"hello\": \"world\",\n"
        "\t\"a\": {\"b\": true, \"c\": [[-228, 123], 0.314e1, 420228e-03]}}";

    int i;
    char str_buf[_str_buf_mlen];
    struct rjson_ctx c;

    if (argc > 1) {
        str = argv[1];
    }

    rjson_init(&c, str_buf, _str_buf_mlen);

    printf("str: '%s'\n", str);

    for (i = 0; i < strlen(str) + 1; i++) { /* want to include the '\0' */
        enum rjson_next_res nres = rjson_next(&c, str[i]);

        if (nres == rjson_next_syntax) {
            fprintf(stderr, "syntax error @ %lu ('%c')\n",
                rjson_pos(&c), str[i]);
            return 1;
        } else if (nres == rjson_next_fin) {
            printf("fin\n");
            i = strlen(str) + 1; /* kinda hacky but whatever */
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
    }


    return 0;
}

#endif /* SOB_RJSON_DEMO */

