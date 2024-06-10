/* for fsync in glibc up to and including 2.15 */
/* for realpath */
#define _XOPEN_SOURCE 500

#include "afs.h"
#include "panic.h"

#include <unistd.h>
#include <limits.h> /* for PATH_MAX */
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/socket.h> /* for socketpair */
#include <sys/wait.h> /* for waitpid */
#include <sys/mman.h> /* for mmap, munmap */
#include <sys/stat.h> /* for mkdir */

struct afs_ev {
    enum afs_event ty;
    int fd;
    union {
        struct {
            size_t len; /* always equal to requested if not fail */
        } write;
        struct {
            size_t len;
            const char * data;
        } readall;
    } d;
};

enum proc_cmd_ {
    proc_cmd_none_ = 0,
    proc_cmd_exit_,
    proc_cmd_open_,
    proc_cmd_close_,
    proc_cmd_fsync_,
    proc_cmd_write_,
    proc_cmd_readall_,
    proc_cmd_mkdir_
};
enum proc_res_ {
    proc_res_none_ = 0,
    proc_res_ok_ = 1,
    proc_res_fail_
};
enum proc_child_st_ {
    proc_child_st_not_started_ = 0,
    proc_child_st_idle_ = 1,
    proc_child_st_busy_ = 2
};
struct proc_shared_ {
    /* modified by parent */
    enum proc_cmd_ cmd;
    size_t write_len;
    int open_flags;

    /* modified by child */
    enum proc_res_ res;
    enum proc_child_st_ st;
    size_t written;
    size_t read_len;
    struct sob_fail fail;
};

enum {
    /* best guess; remember that page size on a different target may differ */
    rw_buf_len_ = PAGESIZE * 2 - sizeof(struct proc_shared_),
    proc_evs_maxlen_ = 3,
    evs_pfds_prealloc_len_ = 2,
};

enum proc_st_ {
    proc_st_uninit_ = 0,
    proc_st_init_pend_,
    proc_st_avail_,
    proc_st_busy_,
    proc_st_dead_
};

struct proc_ {
    struct sob_fail fail;
    int is_stop_req;
    enum proc_st_ st;
    struct afs_ev evs[proc_evs_maxlen_];
    size_t evs_len;
    struct proc_shared_ * shared;
    void * rw_buf;
    size_t rw_buf_len;
    void * mmap_start;
    size_t mmap_len;
    pid_t pid;
    int fd;
};

struct ps_ {
    int fd;
    struct proc_ p;
    struct ps_ * next;
    enum proc_cmd_ cmd_after_init;
    int is_avail;
};

struct afs_ctx {
    struct sob_fail fail;
    int is_stop_req;
    struct ps_ * ps;

    struct pollfd * pfds;
    size_t pfds_maxlen;
    size_t pfds_len;
    struct afs_ev * evs;
    size_t evs_maxlen;
    size_t evs_len;
};

/* undef at the bottom */
#define SOB_AFS_FAIL_(msg) SOB_FAIL_INIT(&c->fail, msg);
#define SOB_AFS_PROC_FAIL_(msg) SOB_FAIL_INIT(&p->fail, msg);
#define SOB_AFS_PROC_C_FAIL_(msg) SOB_FAIL_INIT(&s->fail, msg);
#define SOB_AFS_PROC_CHECK_EV_(stmt) \
    do { \
        if ((stmt) == NULL) { \
            SOB_PANIC("add_ev %s", #stmt); \
            return afs_fail; \
        } \
    } while (0)

static struct ps_ * maybe_alloc_ps_(struct afs_ctx * c, int * was_init_out);
static struct ps_ * ps_add_(struct afs_ctx * c);
static enum afs_res ps_del_(struct afs_ctx * c, int fd);
static struct ps_ * ps_get_(struct afs_ctx * c, int fd);
static int ps_next_fd_(struct afs_ctx * c);
static size_t ps_len_(struct afs_ctx * c);

static enum afs_res proc_init_(struct proc_ * p);
static enum afs_res proc_update_(struct proc_ * p,
    const struct pollfd * fds, size_t fds_len);
static enum afs_res proc_stop_prep_(struct proc_ * p);
static enum afs_res proc_stop_(struct proc_ * p);
static size_t proc_evs_(struct proc_ * p, struct afs_ev ** evs_out);
static int proc_fd_(const struct proc_ * p);

static enum afs_res proc_open_(struct proc_ * p);
static enum afs_res proc_close_(struct proc_ * p);
static enum afs_res proc_fsync_(struct proc_ * p);
static enum afs_res proc_write_(struct proc_ * p);
static enum afs_res proc_readall_(struct proc_ * p);
static enum afs_res proc_mkdir_(struct proc_ * p);

static enum afs_res proc_update_init_pend_(struct proc_ * p, short revents);
static enum afs_res proc_update_busy_(struct proc_ * p, short revents);
static enum afs_res proc_update_avail_(struct proc_ * p, short revents);
static enum afs_res proc_update_dead_(struct proc_ * p);

static enum afs_res proc_send_cmd_(struct proc_ * p, enum proc_cmd_ cmd);
static enum afs_res proc_notify_child_(struct proc_ * p);
static void proc_destroy_child_(struct proc_ * p);
static struct afs_ev * proc_add_ev_(struct proc_ * p, enum afs_event ty);

static int proc_child_worker_(int fd,
    struct proc_shared_ * s, void * rw_buf, size_t rw_buf_len);
static enum proc_res_ proc_child_open_(struct proc_shared_ * s, int * fd,
    void * rw_buf, size_t rw_buf_len);
static enum proc_res_ proc_child_close_(struct proc_shared_ * s, int * fd);
static enum proc_res_ proc_child_fsync_(struct proc_shared_ * s, int fd);
static enum proc_res_ proc_child_write_(struct proc_shared_ * s, int fd,
    void * rw_buf, size_t rw_buf_len);
static enum proc_res_ proc_child_readall_(struct proc_shared_ * s, int fd,
    void * rw_buf, size_t rw_buf_len);
static enum proc_res_ proc_child_mkdir_(struct proc_shared_ * s,
    void * rw_buf, size_t rw_buf_len);

static enum afs_event proc_cmd_fail_ev_(enum proc_cmd_ cmd);

struct sob_fail * afs_get_fail(struct afs_ctx * c)
{
    return &c->fail;
}

void afs_init(struct afs_ctx * c)
{
    c->is_stop_req = 0;
    c->ps = NULL;
    c->pfds = NULL;
    c->pfds_maxlen = 0;
    c->pfds_len = 0;
    c->evs = NULL;
    c->evs_maxlen = 0;
    c->evs_len = 0;
}

/* undef after afs_update */
#define SOB_AFS_UPD_ADD_EV_() \
    do { \
        if (c->evs_len >= c->evs_maxlen) { \
            SOB_PANIC("evs overflow (maxlen = %lu)", c->evs_maxlen); \
        } \
        c->evs_len++; \
        oev++; \
    } while (0)

void afs_update(struct afs_ctx * c,
    struct pollfd * fds, size_t fds_len)
{
    struct ps_ * ps = c->ps;
    struct afs_ev * oev = c->evs;
    c->evs_len = 0;
    if (ps != NULL && oev == NULL) {
        SOB_PANIC("no evs");
    }

    while (ps != NULL) {
        struct ps_ * next_ps = ps->next;
        enum afs_res r = proc_update_(&ps->p, fds, fds_len);
        struct afs_ev * evs = NULL;
        size_t evs_len = proc_evs_(&ps->p, &evs);
        int should_del = 0;
        while (evs_len > 0 && evs != NULL) {
            int should_add_ev = 0;
            evs->fd = ps->fd;
            switch (afs_ev_ty(evs)) {
            case afs_ev_init:
                should_add_ev = 0;
                ps->is_avail = 1;
                if (ps->cmd_after_init == proc_cmd_open_) {
                    enum afs_res ores = proc_open_(&ps->p);
                    if (ores != afs_ok) {
                        memcpy(&c->fail, &ps->p.fail, sizeof(struct sob_fail));
                        oev->fd = evs->fd;
                        oev->ty = afs_ev_open_fail;
                        SOB_AFS_UPD_ADD_EV_();
                    }
                } else if (ps->cmd_after_init == proc_cmd_mkdir_) {
                    enum afs_res ores = proc_mkdir_(&ps->p);
                    if (ores != afs_ok) {
                        memcpy(&c->fail, &ps->p.fail, sizeof(struct sob_fail));
                        oev->fd = evs->fd;
                        oev->ty = afs_ev_mkdir_fail;
                        SOB_AFS_UPD_ADD_EV_();
                    }
                } else if (ps->cmd_after_init != proc_cmd_none_) {
                    oev->fd = evs->fd;
                    oev->ty = proc_cmd_fail_ev_(ps->cmd_after_init);
                    SOB_AFS_UPD_ADD_EV_();
                    SOB_AFS_FAIL_("bad cmd_after_init (no errno)");
                }
                ps->cmd_after_init = proc_cmd_none_;
                break;
            case afs_ev_close:
            case afs_ev_close_fail:
                should_add_ev = 1;
                ps->fd = -1;
                break;
            /* after these 3 events proc_ is unusable */
            case afs_ev_init_fail:
                should_add_ev = 0;
                should_del = 1;
                if (ps->cmd_after_init != proc_cmd_none_) {
                    oev->fd = evs->fd;
                    oev->ty = proc_cmd_fail_ev_(ps->cmd_after_init);
                    SOB_AFS_UPD_ADD_EV_();
                }
                ps->cmd_after_init = proc_cmd_none_;
                break;
            case afs_ev_mkdir_fail:
            case afs_ev_mkdir:
                should_add_ev = 1;
                ps->fd = -1;
                break;
            /* do not del after ev_stop[_fail] because gotta call proc_stop */
            case afs_ev_stop:
            case afs_ev_stop_fail:
                should_add_ev = 0;
                ps->fd = -1;
                ps->is_avail = 0;
                break;
            default:
                should_add_ev = 1;
                break;
            };
            if (afs_ev_is_fail(evs)) {
                memcpy(&c->fail, &ps->p.fail, sizeof(struct sob_fail));
            }
            if (should_add_ev) {
                should_add_ev = 0;
                memcpy(oev, evs, sizeof(struct afs_ev));
                SOB_AFS_UPD_ADD_EV_();
            }
            evs_len--;
            evs++;
        }
        if (r != afs_ok || should_del) { /* proc_ is destroyed and unusable */
            /* will del same ps because we iterate same way as ps_del_ */
            (void) ps_del_(c, ps->fd);
            ps = NULL;
        }
        ps = next_ps;
    }
    if (c->is_stop_req) {
        int all_stopped = 1;
        ps = c->ps;
        while (ps != NULL) {
            if (ps->p.st != proc_st_uninit_) {
                all_stopped = 0;
            }
            ps = ps->next;
        }
        if (all_stopped) {
            oev->fd = -1;
            oev->ty = afs_ev_stop;
            SOB_AFS_UPD_ADD_EV_();
        }
    }
}

#undef SOB_AFS_UPD_ADD_EV_

enum afs_res afs_get_rw_buf(struct afs_ctx * c,
    int fd_from_afs, void ** buf_out, size_t * len_out)
{
    struct ps_ * ps = ps_get_(c, fd_from_afs);
    if (ps == NULL || ! ps->is_avail) {
        return afs_fail_bad_fd;
    }
    *buf_out = ps->p.rw_buf;
    *len_out = ps->p.rw_buf_len;
    return afs_ok;
}

enum afs_res afs_open(struct afs_ctx * c,
    const char * path, int flags, int * afs_fd_out)
{
    int was_init;
    struct ps_ * ps = maybe_alloc_ps_(c, &was_init);
    if (ps == NULL) {
        return afs_fail_alloc;
    }

    if (strlen(path) + 1 > ps->p.rw_buf_len) {
        SOB_AFS_FAIL_("path does not fit in rw_buf (no errno)");
        return afs_fail_bad_arg;
    }

    *afs_fd_out = ps->fd;

    if (ps->p.rw_buf == NULL) {
        SOB_PANIC("rw_buf is NULL");
    }
    if (ps->p.shared == NULL) {
        SOB_PANIC("shared is NULL");
    }
    strcpy(ps->p.rw_buf, path);
    ps->p.shared->open_flags = flags;
    if (was_init) {
        enum afs_res r = proc_open_(&ps->p);
        memcpy(&c->fail, &ps->p.fail, sizeof(struct sob_fail));
        return r;
    } else {
        ps->is_avail = 0;
        ps->cmd_after_init = proc_cmd_open_;
        return afs_ok;
    }
}

enum afs_res afs_close(struct afs_ctx * c, int fd_from_afs)
{
    struct ps_ * ps = ps_get_(c, fd_from_afs);
    if (ps == NULL || ! ps->is_avail) {
        SOB_AFS_FAIL_("bad fd (no errno)");
        return afs_fail_bad_fd;
    }
    return proc_close_(&ps->p);
}

enum afs_res afs_fsync(struct afs_ctx * c, int fd_from_afs)
{
    struct ps_ * ps = ps_get_(c, fd_from_afs);
    if (ps == NULL || ! ps->is_avail) {
        SOB_AFS_FAIL_("bad fd (no errno)");
        return afs_fail_bad_fd;
    }
    return proc_fsync_(&ps->p);
}

enum afs_res afs_write(struct afs_ctx * c, int fd_from_afs, size_t len)
{
    struct ps_ * ps = ps_get_(c, fd_from_afs);
    if (ps == NULL || ! ps->is_avail) {
        SOB_AFS_FAIL_("bad fd (no errno)");
        return afs_fail_bad_fd;
    }
    if (ps->p.shared == NULL) {
        SOB_AFS_FAIL_("shared is NULL (no errno)");
        return afs_fail;
    }
    ps->p.shared->write_len = len;
    return proc_write_(&ps->p);
}

enum afs_res afs_readall(struct afs_ctx * c, int fd_from_afs)
{
    struct ps_ * ps = ps_get_(c, fd_from_afs);
    if (ps == NULL || ! ps->is_avail) {
        return afs_fail_bad_fd;
    }
    return proc_readall_(&ps->p);
}

enum afs_res afs_mkdir(struct afs_ctx * c, const char * path, int * afs_fd_out)
{
    int was_init;
    struct ps_ * ps = maybe_alloc_ps_(c, &was_init);
    if (ps == NULL) {
        return afs_fail_alloc;
    }

    if (strlen(path) + 1 > ps->p.rw_buf_len) {
        SOB_AFS_FAIL_("path does not fit in rw_buf (no errno)");
        return afs_fail_bad_arg;
    }

    *afs_fd_out = ps->fd;

    if (ps->p.rw_buf == NULL) {
        SOB_PANIC("rw_buf is NULL");
    }
    if (ps->p.shared == NULL) {
        SOB_PANIC("shared is NULL");
    }
    strcpy(ps->p.rw_buf, path);
    if (was_init) {
        enum afs_res r = proc_mkdir_(&ps->p);
        memcpy(&c->fail, &ps->p.fail, sizeof(struct sob_fail));
        return r;
    } else {
        ps->is_avail = 0;
        ps->cmd_after_init = proc_cmd_mkdir_;
        return afs_ok;
    }
}

enum afs_res afs_stop_prep(struct afs_ctx * c)
{
    if (! c->is_stop_req) {
        enum afs_res res = afs_ok;
        struct ps_ * ps = c->ps;
        c->is_stop_req = 1;
        while (ps != NULL) {
            enum afs_res r = proc_stop_prep_(&ps->p);
            if (r != afs_ok) {
                /* XXX: will only keep last fail */
                memcpy(&c->fail, &ps->p.fail, sizeof(struct sob_fail));
                res = r;
            }
            ps = ps->next;
        }
        return res;
    } else {
        SOB_AFS_FAIL_("stop_prep already pending (no errno)");
        return afs_fail;
    }
}

enum afs_res afs_stop(struct afs_ctx * c)
{
    enum afs_res res = afs_ok;
    struct ps_ * ps = c->ps;
    while (ps != NULL) {
        struct ps_ * next_ps = ps->next;
        enum afs_res r = proc_stop_(&ps->p);
        if (r != afs_ok) {
            /* XXX: will only keep last fail */
            memcpy(&c->fail, &ps->p.fail, sizeof(struct sob_fail));
            res = r;
        }
        free(ps);
        ps = next_ps;
    }
    c->ps = NULL;
    if (c->pfds != NULL) {
        free(c->pfds);
    }
    if (c->evs != NULL) {
        free(c->evs);
    }
    c->pfds = NULL;
    c->pfds_len = 0;
    c->pfds_maxlen = 0;
    c->evs = NULL;
    c->evs_len = 0;
    c->evs_maxlen = 0;
    return res;
}

size_t afs_pollfds(struct afs_ctx * c, struct pollfd ** fds_out)
{
    const struct ps_ * ps = c->ps;
    struct pollfd * pfd = c->pfds;
    c->pfds_len = 0;
    while (ps != NULL) {
        if (c->pfds_len == c->pfds_maxlen) {
            SOB_PANIC("not enough pfds");
        }
        pfd->fd = proc_fd_(&ps->p);
        if (pfd->fd != -1) {
            pfd->events = POLLIN;
            pfd->revents = 0;
            pfd++;
            c->pfds_len++;
        }
        ps = ps->next;
    }
    *fds_out = c->pfds;
    return c->pfds_len;
}

size_t afs_evs(struct afs_ctx * c, struct afs_ev ** evs_out)
{
    *evs_out = c->evs;
    return c->evs_len;
}

enum afs_event afs_ev_ty(const struct afs_ev * ev)
{
    return ev->ty;
}

int afs_ev_is_fail(const struct afs_ev * ev)
{
    switch (ev->ty) {
    case afs_ev_init_fail:
    case afs_ev_stop_fail:
    case afs_ev_open_fail:
    case afs_ev_close_fail:
    case afs_ev_fsync_fail:
    case afs_ev_write_fail:
    case afs_ev_readall_fail:
    case afs_ev_mkdir_fail:
        return 1;
    case afs_ev_init:
    case afs_ev_stop:
    case afs_ev_open:
    case afs_ev_close:
    case afs_ev_fsync:
    case afs_ev_write:
    case afs_ev_readall:
    case afs_ev_mkdir:
        return 0;
    }
    SOB_PANIC("unreacheable");
    return 0;
}

size_t afs_ev_write_len(const struct afs_ev * ev)
{
    return ev->d.write.len;
}

size_t afs_ev_readall_len(const struct afs_ev * ev)
{
    return ev->d.readall.len;
}

const char * afs_ev_readall_data(const struct afs_ev * ev)
{
    return ev->d.readall.data;
}

const char * afs_event_str(enum afs_event event)
{
    switch (event) {
    case afs_ev_init:
        return "afs_ev_init";
    case afs_ev_init_fail:
        return "afs_ev_init_fail";
    case afs_ev_stop:
        return "afs_ev_stop";
    case afs_ev_stop_fail:
        return "afs_ev_stop_fail";
    case afs_ev_open:
        return "afs_ev_open";
    case afs_ev_open_fail:
        return "afs_ev_open_fail";
    case afs_ev_close:
        return "afs_ev_close";
    case afs_ev_close_fail:
        return "afs_ev_close_fail";
    case afs_ev_fsync:
        return "afs_ev_fsync";
    case afs_ev_fsync_fail:
        return "afs_ev_fsync_fail";
    case afs_ev_write:
        return "afs_ev_write";
    case afs_ev_write_fail:
        return "afs_ev_write_fail";
    case afs_ev_readall:
        return "afs_ev_readall";
    case afs_ev_readall_fail:
        return "afs_ev_readall_fail";
    case afs_ev_mkdir:
        return "afs_ev_mkdir";
    case afs_ev_mkdir_fail:
        return "afs_ev_mkdir_fail";
    }
    return "";
}

static struct ps_ * maybe_alloc_ps_(struct afs_ctx * c, int * was_init_out)
{
    struct ps_ * ps = ps_get_(c, -1);
    if (ps != NULL) { /* reuse free proc */
        if (ps->p.st == proc_st_uninit_) {
            enum afs_res r = proc_init_(&ps->p);
            *was_init_out = 0;
            if (r != afs_ok) {
                memcpy(&c->fail, &ps->p.fail, sizeof(struct sob_fail));
                /* ps->fd is still -1; will find the same one as ps_get_(-1) */
                (void) ps_del_(c, -1);
                return NULL;
            }
        } else {
            *was_init_out = 1;
        }
        ps->fd = ps_next_fd_(c);
        return ps;
    } else {
        enum afs_res r;
        ps = ps_add_(c);
        if (ps == NULL) {
            return NULL;
        }
        *was_init_out = 0;
        r = proc_init_(&ps->p);
        if (r != afs_ok) {
            memcpy(&c->fail, &ps->p.fail, sizeof(struct sob_fail));
            (void) ps_del_(c, ps->fd);
            return NULL;
        }
        return ps;
    }
}

static struct ps_ * ps_add_(struct afs_ctx * c)
{
    size_t ps_len = ps_len_(c);
    struct ps_ * parent = c->ps;
    struct ps_ * ps = malloc(sizeof(struct ps_));
    if (ps == NULL) {
        SOB_AFS_FAIL_("malloc ps");
        return NULL;
    }
    ps_len++;

    if (c->pfds_maxlen < ps_len) {
        size_t new_pfds_len = ps_len + evs_pfds_prealloc_len_;
        struct pollfd * new_pfds = malloc(sizeof(struct pollfd) * new_pfds_len);
        if (new_pfds == NULL) {
            SOB_AFS_FAIL_("malloc pfds");
            free(ps);
            return NULL;
        }
        c->pfds = new_pfds;
        c->pfds_maxlen = new_pfds_len;
        memset(c->pfds, 0, sizeof(struct pollfd) * c->pfds_maxlen);
    }
    if (c->evs_maxlen < ps_len * proc_evs_maxlen_) {
        size_t new_evs_len = \
             (ps_len + evs_pfds_prealloc_len_) * proc_evs_maxlen_;
        struct afs_ev * new_evs = malloc(sizeof(struct afs_ev) * new_evs_len);
        if (new_evs == NULL) {
            SOB_AFS_FAIL_("malloc evs");
            free(ps);
            return NULL;
        }
        c->evs = new_evs;
        c->evs_maxlen = new_evs_len;
        memset(c->evs, 0, sizeof(struct afs_ev) * c->evs_maxlen);
    }

    ps->fd = ps_next_fd_(c);
    ps->next = NULL;
    ps->cmd_after_init = proc_cmd_none_;
    ps->is_avail = 0;
    if (parent != NULL) {
        while (parent->next != NULL) {
            parent = parent->next;
        }
        parent->next = ps;
    } else {
        ps->fd = 0;
        c->ps = ps;
    }
    return ps;
}

static enum afs_res ps_del_(struct afs_ctx * c, int fd)
{
    struct ps_ * parent = NULL;
    struct ps_ * ps = c->ps;
    while (ps != NULL) {
        if (ps->fd == fd) {
            if (parent != NULL) {
                parent->next = ps->next;
            } else {
                c->ps = ps->next;
            }
            free(ps);
            return afs_ok;
        }
        parent = ps;
        ps = ps->next;
    }
    SOB_AFS_FAIL_("fd not found (no errno)");
    return afs_fail_bad_fd;
}

static struct ps_ * ps_get_(struct afs_ctx * c, int fd)
{
    struct ps_ * ps = c->ps;
    while (ps != NULL) {
        if (ps->fd == fd) {
            return ps;
        }
        ps = ps->next;
    }
    return NULL;
}

static int ps_next_fd_(struct afs_ctx * c)
{
    int fd = 0;
    struct ps_ * ps = c->ps;
    while (ps != NULL) {
        if (ps->fd >= fd) {
            fd = ps->fd + 1;
        }
        ps = ps->next;
    }
    return fd;
}

static size_t ps_len_(struct afs_ctx * c)
{
    size_t r = 0;
    struct ps_ * ps = c->ps;
    while (ps != NULL) {
        r++;
        ps = ps->next;
    }
    return r;
}

static enum afs_res proc_init_(struct proc_ * p)
{
    int sv[2]; /* [0] for parent, [1] for child */
    pid_t pid;
    size_t pgs = sysconf(_SC_PAGESIZE);

    p->evs_len = 0;
    p->is_stop_req = 0;
    /* if something goes wrong we might accidentally kill ourselves
     * but not others; should never happen though */
    p->pid = 0;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        SOB_AFS_PROC_FAIL_("socketpair");
        return afs_fail;
    }
    fcntl(sv[0], F_SETFD, fcntl(sv[0], F_GETFD) | O_NONBLOCK);
    fcntl(sv[1], F_SETFD, fcntl(sv[1], F_GETFD) & ~O_NONBLOCK);

    /* make size a multiple of page size; mmap_len > 0 */
    p->mmap_len = sizeof(struct proc_shared_) + rw_buf_len_;
    p->mmap_len = ((p->mmap_len - 1) / pgs + 1) * pgs;
    p->mmap_start = mmap(NULL, p->mmap_len,
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (p->mmap_start == MAP_FAILED) {
        SOB_AFS_PROC_FAIL_("mmap");
        close(sv[0]);
        close(sv[1]);
        return afs_fail;
    }
    p->shared = (struct proc_shared_ *)p->mmap_start;
    p->shared->cmd = proc_cmd_none_;
    p->shared->res = proc_res_none_;
    p->shared->st = proc_child_st_not_started_;
    p->shared->write_len = 0;
    p->shared->open_flags = 0;
    p->shared->written = 0;
    p->shared->read_len = 0;

    p->rw_buf = ((char *) p->mmap_start) + sizeof(struct proc_shared_);
    p->rw_buf_len = p->mmap_len - sizeof(struct proc_shared_);

    pid = fork();
    if (pid == -1) {
        SOB_AFS_PROC_FAIL_("fork");
        close(sv[0]);
        close(sv[1]);
        munmap(p->mmap_start, p->mmap_len);
        p->mmap_start = NULL;
        p->mmap_len = 0;
        p->rw_buf = NULL;
        p->rw_buf_len = 0;
        p->shared = NULL;
        return afs_fail;
    } else if (pid == 0) { /* child */
        close(sv[0]);
        int r = proc_child_worker_(sv[1],
            p->shared, p->rw_buf, p->rw_buf_len);
        _exit(r);
    }
    /* parent continues */
    close(sv[1]);

    p->pid = pid;
    p->fd = sv[0];
    p->st = proc_st_init_pend_;

    /* ask child to notify us when started */
    p->shared->cmd = proc_cmd_none_;
    return proc_notify_child_(p);
}

static enum afs_res proc_open_(struct proc_ * p)
{
    return proc_send_cmd_(p, proc_cmd_open_);
}

static enum afs_res proc_close_(struct proc_ * p)
{
    return proc_send_cmd_(p, proc_cmd_close_);
}

static enum afs_res proc_fsync_(struct proc_ * p)
{
    return proc_send_cmd_(p, proc_cmd_fsync_);
}

static enum afs_res proc_write_(struct proc_ * p)
{
    return proc_send_cmd_(p, proc_cmd_write_);
}

static enum afs_res proc_readall_(struct proc_ * p)
{
    return proc_send_cmd_(p, proc_cmd_readall_);
}

static enum afs_res proc_mkdir_(struct proc_ * p)
{
    return proc_send_cmd_(p, proc_cmd_mkdir_);
}

static enum afs_res proc_update_(struct proc_ * p,
    const struct pollfd * fds, size_t fds_len)
{
    short revents = 0;
    if (p->fd != -1) {
        size_t i;
        for (i = 0; i < fds_len; i++) {
            if (fds[i].fd == p->fd) {
                revents = fds[i].revents;
            }
        }
    }

    p->evs_len = 0;

    switch (p->st) {
    case proc_st_init_pend_:
        return proc_update_init_pend_(p, revents);
    case proc_st_busy_:
        return proc_update_busy_(p, revents);
    case proc_st_avail_:
        return proc_update_avail_(p, revents);
    case proc_st_dead_:
        return proc_update_dead_(p);
    case proc_st_uninit_:
        /* should never get there although legal */
        if (p->is_stop_req) {
            p->is_stop_req = 0;
            SOB_AFS_PROC_CHECK_EV_(proc_add_ev_(p, afs_ev_stop));
        }
        SOB_AFS_PROC_FAIL_("uninit (no errno)");
        return afs_fail; /* child is terminated, nothing will work */
    };
    SOB_PANIC("unreacheable");
    return afs_fail;
}

static size_t proc_evs_(struct proc_ * p, struct afs_ev ** evs_out)
{
    *evs_out = p->evs;
    return p->evs_len;
}

static int proc_fd_(const struct proc_ * p)
{
    return p->fd;
}

static enum afs_res proc_stop_prep_(struct proc_ * p)
{
    if (! p->is_stop_req) {
        p->is_stop_req = 1;
        if (p->st == proc_st_avail_) {
            SOB_AFS_CHECK(proc_send_cmd_(p, proc_cmd_exit_));
            return afs_ok;
        } else if (p->st == proc_st_busy_) {
            return afs_ok; /* is_stop_req will send cmd_exit in update_busy_ */
        } else {
            SOB_AFS_PROC_FAIL_("bad st (no errno)");
            return afs_fail;
        }
    } else {
        SOB_AFS_PROC_FAIL_("stop_prep already pending (no errno)");
        return afs_fail;
    }
}

static enum afs_res proc_stop_(struct proc_ * p)
{
    /* actually stops in update_ */
    if (p->st != proc_st_uninit_) {
        SOB_AFS_PROC_FAIL_("not ready to stop (no errno)");
        return afs_fail;
    }
    return afs_ok;
}

static enum afs_res proc_update_init_pend_(struct proc_ * p, short revents)
{
    if (p->is_stop_req) {
        p->is_stop_req = 0;
        proc_destroy_child_(p);
        SOB_AFS_PROC_CHECK_EV_(proc_add_ev_(p, afs_ev_init_fail));
        SOB_AFS_PROC_CHECK_EV_(proc_add_ev_(p, afs_ev_stop));
        return afs_fail;
    }

    if (revents & POLLHUP || revents & POLLERR) {
        proc_destroy_child_(p);
        SOB_AFS_PROC_CHECK_EV_(proc_add_ev_(p, afs_ev_init_fail));
        SOB_AFS_PROC_FAIL_("child died (no errno)");
        return afs_fail;
    } else if (revents & POLLIN) {
        char throwaway;
        read(p->fd, &throwaway, 1);
        if (p->shared->res == proc_res_ok_) {
            p->st = proc_st_avail_;
            SOB_AFS_PROC_CHECK_EV_(proc_add_ev_(p, afs_ev_init));
            return afs_ok;
        } else { /* should never happen */
            proc_destroy_child_(p);
            SOB_AFS_PROC_CHECK_EV_(proc_add_ev_(p, afs_ev_init_fail));
            SOB_AFS_PROC_FAIL_("child failed cmd_none (no errno)");
            return afs_fail;
        }
    } else { /* still waiting */
        return afs_ok;
    }
}

static enum afs_res proc_update_avail_(struct proc_ * p, short revents)
{
    if (revents & POLLHUP || revents & POLLERR) {
        proc_destroy_child_(p);
        SOB_AFS_PROC_FAIL_("child died (no errno)");
        return afs_fail;
    } else {
        return afs_ok;
    }
}

static enum afs_res proc_update_busy_(struct proc_ * p, short revents)
{
    if (revents & POLLHUP || revents & POLLERR) {
        SOB_AFS_PROC_CHECK_EV_(
            proc_add_ev_(p, proc_cmd_fail_ev_(p->shared->cmd)));
        proc_destroy_child_(p); /* p->shared is NULL afterwards */
        SOB_AFS_PROC_FAIL_("child died (no errno)");
        return afs_fail;
    } else if (revents & POLLIN) {
        char throwaway;
        (void) read(p->fd, &throwaway, 1);

        if (p->shared->res == proc_res_ok_) {
            struct afs_ev * ev = NULL;
            if (p->shared->cmd != proc_cmd_none_) {
                ev = proc_add_ev_(p, afs_ev_init); /* this ty is unused */
                SOB_AFS_PROC_CHECK_EV_(ev);
            }
            switch (p->shared->cmd) {
                case proc_cmd_none_:
                    break; /* should never happen although legal */
                case proc_cmd_exit_:
                    p->is_stop_req = 0;
                    /* child should've already terminated at this point */
                    proc_destroy_child_(p);
                    if (p->is_stop_req) {
                        SOB_AFS_PROC_CHECK_EV_(
                            proc_add_ev_(p, afs_ev_stop));
                    }
                    SOB_AFS_PROC_FAIL_("exit successful (no errno)");
                    return afs_fail; /* because child is destroyed */
                case proc_cmd_open_:
                    ev->ty = afs_ev_open;
                    break;
                case proc_cmd_close_:
                    ev->ty = afs_ev_close;
                    break;
                case proc_cmd_fsync_:
                    ev->ty = afs_ev_fsync;
                    break;
                case proc_cmd_write_:
                    ev->ty = afs_ev_write;
                    ev->d.write.len = p->shared->written;
                    break;
                case proc_cmd_readall_:
                    ev->ty = afs_ev_readall;
                    ev->d.readall.len = p->shared->read_len;
                    ev->d.readall.data = p->rw_buf;
                    break;
                case proc_cmd_mkdir_:
                    ev->ty = afs_ev_mkdir;
                    break;
            };
            p->shared->cmd = proc_cmd_none_;
        } else {
            memcpy(&p->fail, &p->shared->fail, sizeof(struct sob_fail));
            /* gotta watch out for proc_cmd_exit_ so that it never fails */
            /* child is still intact so it's not afs_fail */
            SOB_AFS_PROC_CHECK_EV_(
                proc_add_ev_(p, proc_cmd_fail_ev_(p->shared->cmd)));
        }
        p->st = proc_st_avail_;

        if (p->is_stop_req) {
            SOB_AFS_CHECK(proc_send_cmd_(p, proc_cmd_exit_));
        }
        return afs_ok;
    } else {
        return afs_ok;
    }
}

static enum afs_res proc_update_dead_(struct proc_ * p)
{
    if (p->is_stop_req) {
        p->is_stop_req = 0;
        SOB_AFS_PROC_CHECK_EV_(proc_add_ev_(p, afs_ev_stop_fail));
    }
    proc_destroy_child_(p);
    SOB_AFS_PROC_FAIL_("child is dead (no errno)");
    return afs_fail;
}

static enum afs_res proc_send_cmd_(struct proc_ * p, enum proc_cmd_ cmd)
{
    if (p->st != proc_st_avail_) {
        SOB_AFS_PROC_FAIL_("bad st (no errno)");
        return afs_fail;
    }
    if (p->shared->st != proc_child_st_idle_) {
        /* should never happen */
        SOB_AFS_PROC_FAIL_("bad child st (no errno)");
        return afs_fail;
    }
    p->st = proc_st_busy_;
    p->shared->cmd = cmd;
    return proc_notify_child_(p);
}

static enum afs_res proc_notify_child_(struct proc_ * p)
{
    while (1) {
        char throwaway;
        if (write(p->fd, &throwaway, 1) == 1) {
            return afs_ok;
        } else if (errno == EINTR) {
            continue;
        } else {
            SOB_AFS_PROC_FAIL_("write");
            p->st = proc_st_dead_;
            return afs_fail;
        }
    }
}

static void proc_destroy_child_(struct proc_ * p)
{
    if (p->pid > 0) {
        (void) kill(p->pid, SIGKILL);
        /* MAY BLOCK; if so we have bigger problems though */
        while (1) {
            int wstatus;
            int r = waitpid(p->pid, &wstatus, 0);
            if (r == -1 && errno == EINTR) {
                continue;
            } else {
                /* even if waitpid fails the zombie is already reaped */
                break;
            }
        }
        p->pid = 0;
    }

    close(p->fd);
    p->fd = -1;
    munmap(p->mmap_start, p->mmap_len);
    p->mmap_start = NULL;
    p->mmap_len = 0;
    p->rw_buf = NULL;
    p->rw_buf_len = 0;
    p->shared = NULL;

    p->st = proc_st_uninit_;
}

static struct afs_ev * proc_add_ev_(struct proc_ * p, enum afs_event ty)
{
    if (p->evs_len < proc_evs_maxlen_) {
        struct afs_ev * r = &p->evs[p->evs_len];
        p->evs_len++;
        r->ty = ty;
        r->fd = -1; /* afs will fill fd (its internal, not actual fd) */
        return r;
    } else {
        return NULL;
    }
}

static int proc_child_worker_(int parent_fd,
    struct proc_shared_ * s, void * rw_buf, size_t rw_buf_len)
{
    /* child process; spawned in proc_init_ */

    int fd = -1;

    if (s->st != proc_child_st_not_started_) {
        /* sanity check */
        return 126;
    }
    s->st = proc_child_st_idle_;

    while (1) {
        char throwaway;
        int should_exit = 0;

        /* wait for signal from parent */
        if (read(parent_fd, &throwaway, 1) != 1) {
            if (errno == EINTR) {
                continue;
            } else {
                /* should not happen during normal operation */
                return 1;
            }
        }

        s->st = proc_child_st_busy_;
        s->res = proc_res_none_;

        switch (s->cmd) {
        case proc_cmd_none_:
            s->res = proc_res_ok_;
            break;
        case proc_cmd_exit_:
            /* should never fail.
             * if does so in the future, process should still be useful */
            should_exit = 1;
            s->res = proc_res_ok_;
            break;
        case proc_cmd_open_:
            s->res = proc_child_open_(s, &fd, rw_buf, rw_buf_len);
            break;
        case proc_cmd_close_:
            s->res = proc_child_close_(s, &fd);
            break;
        case proc_cmd_fsync_:
            s->res = proc_child_fsync_(s, fd);
            break;
        case proc_cmd_write_:
            s->res = proc_child_write_(s, fd, rw_buf, rw_buf_len);
            break;
        case proc_cmd_readall_:
            s->res = proc_child_readall_(s, fd, rw_buf, rw_buf_len);
            break;
        case proc_cmd_mkdir_:
            s->res = proc_child_mkdir_(s, rw_buf, rw_buf_len);
            break;
        };

        s->st = proc_child_st_idle_;

        while (1) {
            /* notify parent */
            if (write(parent_fd, &throwaway, 1) != 1) {
                if (errno == EINTR) {
                    continue;
                } else {
                    /* should not happen during normal operation */
                    SOB_AFS_PROC_C_FAIL_("write");
                    s->st = proc_child_st_not_started_;
                    return 127;
                }
            }
            break;
        }

        if (should_exit) {
            s->st = proc_child_st_not_started_;
            return 0;
        }
    }

    /* should never get there */
    SOB_AFS_PROC_C_FAIL_("unexpected exit");
    return 128;
}

static enum proc_res_ proc_child_open_(struct proc_shared_ * s, int * fd,
    void * rw_buf, size_t rw_buf_len)
{
    if (*fd != -1) {
        SOB_AFS_PROC_C_FAIL_("already open (no errno)");
        return proc_res_fail_;
    } else {
        *fd = open((const char *) rw_buf, s->open_flags, 00600);
        if (*fd == -1) {
            SOB_AFS_PROC_C_FAIL_("open");
            return proc_res_fail_;
        } else {
            return proc_res_ok_;
        }
    }
}

static enum proc_res_ proc_child_close_(struct proc_shared_ * s, int * fd)
{
    if (*fd == -1) {
        SOB_AFS_PROC_C_FAIL_("not open (no errno)");
        return proc_res_fail_;
    } else {
        close(*fd);
        *fd = -1;
        return proc_res_ok_;
    }
}

static enum proc_res_ proc_child_fsync_(struct proc_shared_ * s, int fd)
{
    if (fd == -1) {
        SOB_AFS_PROC_C_FAIL_("not open (no errno)");
        return proc_res_fail_;
    } else {
        if (fsync(fd) == -1) {
            SOB_AFS_PROC_C_FAIL_("fsync");
            return proc_res_fail_;
        } else {
            return proc_res_ok_;
        }
    }
}

static enum proc_res_ proc_child_write_(struct proc_shared_ * s, int fd,
    void * rw_buf, size_t rw_buf_len)
{
    if (fd == -1) {
        SOB_AFS_PROC_C_FAIL_("not open (no errno)");
        return proc_res_fail_;
    } else {
        const char * write_buf = rw_buf;
        size_t write_len = s->write_len;
        s->written = 0;
        while (1) {
            ssize_t written = write(fd, write_buf, write_len);
            if (written == write_len) {
                s->written = written;
                return proc_res_ok_;
            } else if (written != -1) { /* interrupted */
                write_buf += written;
                write_len -= written;
                s->written += written;
                continue;
            } else {
                if (errno == EINTR) {
                    continue; /* no data was written */
                } else {
                    SOB_AFS_PROC_C_FAIL_("write");
                    return proc_res_fail_;
                }
            }
        }
        /* unreacheable */
    }
}

static enum proc_res_ proc_child_readall_(struct proc_shared_ * s, int fd,
    void * rw_buf, size_t rw_buf_len)
{
    if (fd == -1) {
        SOB_AFS_PROC_C_FAIL_("not open (no errno)");
        return proc_res_fail_;
    } else {
        s->read_len = 0;
        ssize_t read_len = read(fd, rw_buf, rw_buf_len);
        if (read_len < 0) {
            SOB_AFS_PROC_C_FAIL_("read");
            return proc_res_fail_;
        } else {
            s->read_len = read_len;
            return proc_res_ok_;
        }
    }
}

static enum proc_res_ proc_child_mkdir_(struct proc_shared_ * s,
    void * rw_buf, size_t rw_buf_len)
{
    int dirfd;
    int openflags;
    char * parent;

    if (mkdir(rw_buf, 00700) != 0) {
        if (errno == EEXIST) {
            struct stat dstat;
            if (stat(rw_buf, &dstat) != 0) {
                SOB_AFS_PROC_C_FAIL_("stat after EEXIST");
                return proc_res_fail_;
            }
            if ((dstat.st_mode & S_IFMT) != S_IFDIR) {
                SOB_AFS_PROC_C_FAIL_("file with same name as dir (no errno)");
                return proc_res_fail_;
            } else {
                return proc_res_ok_;
            }
        } else {
            SOB_AFS_PROC_C_FAIL_("mkdir");
            return proc_res_fail_;
        }
    }

    openflags = O_RDONLY;
#ifdef O_DIRECTORY
    openflags |= O_DIRECTORY;
#endif
    /* XXX: unsure if necessary to fsync the dir itself */
    dirfd = open(rw_buf, openflags); 
    if (dirfd == -1) {
        SOB_AFS_PROC_C_FAIL_("open dir");
        return proc_res_fail_;
    }
    if (fsync(dirfd) != 0) {
        close(dirfd);
        SOB_AFS_PROC_C_FAIL_("fsync dir");
        return proc_res_fail_;
    }
    close(dirfd);

    parent = realpath(rw_buf, NULL);
    if (parent == NULL) {
        SOB_AFS_PROC_C_FAIL_("realpath");
        return proc_res_fail_;
    }
    dirfd = open(parent, openflags); 
    free(parent);
    if (dirfd == -1) {
        SOB_AFS_PROC_C_FAIL_("open dir");
        return proc_res_fail_;
    }
    if (fsync(dirfd) != 0) {
        close(dirfd);
        SOB_AFS_PROC_C_FAIL_("fsync dir");
        return proc_res_fail_;
    }
    close(dirfd);
    return proc_res_ok_;
}

static enum afs_event proc_cmd_fail_ev_(enum proc_cmd_ cmd)
{
    switch (cmd) {
    case proc_cmd_none_:
        return afs_ev_init_fail; /* should never happen */
    case proc_cmd_exit_:
        return afs_ev_stop_fail;
    case proc_cmd_open_:
        return afs_ev_open_fail;
    case proc_cmd_close_:
        return afs_ev_close_fail;
    case proc_cmd_fsync_:
        return afs_ev_fsync_fail;
    case proc_cmd_write_:
        return afs_ev_write_fail;
    case proc_cmd_readall_:
        return afs_ev_readall_fail;
    case proc_cmd_mkdir_:
        return afs_ev_mkdir_fail;
    };
    SOB_PANIC("unreacheable");
    return afs_ev_init_fail;
}

#undef SOB_AFS_FAIL_
#undef SOB_AFS_PROC_FAIL_
#undef SOB_AFS_PROC_C_FAIL_
#undef SOB_AFS_PROC_CHECK_EV_


#ifdef SOB_AFS_DEMO

#include <stdio.h>

#define SOB_AFS_DEMO_PRINT_FAIL_(c) \
    do { \
        struct sob_fail * SOB_AFS_DEMO_PRINT_FAIL_fail_ = afs_get_fail(c); \
        fprintf(stderr, "fail: %s:%i: %s (%s)\n", \
            SOB_AFS_DEMO_PRINT_FAIL_fail_->file, \
            SOB_AFS_DEMO_PRINT_FAIL_fail_->line, \
            SOB_AFS_DEMO_PRINT_FAIL_fail_->msg, \
            strerror(SOB_AFS_DEMO_PRINT_FAIL_fail_->the_errno)); \
    } while (0)
#define SOB_AFS_DEMO_CHECK_(stmt) \
    do { \
        enum afs_res SOB_AFS_DEMO_CHECK_res_ = (stmt); \
        if (SOB_AFS_DEMO_CHECK_res_ != afs_ok) { \
            SOB_AFS_DEMO_PRINT_FAIL_(c); \
            SOB_PANIC("fail %i @ '%s'", SOB_AFS_DEMO_CHECK_res_, #stmt); \
        } \
    } while (0)

static ssize_t update_(int line, struct afs_ctx * c, struct afs_ev ** evs_out)
{
    while (1) {
        struct pollfd * fds;
        size_t fds_len = afs_pollfds(c, &fds);
        if (fds_len > 0) {
            size_t evs_len;
            if (poll(fds, fds_len, -1) == -1) {
                SOB_PANIC("poll: %s; line %i", strerror(errno), line);
            }
            afs_update(c, fds, fds_len);
            evs_len = afs_evs(c, evs_out);
            if (evs_len > 0) {
                size_t i;
                int is_fail = 0;
                struct afs_ev * evs = *evs_out;
                for (i = 0; i < evs_len; i++) {
                    if (afs_ev_is_fail(&evs[i])) {
                        fprintf(stderr, "ev: %s for fd %i (fail); line %i\n",
                            afs_event_str(evs[i].ty), evs[i].fd, line);
                        is_fail = 1;
                    } else {
                        fprintf(stderr, "ev: %s for fd %i; line %i\n",
                            afs_event_str(evs[i].ty), evs[i].fd, line);
                    }
                }
                if (is_fail) {
                    SOB_AFS_DEMO_PRINT_FAIL_(c);
                    SOB_PANIC("fail; line %i", line);
                }
                return evs_len;
            } else {
                continue;
            }
        } else {
            return -1;
        }
    }
}

static void wait_evs_(int line, struct afs_ctx * c,
    struct afs_ev * evs_inout, size_t evs_inout_len)
{
    struct afs_ev * evs;
    size_t remain_evs = evs_inout_len;
    while (remain_evs > 0) {
        ssize_t evs_len = update_(line, c, &evs);
        if (evs_len < 0) {
            SOB_PANIC("some events never fired; line %i", line);
        }
        while (evs_len > 0 && evs != NULL) {
            size_t i;
            for (i = 0; i < evs_inout_len; i++) {
                if (evs->ty == evs_inout[i].ty) {
                    memcpy(&evs_inout[i], evs, sizeof(struct afs_ev));
                    remain_evs--;
                    break;
                }
            }
            evs_len--;
            evs++;
        }
    }
}

#define SOB_AFS_DEMO_WAIT_EVS_(c, evs_inout, evs_inout_len) \
    wait_evs_(__LINE__, (c), (evs_inout), (evs_inout_len))

int main(int argc, char ** argv) {
    const char * path_a;
    const char * path_b;
    int fd_a;
    int fd_b;
    int mkdir_fd;
    struct afs_ctx c_;
    struct afs_ctx * c = &c_;
    struct afs_ev evs[10];
    void * b_rw_buf = NULL;
    size_t b_rw_buf_len = 0;
    int should_wait_write = 0;

    if (argc != 3) {
        fprintf(stderr, "pass source path and dest path\n");
        return 1;
    }
    path_a = argv[1];
    path_b = argv[2];

    afs_init(c);

    SOB_AFS_DEMO_CHECK_(
        afs_mkdir(c, "/tmp/SOB_AFS_DEMO", &mkdir_fd));
    SOB_AFS_DEMO_CHECK_(
        afs_open(c, path_a, O_RDONLY | O_NOCTTY, &fd_a));
    SOB_AFS_DEMO_CHECK_(
        afs_open(c, path_b, O_RDWR | O_CREAT | O_NOCTTY, &fd_b));
    evs[0].ty = afs_ev_open;
    evs[1].ty = afs_ev_open;
    evs[2].ty = afs_ev_mkdir;
    SOB_AFS_DEMO_WAIT_EVS_(c, evs, 3);

    while (1) {
        SOB_AFS_DEMO_CHECK_(afs_readall(c, fd_a));
        evs[0].ty = afs_ev_readall;
        if (should_wait_write) {
            evs[1].ty = afs_ev_write;
        }
        SOB_AFS_DEMO_WAIT_EVS_(c, evs, should_wait_write ? 2 : 1);
        should_wait_write = 0;
        if (evs[0].d.readall.len == 0) {
            break;
        }

        SOB_AFS_DEMO_CHECK_(afs_get_rw_buf(c, fd_b, &b_rw_buf, &b_rw_buf_len));
        if (b_rw_buf_len < evs[0].d.readall.len) {
            SOB_PANIC("buf too short");
            return 1; /* unreacheable */
        }
        memcpy(b_rw_buf, evs[0].d.readall.data, evs[0].d.readall.len);
        SOB_AFS_DEMO_CHECK_(afs_write(c, fd_b, evs[0].d.readall.len));
        should_wait_write = 1;
    }

    SOB_AFS_DEMO_CHECK_(afs_fsync(c, fd_b));
    evs[0].ty = afs_ev_fsync;
    SOB_AFS_DEMO_WAIT_EVS_(c, evs, 1);

    SOB_AFS_DEMO_CHECK_(afs_close(c, fd_a));
    SOB_AFS_DEMO_CHECK_(afs_close(c, fd_b));
    SOB_AFS_DEMO_CHECK_(afs_stop_prep(c));
    evs[0].ty = afs_ev_close;
    evs[1].ty = afs_ev_close;
    evs[2].ty = afs_ev_stop;
    SOB_AFS_DEMO_WAIT_EVS_(c, evs, 3);

    SOB_AFS_DEMO_CHECK_(afs_stop(c));

    return 0;
}

#undef SOB_AFS_DEMO_CHECK_
#undef SOB_AFS_DEMO_WAIT_EVS_
#undef SOB_AFS_DEMO_WAIT_EV_
#undef SOB_AFS_DEMO_PRINT_FAIL_

#endif /* SOB_AFS_DEMO */

