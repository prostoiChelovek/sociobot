#include "tgdb.h"
#include "wdb.h"

#include <unistd.h>

enum {
    add_bmsg_ctxs_len_ = 10,
    bmsg_wdb_str_mlen_ = 5000
};

struct tg_chat {
};

struct tg_bmsg {
    struct tg_bmsg_id id;
    const char * text;
    struct tg_chat chat;
};

struct tg_bmsg_id {
};

struct tgdb_ev {
    enum tgdb_event ty;
};

enum add_bmsg_st_ {
    add_bmsg_st_free_ = 0,
    add_bmsg_st_not_started_,
    add_bmsg_st_written_,
    add_bmsg_st_done_,
    add_bmsg_st_fail_
};

struct add_bmsg_ctx_ {
    struct tgdb_ctx * c;
    const struct tg_bmsg * m;
    enum add_bmsg_st_ st;
    struct pollfd fd;
};

struct tgdb_ctx {
    const char * dir;

    struct add_bmsg_ctx_ add_bmsg_ctxs[add_bmsg_ctxs_len_];
};

static struct add_bmsg_ctx_ * get_add_bmsg_ctx_(struct tgdb_ctx * c);
static enum {
    open_bmsg_fail_dir_ = -1,
    open_bmsg_fail = 0,
    open_bmsg_ok_ = 1
} open_bmsg_(struct tgdb_ctx * c, int * fd_out, struct tgdb_bmsg_id i);

static void add_bmsg_ctx_init_(struct add_bmsg_ctx_ * ctx,
    const struct tg_bmsg * m)
static size_t add_bmsg_pollfds_(const struct add_bmsg_ctx_ * ctx,
    struct pollfd ** fds_out);
static enum tgdb_res add_bmsg_update_(struct add_bmsg_ctx_ * ctx,
    struct pollfd * fds, size_t fds_len);

static enum tgdb_res add_bmsg_update_idle_(struct add_bmsg_ctx_ * ctx,
    struct pollfd * fds, size_t fds_len);
static enum tgdb_res add_bmsg_update_written_(struct add_bmsg_ctx_ * ctx,
    struct pollfd * fds, size_t fds_len);

enum tgdb_res tgdb_init(struct tgdb_ctx * c, const char * base_dir)
{
    size_t i;
    c->dir = base_dir;
    for (i = 0; i < add_bmsg_ctxs_len_; i++) {
        c->add_bmsg_ctxs[i].st = add_bmsg_st_free_;
        c->add_bmsg_ctxs[i].c = c;
    }
}

size_t tgdb_evs(const struct tgdb_ctx * c, const struct tgdb_ev ** evs_out);

size_t tgdb_pollfds(const struct tgdb_ctx * c,
    struct pollfd ** fds_out);

enum tgdb_res tgdb_update(struct tgdb_ctx * c,
    const struct pollfd * fds, size_t fds_len);

enum tgdb_res tgdb_stop_prep(struct tgdb_ctx * c);

enum tgdb_res tgdb_stop(struct tgdb_ctx * c);

enum tgdb_res tgdb_add_bmsg(struct tgdb_ctx * c, struct tg_bmsg * m)
{
    struct add_bmsg_ctx_ * ctx;
    enum open_res_ ores;
    int fd;

    if (m->id != tgdb_bmsg_id_null()) {
        return tgdb_bad_param;
    }
    m->id = gen_id_(c);

    ctx = get_add_bmsg_ctx_(c);
    if (ctx == NULL) {
        return tgdb_too_many;
    }

    add_bmsg_ctx_init_(ctx, m);
    return tgdb_ok;
}

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

static struct add_bmsg_ctx_ * get_add_bmsg_ctx_(struct tgdb_ctx * c)
{
    size_t i;
    for (i = 0; i < add_bmsg_ctxs_len_; i++) {
        struct add_bmsg_ctx * ctx = &c->add_bmsg_ctxs[i];
        if (ctx->st == add_bmsg_st_free_) {
            return ctx;
        }
    }
    return NULL;
}

static enum open_bmsg_res_ open_bmsg_(struct tgdb_ctx * c,
    int * fd_out, struct tgdb_bmsg_id i)
{
    if (mk_bmsg_dir_(c) != mk_bmsg_dir_ok_) {
        return open_bmsg_fail_dir_;
    }
    return open_bmsg_fail_;
}

static void add_bmsg_ctx_init_(struct add_bmsg_ctx_ * ctx,
    const struct tg_bmsg * m)
{
    ctx->m = m;
    ctx->st = add_bmsg_ctx_idle_;
}

static size_t add_bmsg_pollfds_(const struct add_bmsg_ctx_ * ctx,
    struct pollfd ** fds_out)
{
    switch (ctx->st) {
    case add_bmsg_st_free_:
    case add_bmsg_st_done_:
    case add_bmsg_st_fail_:
    case add_bmsg_st_idle_:
        return 0;
    case add_bmsg_st_written_:
        *fds_out = &ctx->fd;
        return 1;
    };
}

static enum tgdb_res add_bmsg_update_(struct add_bmsg_ctx_ * ctx,
    struct pollfd * fds, size_t fds_len)
{
    switch (ctx->st) {
    case add_bmsg_st_free_:
        return tgdb_ok;
    case add_bmsg_st_idle_:
        return add_bmsg_update_idle_(ctx, fds, fds_len);
    case add_bmsg_st_written_:
        return add_bmsg_update_written_(ctx, fds, fds_len);
    case add_bmsg_st_done_:
        return tgdb_ok;
    case add_bmsg_st_fail_:
        return tgdb_fail;
    };
}

static enum tgdb_res add_bmsg_update_idle_(struct add_bmsg_ctx_ * ctx,
    struct pollfd * fds, size_t fds_len)
{
    /* XXX: maybe a bad idea to use so much stack */
    char str[bmsg_wdb_str_len_];
    size_t str_len;
    if (bmsg_wdb_(ctx->m, str, bmsg_wdb_str_len_, &str_len) != wdb_ok) {
        ctx->st = add_bmsg_st_fail_;
        return tgdb_fail;
    }
    if (open_bmsg_(ctx->c, &ctx->fd.fd,
            tgdb_bmsg_get_id(ctx->m)) != open_bmsg_ok_) {
        ctx->st = add_bmsg_st_fail_;
        return tgdb_fail;
    }
    ctx->fd.events = POLLIN;
    if (write(ctx->fd, str, str_len) != str_len) {
        return tgdb_fail;
    }
    ctx->st = add_bmsg_st_written_;
    return tgdb_ok;
}

static enum tgdb_res add_bmsg_update_written_(struct add_bmsg_ctx_ * ctx,
    struct pollfd * fds, size_t fds_len)
{
}

#ifdef SOB_TGDB_DEMO

#include <stdio.h>
#include <poll.h>

static const struct * tgdb_ev wait_ev_(struct tgdb_ctx * c,
    enum tgdb_event ev_wait)
{
    struct pollfd * fds;
    size_t fds_len;
    const struct tgdb_ev * evs;
    size_t evs_len;
    size_t i;

    while (1) {
        fds_len = tgdb_pollfds(c, &fds);
        if (fds_len > 0) {
            int pres = poll(fds, fds_len, -1);
            if (pres <= 0) {
                perror("poll");
                return NULL;
            }
            tgdb_update(c, fds, fds_len);

            evs_len = tgdb_evs(c, &evs);
            for (i = 0; i < evs_len; i++) {
                const struct tgdb_ev * ev = evs[i];
                enum tgdb_event ty = tgdb_ev_ty(ev);
                fprintf(stderr, "ev : %s\n", tgdb_event_str(ty));
                if (ty == ev_wait) {
                    return ev;
                } else if (tgdb_ev_is_fail(ev)) {
                    return ev;
                } else {
                    continue;
                }
            }
        } else {
            return NULL;
        }
    }
}

/* undef at the bottom */
#define SOB_TGDB_WAIT_EV_(wait_ev) \
    do { \
        ev = wait_ev_(&c, (wait_ev)); \
        if (ev == NULL || tgdb_ev_ty(&ev) != ev) { \
            printf("bad event %s while waiting for %s @ %i\n", \
                tgdb_event_str(tgdb_ev_ty(&ev)), \
                tgdb_event_str(wait_ev), \
                __LINE__); \
            return 1; \
        } \
    } while (0)
#define SOB_TGDB_CHECK_(stmt) \
    do { \
        enum tgdb_res SOB_TGDB_CHECK_res_ = (stmt); \
        if (SOB_TGDB_CHECK_res_ != tgdb_ok) { \
            printf("fail @ %i (%s) : %i\n", \
                __LINE__, #stmt, SOB_TGDB_CHECK_res_); \
            return 1; \
        } \
    } while (0)

enum {
    pend_bmsg_ids_len_ = 5
};

int main(void)
{
    struct tgdb_ctx c;
    const struct tgdb_ev * ev;
    struct tg_bmsg bmsg;
    struct tg_chat chat;
    struct tg_bmsg_id last_pend_bmsg = tg_bmsg_id_null();
    struct tg_bmsg_id pend_bmsg_ids[pend_bmsg_ids_len_];
    struct tg_bmsg pend_bmsgs[pend_bmsg_ids_len_];
    size_t i;

    SOB_TGDB_CHECK_(tgdb_init(&c, "/tmp/tgdb_demo"));
    SOB_TGDB_WAIT_EV_(tgdb_ev_init);

    tg_chat_init(&chat, 505249189);
    tg_bmsg_init(&bmsg, &chat, "hello, world!");
    SOB_TGDB_CHECK_(tgdb_add_bmsg(&c, &bmsg));
    SOB_TGDB_WAIT_EV_(tgdb_ev_add_bmsg);
    tg_bmsg_init(&bmsg, &chat, "what's up\n\"This is a test\"");
    SOB_TGDB_CHECK_(tgdb_add_bmsg(&c, &bmsg));
    SOB_TGDB_CHECK_(tgdb_set_bmsg_status(&c,
        tgdb_bmsg_get_id(&bmsg), tg_bmsg_sent));
    /* tgdb_ev_set_bmsg_status should also happen there */
    SOB_TGDB_WAIT_EV_(tgdb_ev_add_bmsg);

    while (1) {
        size_t pend_bmsgs_len;
        SOB_TGDB_CHECK_(tgdb_get_pend_bmsgs(&c, last_pend_bmsg,
            pend_bmsg_ids, pend_bmsg_ids_len_, &pend_bmsgs_len));
        SOB_TGDB_WAIT_EV_(tgdb_ev_get_pend_bmsgs);
        if (pend_bmsgs_len > 0) {
            for (i = 0; i < pend_bmsgs_len; i++) {
                last_pend_bmsg = pend_bmsg_ids[i];
                SOB_TGDB_CHECK_(tgdb_get_bmsg(&c,
                    pend_bmsg_ids[i], &pend_bmsgs[i]));
            }
            for (i = 0; i < pend_bmsgs_len; i++) {
                char print_str[2048];
                SOB_TGDB_WAIT_EV_(tgdb_ev_get_bmsg);
                tg_bmsg_print(tgdb_ev_bmsg(&ev), print_str, sizeof(print_str));
                printf("pend : %s\n", print_str);
            }
            SOB_TGDB_CHECK_(tgdb_set_bmsg_status(&c,
                last_pend_bmsg, tg_bmsg_sent);
            /* set_status is enqueued but will happen during stop_prep  */
        } else {
            break;
        }
    }

    SOB_TGDB_CHECK_(tgdb_stop_prep(&c));
    /* tgdb_ev_set_bmsg_status should happen during stop prep */
    SOB_TGDB_WAIT_EV_(tgdb_ev_stop);
    SOB_TGDB_CHECK_(tgdb_stop(&c));

    return 0;
}

#undef SOB_TGDB_WAIT_EV_
#undef SOB_TGDB_CHECK_

#endif /* SOB_TGDB_DEMO */

