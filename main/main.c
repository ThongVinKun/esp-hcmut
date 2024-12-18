#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"

#include "ssd1306.h"

// Define GGURL
#define SERVER_URL "https://script.google.com/macros/s/AKfycbwmrmEyB9YCGmGB9exB4-NzqBHdx9YVXBeWGA5VKhyKm8brUU1Or_lAizQN4l-w-W-xDw/exec"

// I2C Config
#define I2C_SCL_PIN     GPIO_NUM_22   // I2C Clock
#define I2C_SDA_PIN     GPIO_NUM_21   // I2C Data
#define I2C_MASTER_FREQ_HZ  400000

// GPIO Definitions
#define IR_SENSOR_PIN   GPIO_NUM_17 // IR Sensor
#define SW420_PIN       GPIO_NUM_18 // Vibatrion Sensor
#define BUZZER_PIN      GPIO_NUM_27 // Buzzer
#define RED_LED_PIN     GPIO_NUM_25 // Temperature Warning

// Device Definitions
#define OLED_ADDR       0x3C
#define AHT30_SENSOR_ADDR   0x38
#define ACK_CHECK_EN 0x1    

// Wifi Definitions
#define WIFI_SSID "DEMO"
#define WIFI_PASSWORD "12345678"

// System State
typedef enum {
    IDLE,
    WARNING,
} system_state_t;
static system_state_t system_state = IDLE;

static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

static const char *TAG = "MULTI_TASK_PROJECT";

static ssd1306_handle_t oled_device;

// Variables
static float hum_percent = 0;
static float temp_celsius = 0;
static bool object_detected = false;
static bool vibration_state = false;

// I2C Initialization
static esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0));
    return ESP_OK;
}

// GPIO Config
static void gpio_config_init() {
    // Outputs
    gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << RED_LED_PIN) | (1ULL << BUZZER_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    gpio_config(&output_config);

    // Inputs
    gpio_config_t input_config = {
        .pin_bit_mask = (1ULL << IR_SENSOR_PIN) | (1ULL << SW420_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    gpio_config(&input_config);
}

// AHT30 Initialization
static esp_err_t aht30_init(void) {
    uint8_t init_cmd[] = {0xBE, 0x08, 0x00};
    vTaskDelay(pdMS_TO_TICKS(300));
    i2c_master_write_to_device(I2C_NUM_0, AHT30_SENSOR_ADDR, init_cmd, sizeof(init_cmd), pdTRUE);
    return ESP_OK;
}

// Read data from AHT30
static esp_err_t aht30_read_data(uint8_t *data, size_t len) {
    uint8_t measure_cmd[] = {0xAC, 0x33, 0x00};
    i2c_master_write_to_device(I2C_NUM_0, AHT30_SENSOR_ADDR, measure_cmd, sizeof(measure_cmd), pdTRUE);
    vTaskDelay(pdMS_TO_TICKS(80)); // Thời gian chờ AHT30 xử lý đo đạc
    return i2c_master_read_from_device(I2C_NUM_0, AHT30_SENSOR_ADDR, data, sizeof(data), pdTRUE);
}

// OLED Initialization
static void oled_init() {
    oled_device = ssd1306_create(I2C_NUM_0, OLED_ADDR);
    ssd1306_clear_screen(oled_device, 0x00);
}

// Vibration Sensor Task
void vibration_sensor_task(void *pvParameters) {
    while (1) {
        // Read vibration sensor
        vibration_state = (gpio_get_level(SW420_PIN) == 1);   // Vibrating = True
        ESP_LOGI(TAG, "Vibrating? %s", vibration_state ? "YES" : "NO");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// IR Sensor Task
void ir_sensor_task(void *pvParameters) {
    while (1) {
        object_detected = !(gpio_get_level(IR_SENSOR_PIN) == 1);
        // ESP_LOGI(TAG, "Object detected? %s", object_detected ? "YES" : "NO");
        vTaskDelay(pdMS_TO_TICKS(00));
    }
}

// Temperature and Humidity Task
void aht30_task(void *pvParameters) {
    ESP_ERROR_CHECK(aht30_init());
    while (1) {
        uint8_t data[7];
        esp_err_t ret = aht30_read_data(data, sizeof(data));
        if (ret == ESP_OK) {
            uint32_t humidity = ((data[1] << 16) | (data[2] << 8) | (data[3] << 0));
            humidity = humidity >> 4;

            uint32_t temperature = ((data[3] << 16) | (data[4] << 8) | data[5] << 0);
            temperature = temperature & 0xFFFFF;           

            // Convert humidiy & temperature
            hum_percent = (humidity * 100.0) / (1 << 20);
            temp_celsius = (temperature * 200.0 / (1 << 20)) - 50;
            // temp_celsius = 61;

        } else {
            ESP_LOGE(TAG, "Failed to read data from AHT30 sensor");
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// OLED Update Task
void oled_update_task(void *pvParameters) {
    char oled_buffer[64];
    while (1) {
        if (temp_celsius > 30.0) {
            ssd1306_clear_screen(oled_device, 0x00);
            ESP_LOGW(TAG, "HIGH TEMPERATURE");
            snprintf(oled_buffer, sizeof(oled_buffer), "HIGH TEMPERATURE!!");
            ssd1306_draw_string(oled_device, 0, 0, (const uint8_t *)oled_buffer, 12, 1);
            snprintf(oled_buffer, sizeof(oled_buffer), "Temperature: %.1f C", temp_celsius);
            ssd1306_draw_string(oled_device, 0, 16, (const uint8_t *)oled_buffer, 12, 1);       
            ssd1306_refresh_gram(oled_device);
            vTaskDelay(pdMS_TO_TICKS(1000));

        } else {
            ssd1306_clear_screen(oled_device, 0x00);
            // Temperature
            snprintf(oled_buffer, sizeof(oled_buffer), "Temperature: %.1f C", temp_celsius);
            // ESP_LOGI(TAG, "Line 1: %s", oled_buffer);
            ssd1306_draw_string(oled_device, 0, 0, (const uint8_t *)oled_buffer, 12, 1);
            
            // Humidity
            snprintf(oled_buffer, sizeof(oled_buffer), "Humidity: %.2f %%RH", hum_percent);
            // ESP_LOGI(TAG, "Line 2: %s", oled_buffer);
            ssd1306_draw_string(oled_device, 0, 16, (const uint8_t *)oled_buffer, 12, 1);

            // Vibration
            snprintf(oled_buffer, sizeof(oled_buffer), "%s", vibration_state ? "Vibrating" : "No vibration");
            // ESP_LOGI(TAG, "Line 3: %s", oled_buffer);
            ssd1306_draw_string(oled_device, 0, 32, (const uint8_t *)oled_buffer, 12, 1);

            // Object
            snprintf(oled_buffer, sizeof(oled_buffer), "%s", object_detected ? "Object detected" : "No object");
            // ESP_LOGI(TAG, "Line 4: %s", oled_buffer);
            ssd1306_draw_string(oled_device, 0, 48, (const uint8_t *)oled_buffer, 12, 1);

            ssd1306_refresh_gram(oled_device);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

//****************************************************************//

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Wi-Fi connecting...");
    }
    else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Wi-Fi disconnected, retrying...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG, "Wi-Fi connected, got IP");
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta()
{
    wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Wi-Fi initialized successfully");
}

esp_err_t http_event_handler(esp_http_client_event_handle_t evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA: %.*s", evt->data_len, (char *)evt->data);
        break;
    default:
        break;
    }
    return ESP_OK;
}

// Task to Post Data
void post_data_task(void *pvParameters) {
    while (1) {
        char full_url[256];
        snprintf(full_url, sizeof(full_url), "%s?temp=%.2f&hum=%.2f&obs=%d&vib=%d", 
                SERVER_URL, 
                temp_celsius, 
                hum_percent, 
                object_detected ? 1 : 0, 
                vibration_state ? 1 : 0);

        esp_http_client_config_t config = {
            .url = full_url,
            .method = HTTP_METHOD_GET,
            .event_handler = http_event_handler,
            .disable_auto_redirect = true,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_err_t err = esp_http_client_perform(client);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "HTTP GET request successful, status: %d", 
                     esp_http_client_get_status_code(client));
        } else {
            ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        }

        esp_http_client_cleanup(client);

        // Post data every 2 seconds
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// Main task
void main_task(void *pvPrameters) {
    while (1) {
        switch (system_state) {
            case IDLE:
                if (temp_celsius > 30.0) {
                    system_state = WARNING;
                }
                break;

            case WARNING:
                // LED on
                gpio_set_level(RED_LED_PIN, 1);

                // Buzzer triggered
                for (int i = 0; i < 5; i++) {
                    gpio_set_level(BUZZER_PIN, 1);
                    vTaskDelay(pdMS_TO_TICKS(500)); 
                    gpio_set_level(BUZZER_PIN, 0);
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
                system_state = IDLE;
                ssd1306_refresh_gram(oled_device);
                gpio_set_level(RED_LED_PIN, 0);
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main (void) {
    // Initialize I2C
    ESP_ERROR_CHECK(i2c_master_init());

    // Oled init
    oled_init();

    // NVS & Wifi init
    nvs_flash_init();
    wifi_init_sta();

    // GPIOI Config Init
    gpio_config_init();

    // Create tasks with specified priorities (5>4>1=2>3)
    xTaskCreate(main_task, "System", 2048, NULL, 5, NULL);
    xTaskCreate(vibration_sensor_task, "vibration_sensor", 2048, NULL, 2, NULL);
    xTaskCreate(ir_sensor_task, "ir_sensor", 2048, NULL, 2, NULL);
    xTaskCreate(aht30_task, "aht30_read", 2048, NULL, 2, NULL);
    xTaskCreate(post_data_task, "post_data", 4096, NULL, 1, NULL);
    xTaskCreate(oled_update_task, "oled_update", 2048, NULL, 1, NULL);
}