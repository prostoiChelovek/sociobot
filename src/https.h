#ifndef SOB_HTTPS_H_SENTRY
#define SOB_HTTPS_H_SENTRY

struct https_mod;

enum https_init_res {
    https_init_err_curl_easy_init = -2,
    https_init_err_curl_multi_init = -1,
    https_init_ok = 1
};

enum https_init_res https_init(struct https_mod * mod);

#endif /* SOB_HTTPS_H_SENTRY */

