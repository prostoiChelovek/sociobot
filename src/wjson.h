#ifndef SOB_WJSON_H_SENTRY
#define SOB_WJSON_H_SENTRY

#include <stddef.h> /* for size_t */

#define SOB_WJSON_CHECK(stmt) \
    do { \
        enum wjson_res SOB_WJSON_CHECK_res = (stmt); \
        if (SOB_WJSON_CHECK_res != wjson_ok) { \
            return SOB_WJSON_CHECK_res; \
        } \
    } while (0)

struct wjson_ctx;

enum wjson_res {
    wjson_bad_arg = -3,
    wjson_syntax = -2,
    wjson_overflow = -1,
    wjson_ok = 1
};

void wjson_init(struct wjson_ctx * c, char * out_str, size_t out_str_max_len,
    int is_pretty);

const char * wjson_out_str(const struct wjson_ctx * c);

enum wjson_res wjson_str(struct wjson_ctx * c, char * str);
enum wjson_res wjson_int(struct wjson_ctx * c, long long num);
enum wjson_res wjson_double(struct wjson_ctx * c, double num);
enum wjson_res wjson_bool(struct wjson_ctx * c, int is_true);
enum wjson_res wjson_null(struct wjson_ctx * c);
enum wjson_res wjson_obj_start(struct wjson_ctx * c);
enum wjson_res wjson_obj_end(struct wjson_ctx * c);
enum wjson_res wjson_arr_start(struct wjson_ctx * c);
enum wjson_res wjson_arr_end(struct wjson_ctx * c);

#endif /* SOB_WJSON_H_SENTRY */

