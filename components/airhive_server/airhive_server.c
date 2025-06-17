#include "airhive_server.h"
#include "stdbool.h"
#include "esp_log.h"

const char* TAG = "Airhive-server";

esp_err_t test_get_handler(httpd_req_t* req) { }
esp_err_t wifi_config_put_handler(httpd_req_t* req) { }
esp_err_t ap_config_put_handler(httpd_req_t* req) { }
esp_err_t machine_config_put_handler(httpd_req_t* req) { }
esp_err_t machine_status_get_handler(httpd_req_t* req) { }
esp_err_t responses_get_handler(httpd_req_t* req) { }
esp_err_t logs_get_handler(httpd_req_t* req) { }
esp_err_t commands_post_handler(httpd_req_t* req) { }
esp_err_t start_put_handler(httpd_req_t* req) { }
esp_err_t stop_put_handler(httpd_req_t* req) { }
esp_err_t clear_put_handler(httpd_req_t* req) { }


esp_err_t airhive_start_server(httpd_handle_t* server_hdl)
{
    httpd_handle_t server_hdl_tmp = NULL;
    ESP_LOGI(TAG, "Creating server instance...");
    esp_err_t ret = httpd_start(&server_hdl_tmp, &airhive_server_config);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Server instance creation failed, error: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Server instance created.");
    
    ESP_LOGI(TAG, "Registering URI handers...");
    httpd_uri_t test_get = {
        .uri = "/test",
        .method = HTTP_GET,
        .handler = test_get_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl_tmp, &test_get));

    httpd_uri_t wifi_config_put = {
        .uri = "/wifi-config",
        .method = HTTP_PUT,
        .handler = wifi_config_put_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl_tmp, &wifi_config_put));

    httpd_uri_t ap_config_put = {
        .uri = "/ap-config",
        .method = HTTP_PUT,
        .handler = ap_config_put_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl_tmp, &ap_config_put));

    httpd_uri_t machine_config_put = {
        .uri = "/machine-config",
        .method = HTTP_PUT,
        .handler = machine_config_put_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl_tmp, &machine_config_put));

    httpd_uri_t machine_status_get = {
        .uri = "/machine-status",
        .method = HTTP_GET,
        .handler = machine_status_get_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl_tmp, &machine_status_get));

    httpd_uri_t responses_get = {
        .uri = "/responses",
        .method = HTTP_GET,
        .handler = responses_get_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl_tmp, &responses_get));

    httpd_uri_t logs_get = {
        .uri = "/logs",
        .method = HTTP_GET,
        .handler = logs_get_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl_tmp, &logs_get));

    httpd_uri_t commands_post = {
        .uri = "/commands",
        .method = HTTP_POST,
        .handler = commands_post_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl_tmp, &commands_post));

    httpd_uri_t start_put = {
        .uri = "/start",
        .method = HTTP_PUT,
        .handler = start_put_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl_tmp, &start_put));

    httpd_uri_t stop_put = {
        .uri = "/stop",
        .method = HTTP_PUT,
        .handler = stop_put_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl_tmp, &stop_put));

    httpd_uri_t clear_put = {
        .uri = "/clear",
        .method = HTTP_PUT,
        .handler = clear_put_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl_tmp, &clear_put));

    *server_hdl = server_hdl_tmp;
    return ESP_OK;
}
