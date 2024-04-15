#ifndef SOB_EV_H_SENTRY
#define SOB_EV_H_SENTRY

#include <stdint.h>

enum sob_ev_types {
    /* user sent a message */
    ev_tg_msg,
    /* user clicked a button */
    ev_tg_btn,
    /* initialization completed after "tg_init" */
    ev_tg_init,
    /* initialization failed after "tg_init" */
    ev_tg_init_fail,
    /* module is ready to stop after "tg_stop_prep". "tg_stop" can be called safely */
    ev_tg_stopped,
    /* message request was delivered to telegram after "tg_send_msg" */
    ev_tg_sent,
    /* impossible to send a message after "tg_send_msg".
     * Module tries hard so likely it is network failure or api error */
    ev_tg_send_fail,
    /* after "tg_fwd_msg" */
    ev_tg_forwarded,
    /* after "tg_fwd_msg" */
    ev_tg_fwd_fail,
    /* after "tg_del_msg" */
    ev_tg_deleted,
    /* after "tg_del_msg" */
    ev_tg_del_fail,
    /* after "tg_react_msg" */
    ev_tg_reacted,
    /* after "tg_react_msg" */
    ev_tg_react_fail,
    /* after "tg_msg_upd_text" */
    ev_tg_msg_updated,
    /* after "tg_msg_upd_text" */
    ev_tg_msg_upd_fail,
    /* after "tg_msg_del_button" */
    ev_tg_btn_deleted,
    /* after "tg_msg_del_button" */
    ev_tg_btn_del_fail,

    /* initialization complete after "yk_init" */
    ev_yk_init,
    /* initialization failed after "yk_init" */
    ev_yk_init_fail,
    /* a payment was authorized (state "waiting_for_capture") after "ev_yk_pay_started" */
    ev_yk_pay_wait_capture,
    /* payment failed and it is invalid now after "ev_yk_pay_started" */
    ev_yk_pay_fail,
    /* payment was not completed on time and it is invalid now after "ev_yk_pay_started" */
    ev_yk_pay_expired,
    /* module is ready to stop after "yk_stop_prep" */
    ev_yk_stopped,
    /* payment was sent to youkassa and is "pending" after "yk_pay" */
    ev_yk_pay_started,
    /* impossible to start a payment after "yk_pay". likely a network failure */
    ev_yk_pay_start_fail,
    /* payment was captured and money is transfered after "yk_capture" */
    ev_yk_pay_captured,
    /* after "yk_capture" */
    ev_yk_capture_fail,
    /* after "yk_cancel" */
    ev_yk_canceled,
    /* after "yk_cancel" */
    ev_yk_cancel_fail,

    /* after "les_init" */
    ev_les_init,
    /* after "les_init" */
    ev_les_init_fail,
    /* "les_stop" can be called after "les_stop_prep" */
    ev_les_stopped,
    /* change cannot be saved to disk, adding or modifying lessons is pointles */
    ev_les_write_fail,
    /* lesson is disabled immediately after "les_disable"
     * (no "les_update" required) but the change may not be saved to disk yet */
    ev_les_disabled,
    /* after "les_add" */
    ev_les_added,
    /* after "les_set_title" */
    ev_les_new_title,
    /* after "les_set_desc" */
    ev_les_new_desc,
    /* after "les_set_price" */
    ev_les_new_price,

    /* after "acc_init" */
    ev_acc_init,
    /* after "acc_init" */
    ev_acc_init_fail,
    /* after "acc_stop_prep" */
    ev_acc_stopped,
    /* account was created after "acc_new" */
    ev_acc_new,
    /* after "acc_new" */
    ev_acc_new_fail,
    /* after "acc_unlock" */
    ev_acc_les_unlocked,
    /* after "acc_unlock" */
    ev_acc_unlock_fail,

    /* after "str_init" */
    ev_str_init,
    /* after "str_init" */
    ev_str_init_fail,
    /* after "str_stop_prep" */
    ev_str_stopped,

    /* after "cred_init" */
    ev_cred_init,
    /* after "cred_init" */
    ev_cred_init_fail,
    /* after "cred_stop_prep" */
    ev_cred_stopped,

    /* after "bot_init" */
    ev_bot_init,
    /* after "bot_init" */
    ev_bot_init_fail,
    /* after "bot_stop_prep" */
    ev_bot_stopped
};

typedef uint64_t sob_ev_data_t;

struct sob_ev;

enum sob_ev_types sob_ev_type(const struct sob_ev * ev);
sob_ev_data_t sob_ev_data(const struct sob_ev * ev);

#endif /* SOB_EV_H_SENTRY */

