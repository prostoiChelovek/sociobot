#ifndef SOB_PANIC_H_SENTRY
#define SOB_PANIC_H_SENTRY

#define SOB_PANIC(...)  sob_panic(__FILE__, __LINE__, __VA_ARGS__)

void sob_panic(const char * file, unsigned long line, const char * msg, ...);

#endif /* SOB_PANIC_H_SENTRY */

