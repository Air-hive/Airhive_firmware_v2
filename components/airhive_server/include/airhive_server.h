#include "esp_err.h"
#include "esp_http_server.h"

// Maximum request body size in bytes.
// TODO: review this, make it fit our api conventions, mostly the post commands handler will decide.
// I think putting a limit to this is nescessary, on the other hand, responses are not.
#define MAX_REQUEST_BODY_SIZE (50 * 1024) // 50 KiB
// The stack size depends on the maximum request body size, as we need to allocate a buffer for it on the stack.
#define SERVER_TASK_STACK_SIZE (MAX_REQUEST_BODY_SIZE + 4098)

esp_err_t airhive_start_server();

esp_err_t airhive_start_mdns();