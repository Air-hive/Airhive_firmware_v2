#include "esp_wifi.h"
#include "esp_log.h"
#include "airhive_networking.h"
#include "driver/gpio.h"
#include "esp_task.h"
#include "esp_smartconfig.h"
#include "string.h"
#include "nvs_flash.h"

static const char* TAG = "Airhive-Networking";
static const char* airhive_namespace = "Airhive";
static nvs_handle_t airhive_nvs;
static bool sc_button_pressed = false;

ESP_EVENT_DEFINE_BASE(AIRHIVE_EVENT);

static wifi_config_t wifi_cfg = {
    .sta = {
        .ssid = "msaad",
        .password = "0123456seven",
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
        .pmf_cfg.required = false,
        .rm_enabled = 1,
        .btm_enabled = 1, // Enable BTM (Background Traffic Management)
        .mbo_enabled = 0, // Disable MBO (Multi-Band Operation) for now, not sure about 5GHz, TODO: review this.
        .ft_enabled = 1, // Enable FT (Fast Transition)
        .owe_enabled = 0, // disable OWE (Opportunistic Wireless Encryption)
        .transition_disable = 0, //For now we allow WPA2.
        .sae_pwe_h2e = WPA3_SAE_PWE_UNSPECIFIED, //for now, TODO: review this.
        .sae_pk_mode = WPA3_SAE_PK_MODE_AUTOMATIC, //for now, TODO: review this.
        .failure_retry_cnt = 3, // TODO: review this.
    }
};  // TODO: take a look at the other fields in wifi_config_t, see if we need to set them.

static esp_err_t airhive_wifi_sta_start()
{
    size_t temp_len = sizeof(wifi_cfg.sta.ssid);
    esp_err_t ret = nvs_get_str(airhive_nvs, "wifi_ssid", (char *)wifi_cfg.sta.ssid, &temp_len);
    if(ret == ESP_ERR_NVS_INVALID_LENGTH)
    {
        ESP_LOGE(TAG, "Stored SSID has length: %u, maximum allowed: %u", temp_len, sizeof(wifi_cfg.sta.ssid));
    }
    else if(ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "No WiFi SSID is stored on NVS.");
    }
    else if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error reading stored WiFi SSID: %s", esp_err_to_name(ret));
        return ret;
    }

    temp_len = sizeof(wifi_cfg.sta.password);
    ret = nvs_get_str(airhive_nvs, "wifi_pass", (char *)wifi_cfg.sta.password, &temp_len);
    if(ret == ESP_ERR_NVS_INVALID_LENGTH)
    {
        ESP_LOGE(TAG, "Stored WiFi password has length: %u, maximum allowed: %u", temp_len, sizeof(wifi_cfg.sta.ssid));
    }
    else if(ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "No WiFi password is stored on NVS.");
    }
    else if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error reading stored WiFi password: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "WiFi credentials retreival ended.");

    ret = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error setting wifi config: %s.", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "WiFi station configuration set successfully.");

    esp_wifi_set_ps(WIFI_PS_NONE); // Disable power save mode, otherwise very poor performance.

    ret = esp_wifi_start();
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error starting wifi: %s.", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_connect();
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error starting connection process to AP %s: %s", wifi_cfg.sta.ssid, esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    esp_err_t ret;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if(!sc_button_pressed)
        {
            ESP_LOGI(TAG, "Retrying to connect to the AP with SSID: %s ...", wifi_cfg.sta.ssid);
            gpio_set_level(AIRHIVE_WIFI_CONNECTED_LED, 0); // Turn off the connected LED.
            ret = esp_wifi_connect();
            if(ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Error in esp_wifi_connect() in event handlers: %s", esp_err_to_name(ret));
            }
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "WiFi station started.");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED)
    {
        ESP_LOGI(TAG, "WiFi station connected to AP with SSID: %s", wifi_cfg.sta.ssid);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG, "WiFi station got IP address.");
        gpio_set_level(AIRHIVE_WIFI_CONNECTED_LED, 1); // Turn on the connected LED.
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP)
    {
        ESP_LOGI(TAG, "WiFi station lost IP address.");
        gpio_set_level(AIRHIVE_WIFI_CONNECTED_LED, 0); // Turn off the connected LED.
    }
    else if (event_base == AIRHIVE_EVENT && event_id == AIRHIVE_SC_START)
    {
        ESP_LOGI(TAG, "Strarting Smart Config...");
        sc_button_pressed = true;
        ret = esp_wifi_stop();
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Error in esp_wifi_stop() in event handlers: %s", esp_err_to_name(ret));
        }
        ret = esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_V2);
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Error in esp_smartconfig_set_type() in event handlers: %s", esp_err_to_name(ret));
        }
        smartconfig_start_config_t sc_cfg = {
            .enable_log = false,
            .esp_touch_v2_enable_crypt = true,
            .esp_touch_v2_key = AIRHIVE_ESP_TOUCH_V2_KEY
        };
        esp_wifi_set_ps(WIFI_PS_NONE); // Disable power save mode, otherwise very poor performance.
        ret = esp_wifi_start();
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Error in esp_wifi_start() in event handlers: %s", esp_err_to_name(ret));
        }
        ret = esp_smartconfig_start(&sc_cfg);     // If it fails the user can press the button again.
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Error in esp_smartconfig_start() in event handlers: %s", esp_err_to_name(ret));
        }
        gpio_set_level(AIRHIVE_SMART_CONFIG_LED, 1); // Turn on the smart config LED.
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE)
    {
        ESP_LOGI(TAG, "Smart Config scan done.");
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL)
    {
        ESP_LOGI(TAG, "Smart Config found channel.");
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE)
    {
        ESP_LOGI(TAG, "Smart Config ACK sent.");
        esp_smartconfig_stop();
        gpio_set_level(AIRHIVE_SMART_CONFIG_LED, 0); // Turn off the smart config LED.
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD)
    {
        ESP_LOGI(TAG, "Smart Config got SSID and Password.");
        smartconfig_event_got_ssid_pswd_t* evt = (smartconfig_event_got_ssid_pswd_t*)event_data;
        
        ret = nvs_set_str(airhive_nvs, "wifi_ssid", (char *)evt->ssid);
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Error setting WiFi SSID: %s", esp_err_to_name(ret));
        }
        ret = nvs_set_str(airhive_nvs, "wifi_pass", (char *)evt->password);
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Error setting WiFi password: %s", esp_err_to_name(ret));
        }
        ESP_ERROR_CHECK(nvs_commit(airhive_nvs));

        ESP_LOGI(TAG, "New WiFi credentials storage ended.");

        sc_button_pressed = false;

        ret = airhive_wifi_sta_start();
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Error in airhive_wifi_sta_start() in event handlers: %s", esp_err_to_name(ret));
        }
    }
    else
    {
        ESP_LOGW(TAG, "Unhandled event: %s with ID: %" PRId32, event_base, event_id);
    }
}

void smart_config_isr_handler(void* arg)
{
    BaseType_t high_task_awoken = pdFALSE;
    esp_event_isr_post(AIRHIVE_EVENT, AIRHIVE_SC_START, NULL, 0, &high_task_awoken);
    if (high_task_awoken == pdTRUE) portYIELD_FROM_ISR();
}

//TODO: return the error code at the end after cleanup.
esp_err_t airhive_wifi_sta_init()
{
    ESP_LOGI(TAG, "Configuring WiFi GPIO pins...");

    gpio_config_t sc_trig_gpio_config = {
        .pin_bit_mask = 1ULL<<AIRHIVE_SMART_CONFIG_TRIG,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&sc_trig_gpio_config);

    esp_err_t ret = gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3 | ESP_INTR_FLAG_EDGE);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error installing GPIO ISR service: %s", esp_err_to_name(ret));
        return ret;
    }
    gpio_isr_handler_add(AIRHIVE_SMART_CONFIG_TRIG, smart_config_isr_handler, NULL);

    gpio_config_t connected_gpio_config = {
        .pin_bit_mask = 1ULL<<AIRHIVE_WIFI_CONNECTED_LED,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&connected_gpio_config);
    gpio_set_drive_capability(AIRHIVE_WIFI_CONNECTED_LED, GPIO_DRIVE_CAP_1);
    gpio_set_level(AIRHIVE_WIFI_CONNECTED_LED, 0);

    gpio_config_t smart_config_gpio_config = {
        .pin_bit_mask = 1ULL<<AIRHIVE_SMART_CONFIG_LED,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&smart_config_gpio_config);
    gpio_set_drive_capability(AIRHIVE_SMART_CONFIG_LED, GPIO_DRIVE_CAP_1);
    gpio_set_level(AIRHIVE_SMART_CONFIG_LED, 0);

    ESP_LOGI(TAG, "WiFi GPIO pins configured successfully.");

    ESP_LOGI(TAG, "Initializing WiFi station...");

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&init_cfg);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error initializing wifi: %s.", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "WiFi initialized successfully.");
    
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error setting wifi mode: %s.", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error registering wifi event handler: %s.", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error registering IP event handler: %s.", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_instance_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error registering smart config event handler: %s.", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_event_handler_instance_register(AIRHIVE_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error registering Airhive event handler: %s.", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_open(airhive_namespace, NVS_READWRITE, &airhive_nvs);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening NVS: %s.", esp_err_to_name(ret));
        return ret;
    }

    ret = airhive_wifi_sta_start();
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error starting WiFi station: %s.", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "WiFi station started successfully.");
    return ESP_OK;
}
