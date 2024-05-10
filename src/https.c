#include <unistd.h> /* for close */
#include <stdio.h> /* for perror */
#include <string.h> /* for memcpy */
#include <sys/timerfd.h> /* XXX: linux specific, not portable */

#include <curl/curl.h>

#include "https.h"
#include "panic.h"

enum {
    /* arbitrary */

    evs_maxlen_ = 12,
    fds_maxlen_ = 24
};

struct https_ev {
    enum https_ev_type ty;
};

enum set_fd_res_ {
    set_fd_add_overflow_ = -1,
    set_fd_ok_ = 1
} set_fd_(struct https_mod * m, int fd, short events);

enum del_fd_res_ {
    del_fd_not_found_ = -1,
    del_fd_ok_ = 1
} del_fd_(struct https_mod * m, int fd);

enum add_ev_res_ {
    add_ev_overflow_ = -1,
    add_ev_ok_ = 1
} add_ev_(struct https_mod * m, enum https_ev_type ty);

enum st_ {
    st_uninit_ = 0,
    st_just_init_,
    st_idle_,
    st_pend_,
    st_stopped_,
    st_err_
};

enum set_st_res_ {
    set_st_ok_ = 1
} set_st_(struct https_mod * m, enum st_ st);

int sock_cb_(CURL * h, curl_socket_t sock, int what,
        void * data, void * fd_data);
int timer_cb_(CURLM * h, long timeout_ms, void * data);
int write_cb_(char * data, size_t throwaway, size_t len, void * user_data);

struct https_mod {
    enum st_ st;
    CURLcode curl_err; /* check when st_err_ */
    CURLMcode curlm_err; /* check when st_err_ */

    struct https_ev evs[evs_maxlen_];
    size_t evs_len;

    char * resp_out;
    size_t resp_mlen;
    size_t resp_len;
    long resp_status;

    struct pollfd fds[fds_maxlen_];
    size_t fds_len;
    int timerfd;

    CURL * curl;
    CURLM * curlm;

    struct curl_slist * json_hdrs;
};

enum https_init_res https_init(struct https_mod * m,
        char * resp_out, size_t resp_maxlen) {
    m->st = st_just_init_;
    m->curl_err = CURLE_OK;
    m->curlm_err = CURLM_OK;
    m->evs_len = 0;

    m->resp_out = resp_out;
    m->resp_mlen = resp_maxlen;
    m->resp_len = 0;

    if (m->resp_mlen > 0) {
        m->resp_out[0] = '\0';
        m->resp_mlen -= 1; /* for '\0' */
    }

    m->resp_status = 0;

    m->curl = curl_easy_init();
    if (m->curl == NULL) {
        return https_init_fail_curl_easy_init;
    }
    m->curlm = curl_multi_init();
    if (m->curlm == NULL) {
        curl_easy_cleanup(m->curl);
        return https_init_fail_curl_multi_init;
    }

    m->json_hdrs = NULL;
    m->json_hdrs = curl_slist_append(m->json_hdrs,
            "Content-Type: application/json");
    m->json_hdrs = curl_slist_append(m->json_hdrs,
            "Accept: application/json");
    if (m->json_hdrs == NULL) {
        /* XXX: may leak the first slist node in json_hdrs */
        curl_easy_cleanup(m->curl);
        curl_multi_cleanup(m->curlm);
        return https_init_fail_no_mem;
    }

    curl_easy_setopt(m->curl, CURLOPT_POSTFIELDSIZE, -1L);
    curl_easy_setopt(m->curl, CURLOPT_HTTPHEADER, m->json_hdrs);
    curl_easy_setopt(m->curl, CURLOPT_WRITEDATA, m);
    curl_easy_setopt(m->curl, CURLOPT_WRITEFUNCTION, write_cb_);

    curl_multi_setopt(m->curlm, CURLMOPT_SOCKETDATA, m);
    curl_multi_setopt(m->curlm, CURLMOPT_SOCKETFUNCTION, sock_cb_);
    curl_multi_setopt(m->curlm, CURLMOPT_TIMERDATA, m);
    curl_multi_setopt(m->curlm, CURLMOPT_TIMERFUNCTION, timer_cb_);

    m->timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (-1 == m->timerfd) {
        perror("timerfd_create"); /* XXX: should not print */
        return https_init_fail_timerfd;
    }

    m->fds_len = 0;

    return https_init_ok;
}

enum https_stop_prep_res https_stop_prep(struct https_mod * m) {
    if (m->st == st_pend_) {
        curl_multi_remove_handle(m->curlm, m->curl);
    }

    if (set_st_(m, st_stopped_) != set_st_ok_) {
        SOB_PANIC("set_st_(st_stopped_) in https_stop_prep");
    }
    return https_stop_prep_ok;
}

enum https_stop_res https_stop(struct https_mod * m) {
    m->st = st_uninit_;
    curl_easy_cleanup(m->curl);
    curl_multi_cleanup(m->curlm);
    curl_slist_free_all(m->json_hdrs);
    close(m->timerfd);
    return https_stop_ok;
}

size_t https_pollfds(struct https_mod * m, struct pollfd ** fds_out) {
    *fds_out = m->fds;
    if (m->st == st_pend_) {
        return m->fds_len;
    } else {
        return 0;
    }
}

void req_fail_(struct https_mod * m) {
    if (set_st_(m, st_err_) != set_st_ok_) {
        SOB_PANIC("set_st_(st_err_) in update");
    }
    if (add_ev_(m, https_ev_req_fail) != add_ev_ok_) {
        SOB_PANIC("add_ev_(https_ev_req_fail) in update");
    }
}

void https_update(struct https_mod * m, struct pollfd * fds, nfds_t nfds) {
    int i;
    CURLMcode cmres = CURLM_OK;
    struct CURLMsg * cmsg = NULL;
    int still_running;

    m->resp_len = 0;
    m->evs_len = 0;

    if (m->st == st_stopped_) {
        if (add_ev_(m, https_ev_stopped) != add_ev_ok_ ) {
            SOB_PANIC("add_ev_(https_ev_stopped)");
        }
        return;
    }

    if (m->st == st_just_init_) {
        if (set_st_(m, st_idle_) != set_st_ok_) {
            SOB_PANIC("set_st_(st_err_) in update");
        }
        if (add_ev_(m, https_ev_init) != add_ev_ok_ ) {
            SOB_PANIC("add_ev_(https_ev_init)");
        }
        return;
    }

    if (m->st != st_pend_) {
        return;
    }

    for (i = 0; i < nfds; i++) {
        const struct pollfd * fd = &fds[i];
        if (fd->fd == m->timerfd) {
            if (fd->revents & POLLIN) { /* timer fired */
                /* drain the buffer */
                uint64_t throwaway;
                if (read(fd->fd, &throwaway, 8) < 0) {
                    /* XXX: should not print */
                    perror("read(timerfd) ignored");
                }

                cmres = curl_multi_socket_action(m->curlm,
                        CURL_SOCKET_TIMEOUT, 0, &still_running);
            }
        } else {
            int cev = 0;
            switch (fd->revents) {
            case POLLIN:
                cev |= CURL_CSELECT_IN;
                /* fallthrough */
            case POLLOUT:
                cev |= CURL_CSELECT_OUT;
                /* fallthrough */
            case POLLERR:
            case POLLHUP:
            case POLLNVAL:
                cev |= CURL_CSELECT_ERR;
                /* fallthrough */
            };
            cmres = curl_multi_socket_action(m->curlm,
                    fd->fd, cev, &still_running);
        }

        if (cmres == CURLM_OK) {
            do {
                int qlen = 0;
                cmsg = curl_multi_info_read(m->curlm, &qlen);
                if (cmsg != NULL && cmsg->msg == CURLMSG_DONE) {
                    if (cmsg->data.result == CURLE_OK) {
                        curl_easy_getinfo(m->curl,
                                CURLINFO_RESPONSE_CODE, &m->resp_status);
                        if (set_st_(m, st_idle_) != set_st_ok_) {
                            SOB_PANIC("set_st_(st_idle_) in update");
                        }
                        if (add_ev_(m, https_ev_req_fin) != add_ev_ok_ ) {
                            SOB_PANIC("add_ev_(https_ev_req_fin) in update");
                        }
                        curl_multi_remove_handle(m->curlm, m->curl);

                        return;
                    } else {
                        m->curl_err = cmsg->data.result;
                        req_fail_(m);
                    }
                }
            } while (cmsg);
        }

        if (cmres != CURLM_OK) {
            m->curlm_err = cmres;
            req_fail_(m);

            return;
        }
    }
}

size_t https_events(struct https_mod * m, struct https_ev ** evs_out) {
    *evs_out = m->evs;
    return m->evs_len;
}

void https_set_timeout(struct https_mod * m, long timeout_s) {
    curl_easy_setopt(m->curl, CURLOPT_TIMEOUT, timeout_s);
}

void https_set_verbosity(struct https_mod * m, enum https_verbosity level) {
    switch (level) {
    case https_verbosity_silent:
        curl_easy_setopt(m->curl, CURLOPT_VERBOSE, 0L);
        break;
    case https_verbosity_debug:
        curl_easy_setopt(m->curl, CURLOPT_VERBOSE, 1L);
        break;
    };
}

enum https_req_res https_req_json(struct https_mod * m,
        enum https_req_method method, const char * url,
        const char * data) {
    if (m->st != st_idle_ ) {
        return https_req_fail_other_pend;
    }

    switch (method) {
        case https_method_get:
            curl_easy_setopt(m->curl, CURLOPT_HTTPGET, 1L);
            break;
        case https_method_post:
            curl_easy_setopt(m->curl, CURLOPT_POST, 1L);
            break;
    };
    curl_easy_setopt(m->curl, CURLOPT_URL, url);
    curl_easy_setopt(m->curl, CURLOPT_POSTFIELDS, data);

    curl_multi_add_handle(m->curlm, m->curl);

    if (set_st_(m, st_pend_) != set_st_ok_) {
        SOB_PANIC("set_st_(st_pend_) in https_req_json");
    }

    return https_req_ok;
}

long https_resp_status(const struct https_mod * m) {
    return m->resp_status;
}

size_t https_resp_len(const struct https_mod * m) {
    return m->resp_len;
}

char * https_resp_data(struct https_mod * m) {
    return m->resp_out;
}


enum https_ev_type https_ev_ty(const struct https_ev * ev) {
    return ev->ty;
}


int sock_cb_(CURL * h, curl_socket_t fd, int what,
        void * data, void * fd_data) {
    struct https_mod * m = data;

    (void) h;
    (void) fd_data;

    if (fd == m->timerfd) {
        SOB_PANIC("sock_cb_: fd == timerfd but curl doesn't know about timer");
    }

    if (what == CURL_POLL_REMOVE) {
        if (del_fd_ok_ != del_fd_(m, fd)) {
            /* XXX: should not be printing anything */
            fprintf(stderr, "WTF: %s:%i del_fd_\n", __FILE__, __LINE__);
            return -1;
        }
    } else {
        short events = 0;
        switch (what) {
            case CURL_POLL_IN:
                events = POLLIN;
                break;
            case CURL_POLL_OUT:
                events = POLLOUT;
                break;
            case CURL_POLL_INOUT:
                events = POLLIN | POLLOUT;
                break;
        };
        if (set_fd_ok_ != set_fd_(m, fd, events)) {
            fprintf(stderr, "WTF: %s:%i set_fd_\n", __FILE__, __LINE__);
            return -1;
        }
    }

    return 0;
}

int timer_cb_(CURLM * h, long timeout_ms, void * data) {
    struct https_mod * m = data;

    struct itimerspec its;
    /* expire once */
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;

    (void) h;

    if (timeout_ms >= 0) {
        if (timeout_ms > 0) {
            its.it_value.tv_sec = (timeout_ms / 1000);
            its.it_value.tv_nsec = (timeout_ms % 1000) * 1000 * 1000;
        } else if (timeout_ms == 0) {
            /* curl wants to timeout now, but setting both fields
             * of it_value to 0 disarms the timer.
             * Closest is timeout in 1 nsec */
            its.it_value.tv_sec = 0;
            its.it_value.tv_nsec = 1;
        }

        set_fd_(m, m->timerfd, POLLIN);
    } else {
        /* disarm the timer */
        its.it_value.tv_sec = 0;
        its.it_value.tv_nsec = 0;

        (void) del_fd_(m, m->timerfd);
    }

    if (timerfd_settime(m->timerfd, 0, &its, NULL) == -1) {
        perror("timerfd_settime"); /* XXX: should not print */
        return -1;
    }

    return 0;
}

int write_cb_(char * data, size_t throwaway, size_t len, void * user_data) {
    struct https_mod * m = user_data;
    size_t remain_len = m->resp_mlen - m->resp_len; /* mlen alwasy >= len */
    size_t write_len = 0;

    (void) throwaway; /* "size" in man, always 1 */

    if (len <= remain_len) {
        write_len = len;
    } else { /* len > remain_len */
        write_len = remain_len;
    }

    memcpy((m->resp_out + m->resp_len), data, write_len);
    m->resp_len += write_len;
    if (m->resp_mlen > 0) {
        m->resp_out[m->resp_len + 1] = '\0';
    }

    if (add_ev_(m, https_ev_req_data) != add_ev_ok_ ) {
        SOB_PANIC("add_ev_(https_ev_req_data) in write_cb_");
    }

    return write_len;
}


enum set_st_res_ set_st_(struct https_mod * m, enum st_ st) {
    m->st = st;
    return set_st_ok_;
}

int find_fd_(struct https_mod * m, int fd) {
    int i;
    for (i = 0; i < m->fds_len; i++) {
        if (m->fds[i].fd == fd) {
            return i;
        }
    }
    return -1;
}

int add_fd_(struct https_mod * m, int fd, short events) {
    if (m->fds_len < fds_maxlen_) {
        const int idx = m->fds_len;
        m->fds[idx].fd = fd;
        m->fds[idx].events = events;
        m->fds[idx].revents = 0;
        m->fds_len++;
        return idx;
    } else {
        return -1;
    }
}

enum set_fd_res_ set_fd_(struct https_mod * m, int fd, short events) {
    int pos = find_fd_(m, fd);
    if (-1 == pos) { /* not found */
        pos = add_fd_(m, fd, events);
        if (-1 == pos) { /* all slots taken */
            return set_fd_add_overflow_;
        } else {
            /* fd was added */
            return set_fd_ok_;
        }
    } else {
        m->fds[pos].events = events;
        return set_fd_ok_;
    }
}

enum del_fd_res_ del_fd_(struct https_mod * m, int fd) {
    int pos = find_fd_(m, fd);
    if (-1 == pos) {
        return del_fd_not_found_;
    } else {
        int i;
        for (i = pos + 1; i < m->fds_len; i++) {
            m->fds[i - 1] = m->fds[i];
        }
        m->fds_len--;
        return del_fd_ok_;
    }
}

enum add_ev_res_ add_ev_(struct https_mod * m, enum https_ev_type ty) {
    if (evs_maxlen_ == m->evs_len) {
        return add_ev_overflow_;
    }

    m->evs[m->evs_len].ty = ty;
    m->evs_len++;

    return add_ev_ok_;
}


#ifdef SOB_HTTPS_DEMO

#include <stdio.h>
#include <poll.h>

enum {
    evs_maxlen = 5,
    resp_data_maxlen = 4096
};

int main(void) {
    int i;
    struct https_mod m;
    char data[resp_data_maxlen];
    struct pollfd * fds;

    if (https_init(&m, data, resp_data_maxlen) != https_init_ok ) {
        fprintf(stderr, "cannot init\n");
        return 1;
    }

    https_set_verbosity(&m, https_verbosity_debug);
    https_set_timeout(&m, 2);

    while (1) {
        struct https_ev * evs;
        size_t evs_len = 0;

        nfds_t num_fds = https_pollfds(&m, &fds);
        if (num_fds > 0) {
            if (poll(fds, num_fds, -1) == -1) {
                perror("poll");
                return 1;
            }
        }
        https_update(&m, fds, num_fds);
        evs_len = https_events(&m, &evs);
        for (i = 0; i < evs_len; i++) {
            switch (https_ev_ty(&evs[i])) {
            case https_ev_init:
                printf("init\n");
                goto init;
            case https_ev_init_fail:
                printf("init fail\n");
                return 1;
            default:
                continue;
            };
        }
    }

init:

    if (https_req_json(&m,
            https_method_get,
            "https://echo.free.beeceptor.com",
            "{\"a\": 42, \"b\": \"hi\"}") != https_req_ok ) {
        fprintf(stderr, "cannot req_json\n");
        return 1;
    }

    while (1) {
        struct https_ev * evs;
        size_t evs_len = 0;

        nfds_t num_fds = https_pollfds(&m, &fds);
        if (num_fds > 0) {
            if (poll(fds, num_fds, -1) == -1) {
                perror("poll");
                return 1;
            }
        }
        https_update(&m, fds, num_fds);
        evs_len = https_events(&m, &evs);
        for (i = 0; i < evs_len; i++) {
            switch (https_ev_ty(&evs[i])) {
            case https_ev_req_data:
                printf("recv: '%s'\n", data);
                break;
            case https_ev_req_fin:
                printf("req completed\n");
                goto req_done;
            case https_ev_req_fail:
                fprintf(stderr, "https_ev_req_fail\n");
                goto req_fail;
            default:
                continue;
            };
        }
    }

req_done:

    {
        long status = https_resp_status(&m);
        printf("status: %li\n", status);
    }

req_fail:

    if (https_stop_prep(&m) != https_stop_prep_ok) {
        fprintf(stderr, "cannot stop_prep\n");
        return 1;
    }

    while (1) {
        struct https_ev * evs;
        size_t evs_len = 0;

        nfds_t num_fds = https_pollfds(&m, &fds);
        if (num_fds > 0) {
            if (poll(fds, num_fds, -1) == -1) {
                perror("poll");
                return 1;
            }
        }
        https_update(&m, fds, num_fds);
        evs_len = https_events(&m, &evs);
        for (i = 0; i < evs_len; i++) {
            switch (https_ev_ty(&evs[i])) {
            case https_ev_stopped:
                goto stopped;
            default:
                continue;
            };
        }
    }

stopped:

    if (https_stop_ok != https_stop(&m)) {
        fprintf(stderr, "cannot stop\n");
        return 1;
    }

    return 0;
}

#endif /* SOB_HTTPS_DEMO */

