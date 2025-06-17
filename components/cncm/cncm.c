#include "esp_system.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/message_buffer.h"
#include "esp_task.h"

#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"

#include "cncm.h"

static MessageBufferHandle_t tx_buffer;
static MessageBufferHandle_t rx_buffer;
static SemaphoreHandle_t paused;
static bool cncm_initialized = false;

static const char *TAG = "CNCM";
//right now, checking that device is open is assumed to be equivalent to checking if this is null.
static cdc_acm_dev_hdl_t cdc_dev = NULL;

//May be extended afterwards.
typedef struct {
    uint32_t baudrate;
} machine_config_t;

static machine_config_t machine_config;

static void tx_consumer();
static bool rx_producer(const uint8_t *data, size_t data_len, void *arg);
static void handle_event(const cdc_acm_host_dev_event_data_t *event, void *user_ctx);
static void usb_event_handling_task(void *arg);
static void machine_open();

static void tx_consumer()
{
    char sentence[MAX_COMMAND_MESSAGE_SIZE];
    while (true)
    {
        size_t message_len = xMessageBufferReceive(tx_buffer, sentence + 1, MAX_COMMAND_SIZE, portMAX_DELAY);
        sentence[0] = COMMAND_SEPARATOR;
        sentence[message_len + 1] = COMMAND_SEPARATOR;
        xSemaphoreTake(paused, portMAX_DELAY);  //This part must not block and give the semaphore frequently.
        while
        (
            cdc_dev == NULL ||                  //include the command separators in the message length by adding two.
            cdc_acm_host_data_tx_blocking(cdc_dev, (const uint8_t*) sentence, message_len + 2, TX_TIMEOUT_MS) != ESP_OK
        )
        {
            xSemaphoreGive(paused); //checking if it didn't get paused each retry.
            xSemaphoreTake(paused, portMAX_DELAY);
        }
        ESP_LOGI(TAG, "Tx consumer high water mark:\t%d\n", uxTaskGetStackHighWaterMark(NULL));
    }
}

/**
 * assumes that the in buffer size is less than the rx_buffer size.
 * consider the case if a message was not received in one callback.
 * But in the same time make the in bulk buffer as big as the largest possible response.
 */
static bool rx_producer(const uint8_t *data, size_t data_len, void *arg)
{
    static char dummy[MAX_RESPONSE_SIZE];
    //we are sure that the buffer is not empty if we entered in the loop, so no need to wait.
    //Based on that some messages may get lost when space is short, which is fine.
    while(xMessageBufferSpacesAvailable(rx_buffer) < data_len) xMessageBufferReceive(rx_buffer, dummy, MAX_RESPONSE_SIZE, 0);
    //after passing the loop we are sure that the buffer have enough space since this is the only task that writes to it.
    assert(xMessageBufferSend(rx_buffer, data, data_len, 0) == data_len);
    return true;
}

static void handle_event(const cdc_acm_host_dev_event_data_t *event, void *user_ctx)
{
    switch (event->type)
    {
        case CDC_ACM_HOST_DEVICE_DISCONNECTED:
            ESP_LOGI(TAG, "Device disconnected");
            ESP_ERROR_CHECK(cdc_acm_host_close(event->data.cdc_hdl));
            cdc_dev = NULL;
            assert(xTaskCreate(machine_open, "machine_open", 2048, NULL, ESP_TASK_MAIN_PRIO, NULL) == pdPASS);
            break;
        case CDC_ACM_HOST_ERROR:
            ESP_LOGE(TAG, "CDC-ACM error occurred, err_no = %i", event->data.error);
            break;
        default:
            ESP_LOGW(TAG, "Unhandled CDC event: %i", event->type);
            break;
    }
}

static void usb_event_handling_task(void *arg)
{
    while (true)
    {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
        {
            ESP_ERROR_CHECK(usb_host_device_free_all());
        }
        ESP_LOGI(TAG, "USB event handling task high water mark:\t%d\n", uxTaskGetStackHighWaterMark(NULL));
    }
}

//maybe need to make sure no two instances of this task will be created.
static void machine_open()
{
    cdc_acm_host_device_config_t dev_config = {
        .connection_timeout_ms = portMAX_DELAY,
        .out_buffer_size = MAX_COMMAND_MESSAGE_SIZE,
        .in_buffer_size = MAX_RESPONSE_SIZE,
        .user_arg = NULL,
        .event_cb = handle_event,
        .data_cb = rx_producer
    };
    while(cdc_acm_host_open(USB_DEVICE_VID, USB_DEVICE_PID, 0, &dev_config, &cdc_dev) != ESP_OK);
    ESP_LOGI(TAG, "CDC ACM device opened.");

    //cdc_acm_host_desc_print(cdc_dev);
    
    cdc_acm_line_coding_t line_coding = {
        .dwDTERate = machine_config.baudrate,
        .bDataBits = 7,
        .bParityType = 1,
        .bCharFormat = 0
    };

    //Have a look on how flow control is done.
    ESP_ERROR_CHECK(cdc_acm_host_line_coding_set(cdc_dev, &line_coding));
    ESP_ERROR_CHECK(cdc_acm_host_set_control_line_state(cdc_dev, true, false));
    //cdc_acm_host_desc_print(cdc_dev);

    ESP_LOGI(TAG, "Machine open high water mark:\t%d\n", uxTaskGetStackHighWaterMark(NULL));

    xSemaphoreGive(paused);
    vTaskDelete(NULL);
}

//TODO: replace the assertions with error codes.
esp_err_t cncm_init(uint32_t baudrate)
{
    machine_config.baudrate = baudrate;
    ESP_LOGI(TAG, "Initializing the CNCM singleton.");
    ESP_LOGI(TAG, "Installing USB Host.");
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .root_port_unpowered = false,
        .enum_filter_cb = NULL
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    ESP_LOGI(TAG, "USB host installation complete.");

    rx_buffer = xMessageBufferCreate(RX_BUFFER_CAPACITY);
    tx_buffer = xMessageBufferCreate(TX_BUFFER_CAPACITY);
    paused = xSemaphoreCreateBinary();

    ESP_LOGI(TAG, "FreeRTOS elements initialized.");

    BaseType_t task_created = xTaskCreate(usb_event_handling_task, "usb_event_handling_task", 2048, NULL, USB_HOST_PRIORITY, NULL);
    assert(task_created == pdPASS);
    ESP_LOGI(TAG, "USB task created successfully.");

    task_created = xTaskCreate(tx_consumer, "tx_consumer", 2048, NULL, ESP_TASK_MAIN_PRIO, NULL);
    assert(task_created == pdPASS);

    ESP_LOGI(TAG, "Installing CDC-ACM driver.");
    cdc_acm_host_driver_config_t driver_config = {
        .driver_task_priority = USB_HOST_PRIORITY,
        .driver_task_stack_size = 2048,
        .xCoreID = 0,
        .new_dev_cb = NULL
    };
    ESP_ERROR_CHECK(cdc_acm_host_install(&driver_config));

    cncm_initialized = true;

    task_created = xTaskCreate(machine_open, "machine_open", 2048, NULL, ESP_TASK_MAIN_PRIO, NULL);
    assert(task_created == pdPASS);

    ESP_LOGI(TAG, "USB initialization complete.");
    return ESP_OK;
}

esp_err_t cncm_tx_producer(const char* command)
{
    size_t command_length = strlen(command);
    if (command_length == 0 || command_length > MAX_COMMAND_SIZE) return ESP_ERR_INVALID_ARG;
    if (xMessageBufferSend(tx_buffer, command, command_length, 0) != command_length) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t cncm_rx_consumer(uint8_t* to_receive, size_t* response_size)
{
    if(!cncm_initialized) return ESP_ERR_INVALID_STATE;
    if(xMessageBufferIsEmpty(rx_buffer)) return ESP_ERR_NOT_FOUND;
    *response_size = xMessageBufferNextLengthBytes(rx_buffer);
    to_receive = malloc(*response_size);
    if(to_receive == NULL) return ESP_ERR_NO_MEM;
    if(xMessageBufferReceive(rx_buffer, to_receive, *response_size, 0) == *response_size) return ESP_OK;
    else return ESP_ERR_NOT_FINISHED;
}

bool cncm_is_open()
{
    return cdc_dev != NULL ? true : false;
}

esp_err_t cncm_clear_tx_queue()
{
    if(!cncm_initialized) return ESP_ERR_INVALID_STATE;
    return (xMessageBufferReset(tx_buffer) == pdPASS || xMessageBufferIsEmpty(tx_buffer)) ? ESP_OK : ESP_FAIL;
}

esp_err_t cncm_pause()
{
    if(!cncm_initialized) return ESP_ERR_INVALID_STATE;
    if(xSemaphoreTake(paused, pdMS_TO_TICKS(TX_TIMEOUT_MS)) == pdFAIL) return ESP_ERR_TIMEOUT;
    return ESP_OK;
}

esp_err_t cncm_resume()
{
    if(!cncm_initialized) return ESP_ERR_INVALID_STATE;
    if(xSemaphoreGive(paused) == pdFAIL) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t cncm_reset_machine_config(uint32_t baudrate)
{
    if(!cncm_initialized) return ESP_ERR_INVALID_STATE;
    machine_config.baudrate = baudrate;
    esp_err_t ret = cncm_pause();
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to reset machine configuration, Error: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_ERROR_CHECK(cdc_acm_host_close(cdc_dev));
    cdc_dev = NULL;
    assert(xTaskCreate(machine_open, "machine_open", 1024, NULL, ESP_TASK_MAIN_PRIO, NULL) == pdPASS);
    return ESP_OK;
}
