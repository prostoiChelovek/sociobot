#ifndef SOB_RDB_H_SENTRY
#define SOB_RDB_H_SENTRY

#include <stddef.h> /* for size_t */

enum rdb_ty {
    rdb_incomplete = 0,

    rdb_key,
    rdb_str,
    rdb_num,
    rdb_bool,
};

struct rdb_ctx;

void rdb_init(struct rdb_ctx * c, char * str_out_buf, size_t str_maxlen);

enum rdb_next_res {
    rdb_next_syntax = -1,
    rdb_next_fin = 0,
    rdb_next_ok = 1
} rdb_next(struct rdb_ctx * c, char next_char);

size_t rdb_pos(const struct rdb_ctx * c);

enum rdb_ty rdb_cur_ty(const struct rdb_ctx * c);
const char * rdb_cur_str(const struct rdb_ctx * c);
double rdb_cur_num(const struct rdb_ctx * c);
int rdb_cur_is_true(const struct rdb_ctx * c);

#endif /* SOB_RDB_H_SENTRY */

