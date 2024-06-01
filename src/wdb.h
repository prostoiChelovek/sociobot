#ifndef SOB_WDB_H_SENTRY
#define SOB_WDB_H_SENTRY

#include <stddef.h> /* for size_t */

#define SOB_WDB_CHECK(stmt) \
    do { \
        const enum wdb_res SOB_WDB_CHECK_res_ = (stmt); \
        if (SOB_WDB_CHECK_res_ != wdb_ok) { \
            return SOB_WDB_CHECK_res_; \
        } \
    } while (0)

struct wdb_ctx;

enum wdb_res {
    wdb_syntax = -2,
    wdb_overflow = -1,
    wdb_ok = 1
};

void wdb_init(struct wdb_ctx * c, char * out, size_t out_maxlen);

const char * wdb_out_str(const struct wdb_ctx * c);

enum wdb_res wdb_key(struct wdb_ctx * c, const char * v);
enum wdb_res wdb_str(struct wdb_ctx * c, const char * v);
enum wdb_res wdb_long_str(struct wdb_ctx * c, const char * v);
enum wdb_res wdb_int(struct wdb_ctx * c, long v);
enum wdb_res wdb_num(struct wdb_ctx * c, double v);
enum wdb_res wdb_bool(struct wdb_ctx * c, int v);

enum wdb_res wdb_fin(struct wdb_ctx * c);

#endif /* SOB_WDB_H_SENTRY */

