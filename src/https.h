#ifndef SOB_HTTPS_H_SENTRY
#define SOB_HTTPS_H_SENTRY

#include <stddef.h>
#include <poll.h>

typedef void (*https_resp_data_cb)(const char * resp, size_t len, void * user);

struct https_mod;

struct https_ev;

enum https_ev_type {
    https_ev_init,
    https_ev_init_fail,
    https_ev_stopped,
    https_ev_req_fin, /* always after https_ev_req_data */
    https_ev_req_fail
};

enum https_req_method {
    https_method_get,
    https_method_post
};

/* use only after https_stop succeeded or with uninit mod */
enum https_init_res {
    https_init_fail_timerfd = -4,
    https_init_fail_no_mem = -3,
    https_init_fail_curl_easy_init = -2,
    https_init_fail_curl_multi_init = -1,
    https_init_ok = 1
} https_init(struct https_mod * mod,
    https_resp_data_cb data_cb, void * data_cb_user);

enum https_stop_prep_res {
    https_stop_prep_fail = -1,
    https_stop_prep_ok = 1
} https_stop_prep(struct https_mod * mod); 

enum https_stop_res {
    https_stop_fail_no_prep = -2,
    https_stop_fail = -1,
    https_stop_ok = 1
} https_stop(struct https_mod * mod);

size_t https_pollfds(struct https_mod * mod, struct pollfd ** fds_out);

void https_update(struct https_mod * mod, struct pollfd * fds, nfds_t nfds);

size_t https_events(struct https_mod * mod, struct https_ev ** evs_out);

void https_set_timeout(struct https_mod * mod, long timeout_s);

enum https_verbosity {
    https_verbosity_silent,
    https_verbosity_debug
};
void https_set_verbosity(struct https_mod * mod, enum https_verbosity level);

enum https_stop_strategy {
    https_stop_strat_abort,
    https_stop_strat_wait
};
void https_set_stop_strat(struct https_mod * mod, enum https_stop_strategy s);

enum https_req_res {
    https_req_fail_stopping = -3,
    https_req_fail_other_pend = -2,
    https_req_fail = -1,
    https_req_ok = 1
} https_req_json(struct https_mod * mod,
    enum https_req_method method, const char * url, const char * data);

long https_resp_status(const struct https_mod * mod);

enum https_ev_type https_ev_ty(const struct https_ev * ev);

#endif /* SOB_HTTPS_H_SENTRY */

