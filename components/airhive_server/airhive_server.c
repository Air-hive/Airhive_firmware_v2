#include "airhive_server.h"
#include "stdbool.h"
#include "esp_log.h"
#include "mdns.h"
#include "esp_mac.h"
#include "cncm.h"

static const char* TAG = "Airhive-server";

static httpd_handle_t server_hdl;
static bool instance_created = false;

esp_err_t test_get_handler(httpd_req_t* req) { return ESP_OK; }
//esp_err_t wifi_config_put_handler(httpd_req_t* req) { return ESP_OK; }
//esp_err_t ap_config_put_handler(httpd_req_t* req) { return ESP_OK; }
//esp_err_t machine_config_put_handler(httpd_req_t* req) { return ESP_OK; }
//esp_err_t machine_status_get_handler(httpd_req_t* req) { return ESP_OK; }
esp_err_t responses_get_handler(httpd_req_t* req) { return ESP_OK; }
//esp_err_t logs_get_handler(httpd_req_t* req) { return ESP_OK; }
esp_err_t commands_post_handler(httpd_req_t* req) { return ESP_OK; }
esp_err_t start_put_handler(httpd_req_t* req) { return ESP_OK; }
esp_err_t stop_put_handler(httpd_req_t* req) { return ESP_OK; }
esp_err_t clear_put_handler(httpd_req_t* req) { return ESP_OK; }


esp_err_t airhive_start_server()
{
    if(instance_created)
    {
        ESP_LOGI(TAG, "Server instance already exists.");
        return ESP_ERR_INVALID_STATE;
    }
    
    httpd_config_t airhive_server_config = HTTPD_DEFAULT_CONFIG();
    airhive_server_config.task_priority          = 1;
    airhive_server_config.server_port            = 80;
    airhive_server_config.max_resp_headers       = 8;    //TODO: review this.
    airhive_server_config.max_open_sockets       = 1;
    airhive_server_config.max_uri_handlers       = 11;
    airhive_server_config.send_wait_timeout      = 5;
    airhive_server_config.recv_wait_timeout      = 5;
    airhive_server_config.enable_so_linger       = true;
    airhive_server_config.linger_timeout         = 1;
    airhive_server_config.keep_alive_enable      = true;
    airhive_server_config.keep_alive_interval    = 5;
    airhive_server_config.keep_alive_count       = 3;
    airhive_server_config.keep_alive_idle        = 30;

    ESP_LOGI(TAG, "Creating server instance...");
    esp_err_t ret = httpd_start(&server_hdl, &airhive_server_config);
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
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl, &test_get));

    // httpd_uri_t wifi_config_put = {
    //     .uri = "/wifi-config",
    //     .method = HTTP_PUT,
    //     .handler = wifi_config_put_handler,
    //     .user_ctx = NULL
    // };
    // ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl, &wifi_config_put));

    // httpd_uri_t ap_config_put = {
    //     .uri = "/ap-config",
    //     .method = HTTP_PUT,
    //     .handler = ap_config_put_handler,
    //     .user_ctx = NULL
    // };
    // ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl, &ap_config_put));

    // httpd_uri_t machine_config_put = {
    //     .uri = "/machine-config",
    //     .method = HTTP_PUT,
    //     .handler = machine_config_put_handler,
    //     .user_ctx = NULL
    // };
    // ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl, &machine_config_put));

    // httpd_uri_t machine_status_get = {
    //     .uri = "/machine-status",
    //     .method = HTTP_GET,
    //     .handler = machine_status_get_handler,
    //     .user_ctx = NULL
    // };
    // ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl, &machine_status_get));

    httpd_uri_t responses_get = {
        .uri = "/responses",
        .method = HTTP_GET,
        .handler = responses_get_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl, &responses_get));

    // httpd_uri_t logs_get = {
    //     .uri = "/logs",
    //     .method = HTTP_GET,
    //     .handler = logs_get_handler,
    //     .user_ctx = NULL
    // };
    // ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl, &logs_get));

    httpd_uri_t commands_post = {
        .uri = "/commands",
        .method = HTTP_POST,
        .handler = commands_post_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl, &commands_post));

    httpd_uri_t start_put = {
        .uri = "/start",
        .method = HTTP_PUT,
        .handler = start_put_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl, &start_put));

    httpd_uri_t stop_put = {
        .uri = "/stop",
        .method = HTTP_PUT,
        .handler = stop_put_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl, &stop_put));

    httpd_uri_t clear_put = {
        .uri = "/clear",
        .method = HTTP_PUT,
        .handler = clear_put_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl, &clear_put));

    instance_created = true;
    return ESP_OK;
}


esp_err_t airhive_start_mdns()
{
    esp_err_t ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "mDNS initialized successfully");

    // Get unique chip ID for hostname
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char hostname[21];
    snprintf(hostname, sizeof(hostname), "Airhive-%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ret = mdns_hostname_set(hostname);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mDNS hostname set failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0); //TODO: check adding txt.
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mDNS service add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}
