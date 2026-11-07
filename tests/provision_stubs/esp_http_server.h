#include "provision_test_stubs.h"
typedef void *httpd_handle_t;
typedef struct { size_t content_len; } httpd_req_t;
typedef struct { int max_open_sockets, backlog_conn, lru_purge_enable, max_uri_handlers, stack_size; void *uri_match_fn; } httpd_config_t;
typedef struct { const char *uri; int method; esp_err_t (*handler)(httpd_req_t *); } httpd_uri_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
#define HTTPD_SOCK_ERR_TIMEOUT -2
#define HTTP_GET 0
#define HTTP_POST 1
#define httpd_uri_match_wildcard NULL
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *t);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *k, const char *v);
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *s);
esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *r, const char *s);
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *s, ssize_t n);
esp_err_t httpd_resp_send(httpd_req_t *r, const char *s, ssize_t n);
int httpd_req_recv(httpd_req_t *r, char *s, size_t n);
static inline esp_err_t httpd_start(httpd_handle_t *h, const httpd_config_t *c) { (void)c; *h = (void *)1; return ESP_OK; }
static inline esp_err_t httpd_stop(httpd_handle_t h) { (void)h; return ESP_OK; }
static inline esp_err_t httpd_register_uri_handler(httpd_handle_t h, const httpd_uri_t *u) { (void)h; (void)u; return ESP_OK; }
