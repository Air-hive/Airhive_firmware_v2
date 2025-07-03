#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_err.h"
#include "stdbool.h"
#include "esp_task.h"
#include "driver/gpio.h"

// #define CNCM_TX_BUFFER_CAPACITY (1048576*4)    //4 MiB
// #define CNCM_RX_BUFFER_CAPACITY (1048576*1)    //1 MiB, External memeory must have at least RX_BUFFER_CAPACITY free.

#define CNCM_TX_BUFFER_CAPACITY (1024 * 1024)    //4 MiB
#define CNCM_RX_BUFFER_CAPACITY (1024 * 600)    //1 MiB, External memeory must have at least RX_BUFFER_CAPACITY free.
#define CNCM_RX_BUFFER_TRIGGER_LEVEL (32)
#define CNCM_TX_CONSUMER_PRIORITY (ESP_TASK_MAIN_PRIO + 1)
#define CNCM_USB_HOST_PRIORITY (CNCM_TX_CONSUMER_PRIORITY + 1)
#define CNCM_USB_DEVICE_VID (CDC_HOST_ANY_VID)
#define CNCM_USB_DEVICE_PID (CDC_HOST_ANY_PID)
#define CNCM_MAX_BULK_IN_TRANSFER (1024) // TODO: may need change.
#define CNCM_MAX_COMMAND_SIZE (512)
#define CNCM_MAX_COMMAND_MESSAGE_SIZE (CNCM_MAX_COMMAND_SIZE + 1) //taking one byte at the end for COMMAND_SEPARATOR.
#define CNCM_COMMAND_SEPARATOR '\n'
#define CNCM_TX_TIMEOUT_MS (1000)
#define CNCM_TX_CONSUMER_STACK_SIZE (4096)
#define CNCM_USB_EVENT_STACK_SIZE (4096)
#define CNCM_MACHINE_OPEN_STACK_SIZE (4096)
#define CNCM_CDC_DRIVER_STACK_SIZE (4096)
#define CNCM_DEFAULT_BAUDRATE (115200)
#define CNCM_PRINTER_CONNECTED_LED GPIO_NUM_37


/**
 * @brief Initializes the USB host and the CDC-ACM driver. Must be called first before any function in this file.
 * TODO: put return codes.
 */
esp_err_t cncm_init();

/**
 * @brief Adds a message to the tx_queue.
 * @param to_send [IN] string to add to the tx_queue.
 * @return ESP_ERR_INVALID_ARG if the command is empty or oversized. 
 * @return EPS_ERR_NO_MEM if memory allocation failed.
 * @return ESP_OK otherwise. 
 */
esp_err_t cncm_tx_producer(const char* command);

/**
 * @brief Reads from the rx_queue all responses that are available.
 * @param to_receive [OUT] the destination buffer.
 * @param response_size [OUT] pointer to the size of the response.
 * @return ESP_ERR_INVALID_STATE if cncm was not initialized.
 * @return ESP_ERR_INVALID_ARG if the to_receive or response_size is NULL. 
 * @return ESP_OK otherwise.
 * @note It's the caller's responsibility to ensure that the buffer is large enough to hold responses.
 */
esp_err_t cncm_rx_consumer(uint8_t* to_receive, size_t* response_size, size_t max_response_size);

/**
 * @brief returns true if a device is connected and false otherwise.
 * @return true if the device is open and returns false otherwise including the case if cncm was not initialized.
 */
bool cncm_is_open();

/**
 * @brief clears the tx_buffer.
 * @return ESP_ERR_INVALID_STATE if cncm was not initialized. 
 * @return ESP_OK if tx_queue was cleared and ESP_FAIL otherwise. 
 */
esp_err_t cncm_clear_tx_buffer();

/**
 * @brief stops tx_consumer from sending further messages. At most blocks for CNCM_TX_TIMEOUT_MS.
 * @return ESP_ERR_INVALID_STATE if cncm was not initialized.
 * @return ESP_ERR_TIMEOUT if it blocked for more than CNCM_TX_TIMEOUT_MS.
 * @return ESP_OK otherwise.
 * @note If the machine is already paused, ESP_ERR_TIMEOUT will be returned.
 */
esp_err_t cncm_pause();

/**
 * @brief allows tx_consumer to continue sending messages.
 * @return ESP_ERR_INVALID_STATE if cncm was not initialized.
 * @return ESP_FAIL if the pause semaphore was not released successfully, or if it's already released. 
 * @return ESP_OK otherwise. 
 */
esp_err_t cncm_resume();

/**
 * @brief changes the baudrate and reopens the machine with the new baudrate. At most blocks for CNCM_TX_TIMEOUT_MS.
 * @return ESP_ERR_INVALID_STATE if cncm was not initialized.
 * @return Error codes of pause().
 * @return ESP_OK otherwise.
 */
esp_err_t cncm_reset_machine_config(uint32_t baudrate);
