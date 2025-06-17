#include "cncm.h"
#include "esp_err.h"
#include "esp_http_server.h"

const httpd_config_t airhive_server_config = {
    .task_priority          = 1,
    .stack_size             = 4096,
    .core_id                = tskNO_AFFINITY,
    .task_caps              = (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
    .server_port            = 80,
    .ctrl_port              = ESP_HTTPD_DEF_CTRL_PORT,
    .max_resp_headers       = 8,    //TODO: review this.
    .backlog_conn           = 5,
    .lru_purge_enable       = false,
    .max_open_sockets       = 1,
    .max_uri_handlers       = 11,
    .send_wait_timeout      = 5,
    .recv_wait_timeout      = 5,
    .enable_so_linger       = true,
    .linger_timeout         = 1,
    .keep_alive_enable      = true,
    .keep_alive_interval    = 5,
    .keep_alive_count       = 3,
    .keep_alive_idle        = 30,
    .global_user_ctx        = NULL,
    .global_user_ctx_free_fn = NULL,
    .global_transport_ctx   = NULL,
    .global_transport_ctx_free_fn = NULL,
    .open_fn                = NULL,
    .close_fn               = NULL,
    .uri_match_fn           = NULL
};

esp_err_t airhive_start_server(httpd_handle_t* server_hdl);