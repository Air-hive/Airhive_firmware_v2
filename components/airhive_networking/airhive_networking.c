#include "esp_wifi.h"
#include "esp_log.h"
#include "airhive_networking.h"

static const char* TAG = "Airhive-Networking";

esp_err_t airhive_wifi_sta_init()
{
    ESP_LOGI(TAG, "Initializing WiFi station...");
    esp_err_t ret;

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&init_cfg);
    if(ret != ESP_OK)
    {
        ESP_LOGI(TAG, "Error initializing wifi: %s.", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "WiFi initialized successfully.");
    
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if(ret != ESP_OK)
    {
        ESP_LOGI(TAG, "Error setting wifi mode: %s.", esp_err_to_name(ret));
        return ret;
    }

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = AIRHIVE_WIFI_STA_SSID,
            .password = AIRHIVE_WIFI_STA_PASS,
            .scan_method = WIFI_FAST_SCAN,
            .bssid_set = false,
            .channel = 0,   //channel unknwon.
            .listen_interval = 0, //default
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold = {
                .rssi = -70, // Minimum RSSI for fast scan, TODO: review this.
                .authmode = WIFI_AUTH_WPA2_PSK, // Minimum auth mode for fast
                .rssi_5g_adjustment = 0 // No adjustment for 5G RSSI
            },
            .pmf_cfg.required = true,
            .rm_enabled = 1,
            .btm_enabled = 1, // Enable BTM (Background Traffic Management)
            .mbo_enabled = 0, // Disable MBO (Multi-Band Operation) for now, not sure about 5GHz, TODO: review this.
            .ft_enabled = 1, // Enable FT (Fast Transition)
            .owe_enabled = 1, // Enable OWE (Opportunistic Wireless Encryption)
            .transition_disable = 0, //For now we allow WPA2.
            .sae_pwe_h2e = WPA3_SAE_PWE_UNSPECIFIED, //for now, TODO: review this.
            .sae_pk_mode = WPA3_SAE_PK_MODE_AUTOMATIC, //for now, TODO: review this.
            .failure_retry_cnt = 3, // TODO: review this.
        }
    };  // TODO: take a look at the other fields in wifi_config_t, see if we need to set them.

    ret = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg);
    if(ret != ESP_OK)
    {
        ESP_LOGI(TAG, "Error setting wifi config: %s.", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "WiFi station configuration set successfully.");

    ret = esp_wifi_start();
    if(ret != ESP_OK)
    {
        ESP_LOGI(TAG, "Error starting wifi: %s.", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_connect();
    if(ret != ESP_OK)
    {
        ESP_LOGI(TAG, "Error connecting to WiFi: %s.", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "WiFi station started successfully.");
    return ESP_OK;
}
