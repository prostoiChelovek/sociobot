#ifndef SOB_TGDB_H_SENTRY
#define SOB_TGDB_H_SENTRY

#include <stddef.h> /* for size_t */
#include <poll.h>   /* for pollfd */
#include <stdint.h> /* for uint64_t */

struct tg_chat;
struct tg_bmsg;
struct tg_bmsg_id;

enum tg_bmsg_status {
    tg_bmsg_unsaved,
    tg_bmsg_pend,
    tg_bmsg_sent,
    tg_bmsg_send_fail
};

enum tgdb_res {
    tgdb_bad_param = -2,
    tgdb_too_many = -1,
    tgdb_fail = 0,
    tgdb_ok = 1
};

enum tgdb_event {
    tgdb_ev_init,
    tgdb_ev_init_fail,
    tgdb_ev_stop,
    tgdb_ev_stop_fail,
    tgdb_ev_add_bmsg,
    tgdb_ev_add_bmsg_fail,
    tgdb_ev_get_bmsg,
    tgdb_ev_get_bmsg_fail,
    tgdb_ev_get_bmsg_fail_not_found,
    tgdb_ev_get_pend_bmsgs,
    tgdb_ev_get_pend_bmsgs_fail,
    tgdb_ev_set_bmsg_status,
    tgdb_ev_set_bmsg_status_fail
};
struct tgdb_ev;
struct tgdb_ctx;

enum tgdb_res tgdb_init(struct tgdb_ctx * c, const char * base_dir);

size_t tgdb_evs(const struct tgdb_ctx * c, const struct tgdb_ev ** evs_out);

size_t tgdb_pollfds(const struct tgdb_ctx * c,
    struct pollfd ** fds_out);

enum tgdb_res tgdb_update(struct tgdb_ctx * c,
    const struct pollfd * fds, size_t fds_len);

enum tgdb_res tgdb_stop_prep(struct tgdb_ctx * c);

enum tgdb_res tgdb_stop(struct tgdb_ctx * c);

enum tgdb_res tgdb_add_bmsg(struct tgdb_ctx * c, struct tg_bmsg * m);

enum tgdb_res tgdb_get_bmsg(struct tgdb_ctx * c,
    struct tg_bmsg_id i, struct tg_bmsg * msg_out);

enum tgdb_res tgdb_get_pend_bmsgs(struct tgdb_ctx * c,
    struct tg_bmsg_id start_excluding,
    struct tg_bmsg_id * msgs_out,
    size_t msgs_out_maxlen, size_t * msgs_out_len);

enum tgdb_res tgdb_set_bmsg_status(struct tgdb_ctx * c,
    struct tg_bmsg_id m, enum tg_bmsg_status s);


void tg_bmsg_init(struct tg_bmsg * m,
    const struct tg_chat * to, const char * text);

struct tg_bmsg_id tg_bmsg_get_id(const struct tg_bmsg * m);

size_t tg_bmsg_print(struct tg_bmsg m, char * out, size_t out_mlen); 


void tg_chat_init(struct tg_chat * c, uint64_t id);


struct tg_bmsg_id tg_bmsg_id_null();

int tg_bmsg_id_eq(struct tg_bmsg_id a, struct tg_bmsg_id b);

size_t tg_bmsg_id_print(struct tg_bmsg_id i, char * out, size_t out_mlen); 


int tgdb_event_is_fail(enum tgdb_event event);

const char * tgdb_event_str(enum tgdb_event event);

enum tgdb_event tgdb_ev_ty(const struct tgdb_ev * ev);

struct tg_bmsg * tgdb_ev_bmsg(struct tgdb_ev * ev);

#endif /* SOB_TGDB_H_SENTRY */

