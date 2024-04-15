#ifndef SOB_TG_H_SENTRY
#define SOB_TG_H_SENTRY

#include "ev.h"

struct tg_mod;

struct tg_chat;
struct tg_msg;
struct tg_btn;
struct tg_reaction;
struct tg_msg_to_send;

enum tg_init_res {
    tg_init_res_err = 2,
    tg_init_res_ok = 1
};

enum tg_init_res tg_init(struct tg_mod * tg);

int tg_stop_prep(struct tg_mod * tg);

int tg_stop(struct tg_mod * tg);

#endif /* SOB_TG_H_SENTRY */

