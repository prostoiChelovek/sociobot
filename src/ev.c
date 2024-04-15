#include "ev.h"

struct sob_ev {
    enum sob_ev_types _ty;
    sob_ev_data_t _data;
};

enum sob_ev_types sob_ev_type(const struct sob_ev * ev) {
    return ev->_ty;
}

sob_ev_data_t sob_ev_data(const struct sob_ev * ev) {
    return ev->_data;
}

