#ifndef SOB_FAIL_H_SENTRY
#define SOB_FAIL_H_SENTRY

#include <errno.h>

struct sob_fail {
    const char * file;
    int line;
    int the_errno;
    const char * msg;
};

#define SOB_FAIL_INIT(fail, the_msg) \
    do { \
        struct sob_fail * SOB_FAIL_INIT_f_ = (fail); \
        SOB_FAIL_INIT_f_->file = __FILE__; \
        SOB_FAIL_INIT_f_->line = __LINE__; \
        SOB_FAIL_INIT_f_->the_errno = errno; \
        SOB_FAIL_INIT_f_->msg = the_msg; \
    } while (0)

#endif /* SOB_FAIL_H_SENTRY */

