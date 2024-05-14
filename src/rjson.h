#ifndef SOB_RJSON_H_SENTRY
#define SOB_RJSON_H_SENTRY

#include <stddef.h> /* for size_t */

enum rjson_ty {
    rjson_incomplete = 0,

    rjson_str,
    rjson_num,
    rjson_bool,
    rjson_null,

    rjson_obj_start,
    rjson_obj_end,
    rjson_arr_start,
    rjson_arr_end
};

struct rjson_ctx;

void rjson_init(struct rjson_ctx * c, char * str_out_buf, size_t str_mlen);

enum rjson_next_res {
    rjson_next_syntax = -1,
    rjson_next_fin = 0,
    rjson_next_ok = 1
} rjson_next(struct rjson_ctx * c, char next_char);

size_t rjson_pos(const struct rjson_ctx * c);

enum rjson_ty rjson_cur_ty(const struct rjson_ctx * c);
const char * rjson_cur_str(const struct rjson_ctx * c);
double rjson_cur_num(const struct rjson_ctx * c);
int rjson_cur_is_true(const struct rjson_ctx * c);

#endif /* SOB_RJSON_H_SENTRY */

