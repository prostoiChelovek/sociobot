#include "https.h"

#include <curl/curl.h>

struct https_mod {
    CURL * curl;
    CURLM * curlm;
};

enum https_init_res https_init(struct https_mod * mod) {
    int still_running = 1;
    curl_off_t size;

    mod->curl = curl_easy_init();
    if (mod->curl == NULL) {
        return https_init_err_curl_easy_init;
    }
    mod->curlm = curl_multi_init();
    if (mod->curlm == NULL) {
        return https_init_err_curl_multi_init;
    }

    return https_init_ok;
}

#ifdef SOB_HTTPS_DEMO

#include <stdio.h>

int main(void) {
    struct https_mod m;
    if (https_init(&m) != https_init_ok) {
        fprintf(stderr, "cannot init\n");
        return 1;
    }
    return 0;
}

#endif /* SOB_HTTPS_DEMO */

