#include "airhive_server.h"
#include "stdbool.h"
#include "esp_log.h"
#include "mdns.h"
#include "esp_mac.h"
#include "cncm.h"
#include "cJSON.h"
#include "esp_task.h"

static const char* TAG = "Airhive-server";

static httpd_handle_t server_hdl;
static bool instance_created = false;

//TODO: in case of errors return some infomation in the response body.
//Note: each handler has the max size of request body defined locally.
//TODO: see if you made logs that are supposed to be errors infos.

//Echo back the request body as the response.
esp_err_t test_get_handler(httpd_req_t* req) 
{
    ESP_LOGI(TAG, "Received GET request on /test");
    const size_t MAX_LOCAL_REQUEST_SIZE = 64;
    if(req->content_len > MAX_LOCAL_REQUEST_SIZE)
    {
        ESP_LOGE(TAG, "Request body too large: %d bytes, max allowed: %d bytes", req->content_len, MAX_LOCAL_REQUEST_SIZE);
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_send(req, NULL, 0); // Send empty response with 413 status.
        return ESP_OK;
    }

    char content_type_buffer[32];
    httpd_req_get_hdr_value_str(req, "Content-Type", content_type_buffer, sizeof(content_type_buffer)); //Null terminated.
    httpd_resp_set_type(req, content_type_buffer);

    char body_buffer[MAX_LOCAL_REQUEST_SIZE];
    httpd_req_recv(req, body_buffer, req->content_len);

    httpd_resp_set_status(req, "200 OK");
    ESP_ERROR_CHECK(httpd_resp_send(req, body_buffer, req->content_len));
    ESP_LOGI(TAG, "HTTPD task stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
    ESP_LOGI(TAG, "Minimum ever free heap size: %d", xPortGetMinimumEverFreeHeapSize());
    return ESP_OK;
}

//TODO: put a label at the end and gather cleanup in one place then return.
esp_err_t commands_post_handler(httpd_req_t* req)
{ 
    ESP_LOGI(TAG, "Received POST request on /commands");
    httpd_resp_set_type(req, "application/json");

    if(req->content_len > MAX_REQUEST_BODY_SIZE)
    {
        ESP_LOGE(TAG, "Request body too large: %d bytes, max allowed: %d bytes", req->content_len, MAX_REQUEST_BODY_SIZE);
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_send(req, NULL, 0); // Send empty response with 413 status.
        return ESP_OK;
    }

    // It seems that only the first bytes of the request body are buffered in the lower parts of the stack, and then the rest
    // is streamed into our body_buffer afterwards. So, there is no two copies of the entire request in memory at the same time.
    char body_buffer[MAX_REQUEST_BODY_SIZE];    // Since this is a JSON string, we can't read it in chunks.
    size_t received = 0;
    int ret = 0;

    // Keep receiving until we get everything
    while (received < req->content_len) {
        ret = httpd_req_recv(req, body_buffer + received,
                            req->content_len - received);
        if (ret <= 0) {
            ESP_LOGE(TAG, "Error receiving request body: ret=%d", ret);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, NULL, 0);
            return ESP_FAIL;
        }
        received += ret;
    }

    ESP_LOGI(TAG,"Recieved: %u content-len: %u",received,req->content_len);
    // if(total_received != req->content_len)
    // {        
    //     ESP_LOGE(TAG, "Failed to receive request body, received: %d", received);
    //     httpd_resp_set_status(req, "500 Internal Server Error");
    //     httpd_resp_send(req, NULL, 0); // Send empty response with 500 status.
    //     return ESP_FAIL;
    // }

    cJSON *json = cJSON_ParseWithLength(body_buffer, received);
    if(received == 0 || json == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON request body");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, NULL, 0); // Send empty response with 400 status.
        return ESP_OK;
    }

    cJSON *commands = cJSON_GetObjectItemCaseSensitive(json, "commands");
    if(!cJSON_IsArray(commands))
    {
        ESP_LOGE(TAG, "Invalid JSON format, 'commands' should be an array");
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, NULL, 0); // Send empty response with 400 status.
        return ESP_OK;
    }
    
    uint32_t sent_commands = 0;
    cJSON *command = commands->child;
    while(command != NULL)
    {
        if(!cJSON_IsString(command) || cJSON_GetStringValue(command) == NULL)
        {
            ESP_LOGE(TAG, "Invalid command in JSON array");
            httpd_resp_set_status(req, "400 Bad Request");
            goto cleanup;   // Don't skip commands, that is not safe.
        }
        const char* command_str = cJSON_GetStringValue(command);
        if(strlen(command_str) >= CNCM_MAX_COMMAND_SIZE)
        {
            ESP_LOGE(TAG, "Command too long: %s", command_str);
            httpd_resp_set_status(req, "400 Bad Request");
            goto cleanup;
        }
        esp_err_t ret = cncm_tx_producer(command_str);
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to send command: %s, error: %s", command_str, esp_err_to_name(ret));
            httpd_resp_set_status(req, "500 Internal Server Error");
            goto cleanup;
        }
        sent_commands++;
        command = command->next;    // TODO: check that there is no siblings before the first child.
    }
    httpd_resp_set_status(req, "200 OK");   // If got out of the loop without errors.

cleanup:
    cJSON_Delete(json); // Freeing this should be enough as it cascades to all children, TODO: check this.
    char sent_commands_str[12]; // Enough to hold a 32-bit integer as a string.
    snprintf(sent_commands_str, sizeof(sent_commands_str), "%" PRIu32, sent_commands);  // Converting it to string.
    cJSON *response_json = cJSON_CreateObject();    //TODO: what if this failed.
    cJSON_AddItemToObject(response_json, "sent_commands", cJSON_CreateString(sent_commands_str));
    char *response_str = cJSON_Print(response_json);
    cJSON_Delete(response_json); // Freeing this should be enough as it cascades, TODO: check this.
    if(response_str == NULL) ESP_LOGE(TAG, "Failed to print JSON object");
    else
    {
        esp_err_t ret = httpd_resp_send(req, response_str, HTTPD_RESP_USE_STRLEN);
        free(response_str);
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to send response, error: %s", esp_err_to_name(ret));
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

// TODO: should we consider making all handlers allocate memory for JSONs from the SPIRAM using initHooks?
// But keep in mind that any allocation larger than 16KB will be from the SPIRAM anyway.
// And keep in mind that initially we have 300KB of internal RAM free.
esp_err_t responses_get_handler(httpd_req_t* req)
{
    ESP_LOGI(TAG, "Received GET request on /responses");
    httpd_resp_set_type(req, "application/json");
    const size_t MAX_LOCAL_REQUEST_SIZE = 32; //since it's only a single number.
   // ESP_LOGE(TAG,"response buffer size: %d",req->content_len);
    if(req->content_len > MAX_LOCAL_REQUEST_SIZE)
    {
        ESP_LOGE(TAG, "Request body too large: %d bytes, max allowed: %d bytes", req->content_len, MAX_LOCAL_REQUEST_SIZE);
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_send(req, NULL, 0); // Send empty response with 413 status.
        return ESP_OK;
    }

    char body_buffer[MAX_LOCAL_REQUEST_SIZE];
    size_t received = httpd_req_recv(req, body_buffer, MAX_LOCAL_REQUEST_SIZE);
    if(received != req->content_len)
    {
        ESP_LOGE(TAG, "Failed to receive request body, received: %d", received);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, NULL, 0); // Send empty response with 500 status.
        return ESP_FAIL;
    }

    cJSON *in_json = cJSON_ParseWithLength(body_buffer, received);
    if(received == 0 || in_json == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON request body");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, NULL, 0); // Send empty response with 400 status.
        return ESP_OK;
    }

    cJSON *required_size_obj = cJSON_GetObjectItemCaseSensitive(in_json, "size");
    if(!cJSON_IsNumber(required_size_obj) || required_size_obj->valueint <= 0)
    {
        ESP_LOGE(TAG, "Invalid 'size' parameter in JSON request");
        cJSON_Delete(in_json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, NULL, 0); // Send empty response with 400 status.
        return ESP_OK;
    }
    size_t required_size = (size_t)required_size_obj->valueint;
    cJSON_Delete(in_json);

    cJSON *out_json = cJSON_CreateObject();
    if(out_json == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object");
        httpd_resp_set_status(req, "500 Internal Server Error");
        goto cleanup;
    }

    char *responses_str = malloc(required_size + 1);
    size_t response_size = 0;
    esp_err_t ret = cncm_rx_consumer((uint8_t*)responses_str, &response_size, required_size);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read responses, error: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "500 Internal Server Error");
        goto cleanup;
    }

    responses_str[response_size] = '\0';
    cJSON *responses = cJSON_CreateString(responses_str);   //This creates a copy.
    if(responses == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON string for responses");
        httpd_resp_set_status(req, "500 Internal Server Error");
        goto cleanup;
    }
    cJSON_AddItemToObject(out_json, "responses", responses);
    free(responses_str);

    httpd_resp_set_status(req, "200 OK");

cleanup:
    char *json_str = cJSON_Print(out_json);
    cJSON_Delete(out_json); // Freeing this should be enough as it cascades to all children, TODO: check this.
    if(json_str == NULL) ESP_LOGE(TAG, "Failed to print JSON object");
    else
    {
        ret = httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
        free(json_str);
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to send response, error: %s", esp_err_to_name(ret));
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t machine_status_get_handler(httpd_req_t* req)
{
    ESP_LOGI(TAG, "Received GET request on /responses");
    httpd_resp_set_type(req, "application/json");
    cJSON *json = cJSON_CreateObject();
    if(json == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object");
        httpd_resp_set_status(req, "500 Internal Server Error");
        goto cleanup;
    }
    cJSON_AddItemToObject(json, "status", cJSON_CreateString((cncm_is_open()) ? "Connected" : "Disconnected"));
    httpd_resp_set_status(req, "200 OK");

cleanup:
    char *json_str = cJSON_Print(json);
    cJSON_Delete(json); // Calling this with json == NULL is safe, it does nothing.
    if(json_str == NULL) ESP_LOGE(TAG, "Failed to print JSON object");
    else
    {
        esp_err_t ret = httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
        free(json_str);
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to send response, error: %s", esp_err_to_name(ret));
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t start_put_handler(httpd_req_t* req)
{
    ESP_LOGI(TAG, "Received PUT request on /start");
    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = cncm_resume();
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start machine, error: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "500 Internal Server Error");
    }
    else httpd_resp_set_status(req, "200 OK");

    esp_err_t send_ret = httpd_resp_send(req, NULL, 0);
    if(send_ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send response, error: %s", esp_err_to_name(send_ret));
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t stop_put_handler(httpd_req_t* req)
{
    ESP_LOGI(TAG, "Received PUT request on /stop");
    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = cncm_pause();
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to stop machine, error: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "500 Internal Server Error");
    }
    else httpd_resp_set_status(req, "200 OK");

    esp_err_t send_ret = httpd_resp_send(req, NULL, 0);
    if(send_ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send response, error: %s", esp_err_to_name(send_ret));
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t clear_put_handler(httpd_req_t* req)
{
    ESP_LOGI(TAG, "Received PUT request on /clear");
    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = cncm_clear_tx_buffer();
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to clear machine state, error: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "500 Internal Server Error");
    }
    else httpd_resp_set_status(req, "200 OK");

    esp_err_t send_ret = httpd_resp_send(req, NULL, 0);
    if(send_ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send response, error: %s", esp_err_to_name(send_ret));
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t machine_config_put_handler(httpd_req_t* req)
{
    ESP_LOGI(TAG, "Received PUT request on /machine-config");
    httpd_resp_set_type(req, "application/json");

    const size_t MAX_LOCAL_REQUEST_SIZE = 32;
    if(req->content_len > MAX_LOCAL_REQUEST_SIZE)
    {
        ESP_LOGE(TAG, "Request body too large: %d bytes, max allowed: %d bytes", req->content_len, MAX_LOCAL_REQUEST_SIZE);
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_send(req, NULL, 0); // Send empty response with 413 status.
        return ESP_OK;
    }

    char body_buffer[MAX_LOCAL_REQUEST_SIZE];
    size_t received = httpd_req_recv(req, body_buffer, MAX_LOCAL_REQUEST_SIZE);
    if(received != req->content_len)
    {
        ESP_LOGE(TAG, "Failed to receive request body, received: %d", received);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, NULL, 0); // Send empty response with 500 status.
        return ESP_FAIL;
    }

    cJSON *in_json = cJSON_ParseWithLength(body_buffer, received);
    if(received == 0 || in_json == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON request body");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, NULL, 0); // Send empty response with 400 status.
        return ESP_OK;
    }

    cJSON *baudrate_obj = cJSON_GetObjectItemCaseSensitive(in_json, "baudrate");
    if(!cJSON_IsNumber(baudrate_obj) || baudrate_obj->valueint <= 0)
    {
        ESP_LOGE(TAG, "Invalid 'baudrate' parameter in JSON request");
        cJSON_Delete(in_json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, NULL, 0); // Send empty response with 400 status.
        return ESP_OK;
    }
    uint32_t baudrate = (uint32_t)baudrate_obj->valueint;
    cJSON_Delete(in_json);

    esp_err_t ret = cncm_reset_machine_config(baudrate);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error resetting machine config: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, NULL, 0); // Send empty response with 500 status.
    } else{
        //respond with ok
        httpd_resp_set_status(req, "200 OK");
        esp_err_t send_ret = httpd_resp_send(req, NULL, 0);
        if(send_ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to send response, error: %s", esp_err_to_name(send_ret));
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}


esp_err_t airhive_start_server()
{
    if(instance_created)
    {
        ESP_LOGI(TAG, "Server instance already exists.");
        return ESP_ERR_INVALID_STATE;
    }
    
    httpd_config_t airhive_server_config = HTTPD_DEFAULT_CONFIG();
    airhive_server_config.task_priority          = ESP_TASK_MAIN_PRIO;
    airhive_server_config.stack_size             = SERVER_TASK_STACK_SIZE; // Stack size for the server task, TODO: review this, as we test call the test request.
    airhive_server_config.server_port            = 80;
    airhive_server_config.max_resp_headers       = 8;    //TODO: review this.
    airhive_server_config.max_open_sockets       = 1;
    airhive_server_config.max_uri_handlers       = 11;
    airhive_server_config.send_wait_timeout      = 5;   // Timeout for send function (in seconds).
    airhive_server_config.recv_wait_timeout      = 5;   // Timeout for recv function (in seconds).
    airhive_server_config.enable_so_linger       = true;
    airhive_server_config.linger_timeout         = 1;
    airhive_server_config.lru_purge_enable       = true; // Enable LRU purge to replace the current connection with the new one.
    airhive_server_config.keep_alive_enable      = true; // Presistent connections enabled.
    // Total time before closing the connection if no response from client:
    // keep_alive_idle + (keep_alive_interval * keep_alive_count) = 30 + (5 * 3) = 45 seconds.
    airhive_server_config.keep_alive_idle        = 30;  // Time before the first keep-alive probe is sent.
    airhive_server_config.keep_alive_interval    = 5;   // Time between subsequent keep-alive probes, if client didn't respond.
    airhive_server_config.keep_alive_count       = 3;   // Number of keep-alive probes to send before closing the connection.

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

    httpd_uri_t machine_config_put = {
        .uri = "/machine-config",
        .method = HTTP_PUT,
        .handler = machine_config_put_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl, &machine_config_put));

    httpd_uri_t machine_status_get = {
        .uri = "/machine-status",
        .method = HTTP_GET,
        .handler = machine_status_get_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl, &machine_status_get));

    httpd_uri_t responses_get = {
        .uri = "/responses",
        .method = HTTP_GET,
        .handler = responses_get_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_hdl, &responses_get));

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
    snprintf(hostname, sizeof(hostname), "Airhive-%02X%02X%02X%02X%02X%02X", MAC2STR(mac));

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
