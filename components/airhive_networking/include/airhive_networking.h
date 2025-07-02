#include "esp_err.h"

#define AIRHIVE_ESP_TOUCH_V2_KEY "1ARTE67890kgit56"
#define AIRHIVE_SMART_CONFIG_TRIG GPIO_NUM_38
#define AIRHIVE_WIFI_CONNECTED_LED GPIO_NUM_35
#define AIRHIVE_SMART_CONFIG_LED GPIO_NUM_36    // TODO: correct pin numbers.

typedef enum {
    AIRHIVE_SC_START
} airhive_event_t;

ESP_EVENT_DECLARE_BASE(AIRHIVE_EVENT);

esp_err_t airhive_wifi_sta_init();
