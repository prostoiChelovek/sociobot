#ifndef SOB_FAIL_H_SENTRY
#define SOB_FAIL_H_SENTRY

struct sob_fail {
    const char * file;
    int line;
    int errno;
    const char * msg;
};

#define SOB_FAIL_INIT(msg) { __FILE__, __LINE__, errno, msg }

#endif /* SOB_FAIL_H_SENTRY */

