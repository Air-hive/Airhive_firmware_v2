#include "esp_system.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/message_buffer.h"
#include "freertos/stream_buffer.h"
#include "esp_task.h"
#include "nvs_flash.h"

#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"

#include "cncm.h"


static MessageBufferHandle_t tx_buffer;
static StreamBufferHandle_t rx_buffer;
static SemaphoreHandle_t paused;
static bool cncm_initialized = false;
static nvs_handle_t cncm_nvs;
static char* cncm_namespace = "CNCM";

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
    char sentence[CNCM_MAX_COMMAND_MESSAGE_SIZE];
    while (true)
    {
        size_t message_len = xMessageBufferReceive(tx_buffer, sentence, CNCM_MAX_COMMAND_SIZE, portMAX_DELAY);
        sentence[message_len] = CNCM_COMMAND_SEPARATOR;
        xSemaphoreTake(paused, portMAX_DELAY); //wait for the semaphore to be given.
        xSemaphoreGive(paused); //If it was paused then we woudn't have reached this, else we should give the semaphore back.
        while
        (
            cdc_dev == NULL ||                  //include the command separators in the message length by adding two.
            cdc_acm_host_data_tx_blocking(cdc_dev, (const uint8_t*) sentence, message_len + 1, CNCM_TX_TIMEOUT_MS) != ESP_OK
        );
        ESP_LOGD(TAG, "Tx consumer high water mark:\t%d", uxTaskGetStackHighWaterMark(NULL));
    }
}

static bool rx_producer(const uint8_t *data, size_t data_len, void *arg)
{
    xStreamBufferSend(rx_buffer, (void*) data, data_len, 0);
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
            assert(xTaskCreate(machine_open, "machine_open", CNCM_MACHINE_OPEN_STACK_SIZE, NULL, ESP_TASK_MAIN_PRIO, NULL) == pdPASS);
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
        ESP_LOGD(TAG, "USB event handling task high water mark:\t%d", uxTaskGetStackHighWaterMark(NULL));
    }
}

//maybe need to make sure no two instances of this task will be created.
static void machine_open()
{
    gpio_set_level(CNCM_PRINTER_CONNECTED_LED, 0); // Turn off the connected LED.
    ESP_LOGI(TAG, "Attempting to open CDC ACM device ...");
    cdc_acm_host_device_config_t dev_config = {
        .connection_timeout_ms = portMAX_DELAY,
        .out_buffer_size = CNCM_MAX_COMMAND_MESSAGE_SIZE,
        .in_buffer_size = CNCM_MAX_BULK_IN_TRANSFER,
        .user_arg = NULL,
        .event_cb = handle_event,
        .data_cb = rx_producer
    };
    while(cdc_acm_host_open(CNCM_USB_DEVICE_VID, CNCM_USB_DEVICE_PID, 0, &dev_config, &cdc_dev) != ESP_OK)
    {
        ESP_LOGI(TAG, "Failed to open CDC ACM device, retrying...");
    }
    ESP_LOGI(TAG, "CDC ACM device opened.");
    gpio_set_level(CNCM_PRINTER_CONNECTED_LED, 1); // Turn on the connected LED.


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

    ESP_LOGD(TAG, "Machine open high water mark:\t%d", uxTaskGetStackHighWaterMark(NULL));

    xSemaphoreGive(paused);
    vTaskDelete(NULL);
}

esp_err_t cncm_init()
{
    gpio_config_t printer_connected_led_config = {
        .pin_bit_mask = (1ULL << CNCM_PRINTER_CONNECTED_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&printer_connected_led_config);
    esp_err_t ret = nvs_open(cncm_namespace, NVS_READWRITE, &cncm_nvs);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening NVS: %s.", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_get_u32(cncm_nvs, "baudrate", &machine_config.baudrate);
    if(ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "No stored value found for baudrate, falling back to default.");
        machine_config.baudrate = CNCM_DEFAULT_BAUDRATE;
    }
    else if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error accessing NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Initializing the CNCM singleton.");
    ESP_LOGI(TAG, "Installing USB Host.");
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL2, //TODO: review this.
        .root_port_unpowered = false,
        .enum_filter_cb = NULL
    };
    ret = usb_host_install(&host_config);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error installing USB host: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "USB host installation complete.");

    rx_buffer = xStreamBufferCreateWithCaps(CNCM_RX_BUFFER_CAPACITY, CNCM_RX_BUFFER_TRIGGER_LEVEL, MALLOC_CAP_SPIRAM);
    tx_buffer = xMessageBufferCreateWithCaps(CNCM_TX_BUFFER_CAPACITY, MALLOC_CAP_SPIRAM);
    if(rx_buffer == NULL || tx_buffer == NULL)
    {
        ESP_LOGE(TAG, "No enough memory for both rx and tx buffers.");
        return ESP_ERR_NO_MEM;
    }
    paused = xSemaphoreCreateBinary();

    ESP_LOGI(TAG, "FreeRTOS elements initialized.");

    BaseType_t task_created = xTaskCreate(usb_event_handling_task, "usb_event_handling_task", CNCM_USB_EVENT_STACK_SIZE, NULL, CNCM_USB_HOST_PRIORITY, NULL);
    if(task_created != pdPASS)
    {
        ESP_LOGE(TAG, "Couldn't create USB event handling task.");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "USB task created successfully.");

    task_created = xTaskCreate(tx_consumer, "tx_consumer", CNCM_TX_CONSUMER_STACK_SIZE, NULL, CNCM_TX_CONSUMER_PRIORITY, NULL);
    if(task_created != pdPASS)
    {
        ESP_LOGE(TAG, "Couldn't create tx_consumer task.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Installing CDC-ACM driver.");
    cdc_acm_host_driver_config_t driver_config = {
        .driver_task_priority = CNCM_USB_HOST_PRIORITY,
        .driver_task_stack_size = CNCM_CDC_DRIVER_STACK_SIZE,
        .xCoreID = tskNO_AFFINITY,
        .new_dev_cb = NULL
    };
    ret = cdc_acm_host_install(&driver_config);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error installing CDC ACM host: %s", esp_err_to_name(ret));
        return ret;
    }

    cncm_initialized = true;

    task_created = xTaskCreate(machine_open, "machine_open", CNCM_MACHINE_OPEN_STACK_SIZE, NULL, ESP_TASK_MAIN_PRIO, NULL);
    if(task_created != pdPASS)
    {
        ESP_LOGE(TAG, "Couldn't create machine open task.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "USB initialization complete.");
    return ESP_OK;
}

esp_err_t cncm_tx_producer(const char* command)
{
    size_t command_length = strlen(command);
    if (command_length == 0 || command_length > CNCM_MAX_COMMAND_SIZE) return ESP_ERR_INVALID_ARG;
    if (xMessageBufferSend(tx_buffer, command, command_length, 0) != command_length) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t cncm_rx_consumer(uint8_t* to_receive, size_t* response_size, size_t max_response_size)
{
    if(!cncm_initialized) return ESP_ERR_INVALID_STATE;
    if(to_receive == NULL || response_size == NULL) return ESP_ERR_INVALID_ARG;
    *response_size = xStreamBufferReceive(rx_buffer, to_receive, max_response_size, 0);
    return ESP_OK;
}

bool cncm_is_open()
{
    return cdc_dev != NULL ? true : false;
}

esp_err_t cncm_clear_tx_buffer()
{
    if(!cncm_initialized) return ESP_ERR_INVALID_STATE;
    return (xMessageBufferReset(tx_buffer) == pdPASS || xMessageBufferIsEmpty(tx_buffer)) ? ESP_OK : ESP_FAIL;
}

esp_err_t cncm_pause()
{
    if(!cncm_initialized) return ESP_ERR_INVALID_STATE;
    if(xSemaphoreTake(paused, pdMS_TO_TICKS(CNCM_TX_TIMEOUT_MS)) == pdFALSE) return ESP_ERR_TIMEOUT;
    return ESP_OK;
}

esp_err_t cncm_resume()
{
    if(!cncm_initialized) return ESP_ERR_INVALID_STATE;
    if(xSemaphoreGive(paused) == pdFALSE) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t cncm_reset_machine_config(uint32_t baudrate)
{
    if(!cncm_initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = nvs_set_u32(cncm_nvs, "baudrate", baudrate);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error accessing NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    machine_config.baudrate = baudrate;
    ret = cncm_pause();
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to reset machine configuration, Error: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_ERROR_CHECK(cdc_acm_host_close(cdc_dev));
    cdc_dev = NULL;
    assert(xTaskCreate(machine_open, "machine_open", CNCM_MACHINE_OPEN_STACK_SIZE, NULL, ESP_TASK_MAIN_PRIO, NULL) == pdPASS);
    return ESP_OK;
}
