
**FIRMWARE**	

1. Modular design and software components	5
   1. Airhive networking module	5
   2. Airhive HTTP server module	6
   3. High level USB interface (CNCM)	10
1. RTOS	13
   1. FreeRTOS	13
   2. Tasks	13



**FIRMWARE**

**2.1	Modular design and software components**

Our choice for the firmware is to use the ESP-IDF (Espressif Integrated Development Framework). Our choice is based on the rich libraries it offers. Let us analyze the firmware into software components.

**2.1.1	Airhive networking module**

This component handles all operations related to connectivity. Currently, only Wi-Fi is supported. It handles Wi-Fi configuration, connection, retries and SmartConfig. 

SmartConfig is a provisioning technology developed by Espressif Systems that simplifies the process of connecting IoT devices to a Wi-Fi network. It is particularly useful in consumer IoT scenarios where devices have limited or no user interface, such as smart plugs, sensors, and embedded controllers.

The SmartConfig method allows a mobile app (typically running on a smartphone) to securely transmit the Wi-Fi credentials (SSID and password) to an unconfigured device over the air. This is achieved by encoding the credentials into specially crafted UDP packets that are broadcast over the local network. The IoT device, operating in a promiscuous mode, listens to these packets, decodes the credentials, and connects to the Wi-Fi network accordingly. By simply pressing a button and starting the mobile app the user can register any number of machines he wants at the same time.



**2.1.2	Airhive HTTP server module**

This component is responsible for starting the HTTP server and running all HTTP handlers and running mDNS task.

Multicast DNS (mDNS) is a protocol that enables name resolution in small, local networks without the need for a central DNS server. It is primarily designed for zero-configuration networking, allowing devices like computers, printers, and smart home appliances to discover each other and establish communication using human-readable hostnames (e.g., printer.local) instead of IP addresses.

mDNS operates by sending DNS-like queries and responses over multicast to the IP address 224.0.0.251 (IPv4) or FF02::FB (IPv6), using UDP port 5353. When a device wants to resolve a hostname, it multicasts a query to the network. Any device that recognizes the name responds with the appropriate IP address, enabling peer-to-peer name resolution.

Below are the HTTP endpoints provided by the Airhive embedded server. Each entry lists the path, HTTP method, request body format, response body format, and all handled error status codes.

-----
**GET /test**

- Request:
  - A test endpoint that echoes back the request body.
  - Body: any content up to 64 bytes
  - Headers: Content-Type: <value>
- Response (200 OK):
  - Body: identical to request payload
  - Headers: Content-Type: <same as request>
- Error (413 Payload Too Large):
  - Trigger: Content-Length > 64
  - Body: empty
-----
**POST /commands**

- Request:
  - Adds commands to the tx\_queue.
  - Headers: Content-Type: application/json
  - Body (JSON):

    {"commands": ["CMD1", "CMD2", ...]}
  - Max size: 50 KiB
- Response (200 OK):
  - Body (JSON):

    { "sent\_commands": "<number>" }

    The number of commands added to the send queue, in this case it’s supposed to be equal to the number of commands in the request.

- Errors:
  - 413 Payload Too Large: body exceeds 50 KiB, in this case no commands will be sent, and response is empty body.
  - 400 Bad Request:
    - JSON parse failure, in this case no commands will be sent, and response is empty body.
    - Missing or non-array commands, in this case no commands will be sent, and response is emtpy body.
    - Any command not a string or too long (>= CNCM\_MAX\_COMMAND\_SIZE), in this case all commands prior to this one are sent and number of sent commands is returned in response body, as shown above.
  - 500 Internal Server Error: failure in cncm\_tx\_producer during adding some command in the queue, in this case all commands prior to this one are sent and number of sent commands is returned in response body, as shown above.
-----
**GET /responses**

- Request:
  - Returns bytes from the rx\_queue (responses queue).
  - Headers: Content-Type: application/json
  - Body (JSON):

    { "size": <positive integer> }
  - size is the maximum number of bytes to return in the reponse body.
  - Max size of request body: 32 bytes.
- Response (200 OK):
  - Body (JSON):

    { "responses": "<machine responses>" }
- Errors:
  - 413 Payload Too Large: body length > 32
  - 400 Bad Request:
    - JSON parse failure.
    - Missing or non-number size or size <= 0
  - 500 Internal Server Error: allocation failure, or other internal reasons. In all those cases an empty response is sent. Note: Some responses may get lost due to insufficient memory, this is tolerated.
-----

**GET /machine-status**

- Request:
  - Check if the MCU is connected to a machine or not.
  - Request body is empty.
- Response (200 OK):
  - Body (JSON):

    { "status": "Connected" | "Disconnected" }
- Errors: none explicitly handled (always returns 200), except in some cases where internal errors occur.
-----
**PUT /start**

- Request:
  - unpauses the consumption of the tx\_queue.
  - Empty request body.
- Response:
  - 200 OK: on resume success.
  - 500 Internal Server Error: if the queue was not paused, or for other internal errors.
  - Body: empty.
-----
**PUT /stop**

- Request:
  - Pauses the tx\_queue.
  - Request body is empty.
- Response:
  - 200 OK: on pause success.
  - 500 Internal Server Error: If it’s already paused, or for other internal errors.
  - Body: empty.
-----
**PUT /clear**

- Request:
  - Clears the tx\_queue.
  - Empty request body.
- Response:
  - 200 OK: on success.
  - 500 Internal Server Error: Internal errors.
  - Body: empty
-----
**PUT /machine-config**

- Request:
  - resets some machine config parameters (currently baudrate only), and reopens the machine with the new configuration, and these changes are presistant.
  - Headers: Content-Type: application/json
  - Body (JSON):

    { "baudrate": <positive integer> }
  - Max size: 32 bytes
- Response (200 OK): empty body on success.
- Errors:
  - 413 Payload Too Large: body length > 32
  - 400 Bad Request: JSON parse failure or baudrate missing/non-positive.
  - 500 Internal Server Error: Internal errors.
-----
**

**2.1.3	High level USB interface (CNCM)**

This component manages device communication with the machine as a USB CDC ACM device, this includes initialization, configuration, data transmission, and reception using FreeRTOS tasks, buffers, and semaphores. The module handles USB events, and device connection/disconnection. It provides an API for sending commands to the CNC machine, receiving responses, pausing/resuming communication, and resetting machine configuration.

The following is the provided API:

/\*\*

` `\* @brief Initializes the USB host and the CDC-ACM driver.

` `\* Must be called first before any function in this file.

` `\*/

esp\_err\_t cncm\_init();

/\*\*

` `\* @brief Adds a message to the tx\_queue.

` `\* @param to\_send [IN] string to add to the tx\_queue.

` `\* @return ESP\_ERR\_INVALID\_ARG if the command is empty or oversized. 

` `\* @return EPS\_ERR\_NO\_MEM if memory allocation failed.

` `\* @return ESP\_OK otherwise. 

` `\*/

esp\_err\_t cncm\_tx\_producer(const char\* command);

/\*\*

` `\* @brief Reads from the rx\_queue all responses that are available.

` `\* @param to\_receive [OUT] the destination buffer.

` `\* @param response\_size [OUT] pointer to the size of the response.

` `\* @return ESP\_ERR\_INVALID\_STATE if cncm was not initialized.

` `\* @return ESP\_ERR\_INVALID\_ARG if the to\_receive or response\_size is NULL. 

` `\* @return ESP\_OK otherwise.

` `\* @note It's the caller's responsibility to ensure that the buffer is large enough to hold responses.

` `\*/

esp\_err\_t cncm\_rx\_consumer(uint8\_t\* to\_receive, size\_t\* response\_size, size\_t max\_response\_size);

/\*\*

` `\* @brief returns true if a device is connected and false otherwise.

` `\* @return true if the device is open and returns false otherwise including the case if cncm was not initialized.

` `\*/

bool cncm\_is\_open();

/\*\*

` `\* @brief clears the tx\_buffer.

` `\* @return ESP\_ERR\_INVALID\_STATE if cncm was not initialized. 

` `\* @return ESP\_OK if tx\_queue was cleared and ESP\_FAIL otherwise. 

` `\*/

esp\_err\_t cncm\_clear\_tx\_buffer();

/\*\*

` `\* @brief stops tx\_consumer from sending further messages. At most blocks for CNCM\_TX\_TIMEOUT\_MS.

` `\* @return ESP\_ERR\_INVALID\_STATE if cncm was not initialized.

` `\* @return ESP\_ERR\_TIMEOUT if it blocked for more than CNCM\_TX\_TIMEOUT\_MS.

` `\* @return ESP\_OK otherwise.

` `\* @note If the machine is already paused, ESP\_ERR\_TIMEOUT will be returned.

` `\*/

esp\_err\_t cncm\_pause();

/\*\*

` `\* @brief allows tx\_consumer to continue sending messages.

` `\* @return ESP\_ERR\_INVALID\_STATE if cncm was not initialized.

` `\* @return ESP\_FAIL if the pause semaphore was not released successfully, or if it's already released. 

` `\* @return ESP\_OK otherwise. 

` `\*/

esp\_err\_t cncm\_resume();

/\*\*

` `\* @brief changes the baudrate and reopens the machine with the new baudrate. At most blocks for CNCM\_TX\_TIMEOUT\_MS.

` `\* @return ESP\_ERR\_INVALID\_STATE if cncm was not initialized.

` `\* @return Error codes of pause().

` `\* @return ESP\_OK otherwise.

` `\*/

esp\_err\_t cncm\_reset\_machine\_config(uint32\_t baudrate);



**2.2	RTOS**

**2.2.1	FreeRTOS**

Our system is FreeRTOS based.

FreeRTOS is a lightweight, open-source real-time operating system designed for embedded systems and microcontrollers. Developed by Real Time Engineers Ltd. and now maintained by Amazon Web Services (AWS). We are using FreeRTOS in its preemptive mode.

**2.2.2	Tasks**

Let us list all tasks our system runs constantly, each paired with its priority and stack size:

- Tx\_consumer – Stack size: 4096 – Priority: 2
- CDC\_ACM\_host\_driver\_task - Stack size: 4096 - Priority: 3
- USB\_event\_handling\_task - Stack size: 4096 - Priority: 3
- Default\_event\_loop - Stack size: 2816 - Priority: 20 (system task).
- TCP/IP\_task - Stack size: 3584 - Priority: 18 (system task).
- WiFi\_task - Stack size: 3000 - Priority: 23 (system task)
- Airhive\_server\_task - Stack size: 55,296 - Priority: 1
- mDNS\_task - Stack size: 4096 - Priority: 1

The mDNS\_task, WiFi\_task, and TCP/IP\_task handle all connectivity related operations in the background.

Default\_event\_loop and USB\_event\_handling\_task handles together handles all our system events, other tasks or ISRs post events to them by setting specific flags and those tasks periodically checks those flags and runs event handlers for each assigned event.

The remaining three tasks either produce or consume data from two very large buffers that are allocated in the PSRAM. The tx\_buffer, this carries messages that are to be sent to the connected machine, the tx\_consumer task periodically checks and consumes data sending it to tx\_buffers in the lower levels of the USB stack, and the server\_task produces data into this buffer according to incoming requests. The other large buffer is the rx\_buffer, the CDC\_ACM\_host\_driver\_task feeds this buffer with the incoming data from the machine, and the server\_task consumes this data according to incoming requests.

![A screenshot of a diagram

AI-generated content may be incorrect.](Aspose.Words.758a230d-a2f4-401c-9490-49a5ed7f0264.012.png)Following is a diagram that summarizes:

Figure 2.1


**REFERENCES**

- Espressif IDF docs (v5.4). API reference – Project configuration.
- Espressif IDF docs (v5.4). API guides – Wi-Fi drivers.
- Espressif IDF docs (v5.4). API reference – Networking – Wi-Fi.
- Espressif IDF docs (v5.4). API reference – Networking – ESP-NETIF.
- Espressif IDF docs (v5.4). API reference – Storage – Non-Volatile Storage.
- Espressif IDF docs (v5.4). API reference – Provisioning – SmartConfig.
- Espressif IDF docs (v5.4). API reference – Peripherals – GPIO & RTC.
- Espressif IDF docs (v5.4). API reference – System – Watchdogs.
- Espressif IDF docs (v5.4). API reference – System – Interrupt allocation.
- Espressif IDF docs (v5.4). API guides – Partition tables.
- Espressif IDF docs (v5.4). API reference – System – Event loop library.
- Espressif IDF docs (v5.4). API reference – Application protocols – HTTP server.
- Espressif IDF docs (v5.4). API guides – SPI Flash & external SPI RAM configuration.
- Espressif IDF docs (v5.4). API guides – Support for external RAM.
- Espressif IDF docs (v5.4). API reference – Application protocols – mDNS service.
- Espressif IDF docs (v5.4). API guides – Build system.
- ESP IDF CDC ACM host component - <https://github.com/espressif/esp-usb/tree/b79a9c25ce77d89e934023205ef184e3be1bd59b/host/class/cdc/usb_host_cdc_acm>
- Dave Gamble (2009). cJSON. <https://github.com/DaveGamble/cJSON>
- Espressif IDF docs (v5.4). API reference – System – FreeRTOS.

