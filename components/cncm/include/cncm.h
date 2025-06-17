#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_err.h"
#include "stdbool.h"

#define TX_BUFFER_CAPACITY (1024)
#define RX_BUFFER_CAPACITY (1024)
#define USB_HOST_PRIORITY (3)
#define USB_DEVICE_VID (CDC_HOST_ANY_VID)
#define USB_DEVICE_PID (CDC_HOST_ANY_PID)
#define MAX_COMMAND_SIZE (128)
#define MAX_COMMAND_MESSAGE_SIZE (MAX_COMMAND_SIZE + 2) //taking one byte at the start and one at the end for COMMAND_SEPARATOR.
#define MAX_RESPONSE_SIZE (512)
#define COMMAND_SEPARATOR '\n'
#define TX_TIMEOUT_MS (1000)

/**
 * @brief Initializes the USB host and the CDC-ACM driver. Must be called first before any function in this file.
 * @param mp_baudrate the baudrate to be used by the USB device.
 */
esp_err_t cncm_init(uint32_t baudrate);

/**
 * @brief Adds a message to the tx_queue.
 * @param to_send [IN] string to add to the tx_queue.
 * @return ESP_ERR_INVALID_ARG if the command is empty or oversized. 
 * @return EPS_ERR_NO_MEM if memory allocation failed.
 * @return ESP_OK otherwise. 
 */
esp_err_t cncm_tx_producer(const char* command);

/**
 * @brief Reads from the rx_queue a single response recieved from an rx callback. Assumes messages obey size restrictions.
 * @param to_receive [OUT] pointer to the message to be received.
 * @param response_size [OUT] pointer to the size of the response.
 * @return ESP_ERR_NOT_FOUND if the buffer is empty. 
 * @return ESP_ERR_INVALID_STATE if cncm was not initialized. 
 * @return ESP_ERR_NO_MEM if failed to allocate memory to copy the received message.
 * @return ESP_ERR_NOT_FINISHED if the number of received bytes is not the number expected. 
 * @return ESP_OK otherwise.
 */
esp_err_t cncm_rx_consumer(uint8_t* to_receive, size_t* response_size);

/**
 * @brief returns true if a device is connected and false otherwise.
 * @return true if the device is open and returns false otherwise including the case if cncm was not initialized.
 */
bool cncm_is_open();

/**
 * @brief clears the tx_queue.
 * @return ESP_ERR_INVALID_STATE if cncm was not initialized. 
 * @return ESP_OK if tx_queue was cleared and ESP_FAIL otherwise. 
 */
esp_err_t cncm_clear_tx_queue();

/**
 * @brief stops tx_consumer from sending further messages. At most blocks for TX_TIMEOUT_MS.
 * @return ESP_ERR_INVALID_STATE if cncm was not initialized.
 * @return ESP_ERR_TIMEOUT if it blocked for more than TX_TIMEOUT_MS. 
 * @return ESP_OK otherwise.
 */
esp_err_t cncm_pause();

/**
 * @brief allows tx_consumer to continue sending messages.
 * @return ESP_ERR_INVALID_STATE if cncm was not initialized.
 * @return ESP_FAIL if the pause semaphore was not released successfully. 
 * @return ESP_OK otherwise. 
 */
esp_err_t cncm_resume();

/**
 * @brief changes the baudrate and reopens the machine with the new baudrate. At most blocks for TX_TIMEOUT_MS.
 * @return ESP_ERR_INVALID_STATE if cncm was not initialized.
 * @return Error codes of pause().
 * @return ESP_OK otherwise.
 */
esp_err_t cncm_reset_machine_config(uint32_t baudrate);
