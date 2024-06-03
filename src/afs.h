#ifndef SOB_AFS_H_SENTRY
#define SOB_AFS_H_SENTRY

#include "fail.h"

#include <stddef.h>
#include <poll.h> /* for pollfd */

#define SOB_AFS_CHECK(stmt) \
    do { \
        enum afs_res SOB_AFS_CHECK_res_ = (stmt); \
        if (SOB_AFS_CHECK_res_ != afs_ok) { \
            return SOB_AFS_CHECK_res_; \
        } \
    } while (0)

struct afs_ctx;

struct afs_ev;
enum afs_event {
    afs_ev_init,
    afs_ev_init_fail,
    afs_ev_stop,
    afs_ev_stop_fail,
    afs_ev_open,
    afs_ev_open_fail,
    afs_ev_close,
    afs_ev_close_fail,
    afs_ev_fsync,
    afs_ev_fsync_fail,
    afs_ev_write,
    afs_ev_write_fail,
    afs_ev_readall,
    afs_ev_readall_fail
};

enum afs_res {
    afs_fail_bad_arg = -4,
    afs_fail_bad_fd = -3,
    afs_fail_alloc = -2,
    afs_fail = -1,
    afs_ok = 1
};

struct sob_fail * afs_get_fail(struct afs_ctx * c);

/* no afs_ev_init or afs_ev_init_fail */
void afs_init(struct afs_ctx * c);

enum afs_res afs_open(struct afs_ctx * c,
    const char * path, int flags, int * afs_fd_out);

enum afs_res afs_close(struct afs_ctx * c, int fd_from_afs);

enum afs_res afs_fsync(struct afs_ctx * c, int fd_from_afs);

/* used as path for afs_open, buffer for afs_write and afs_readall */
enum afs_res afs_get_rw_buf(struct afs_ctx * c,
    int fd_from_afs, void ** buf_out, size_t * len_out);

enum afs_res afs_write(struct afs_ctx * c, int fd_from_afs, size_t len);

enum afs_res afs_readall(struct afs_ctx * c, int fd_from_afs);

enum afs_res afs_stop_prep(struct afs_ctx * c);

enum afs_res afs_stop(struct afs_ctx * c);

void afs_update(struct afs_ctx * c,
    struct pollfd * fds, size_t fds_len);

size_t afs_pollfds(struct afs_ctx * c, struct pollfd ** fds_out);

size_t afs_evs(struct afs_ctx * c, struct afs_ev ** evs_out);


enum afs_event afs_ev_ty(const struct afs_ev * ev);

int afs_ev_is_fail(const struct afs_ev * ev);

int afs_ev_fd(const struct afs_ev * ev);

size_t afs_ev_write_len(const struct afs_ev * ev);

size_t afs_ev_readall_len(const struct afs_ev * ev);

const char * afs_ev_readall_data(const struct afs_ev * ev);

const char * afs_event_str(enum afs_event event);

#endif /* SOB_AFS_H_SENTRY */

