#include "airhive_server.h"
#include "airhive_networking.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "cncm.h"

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(cncm_init());
    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(airhive_wifi_sta_init());
    ESP_ERROR_CHECK(airhive_start_server());
    ESP_ERROR_CHECK(airhive_start_mdns());
}