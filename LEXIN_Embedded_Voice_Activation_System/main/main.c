#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"
#include "esp_adc/adc_continuous.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "hilexin.h"
#include "esp_crt_bundle.h"
#include "esp_rom_sys.h"   // esp_rom_delay_us for bit-bang timing
#include "driver/uart.h"   // for the MP3-TF-16P (DFPlayer Mini) serial link
#include "driver/i2c_master.h"
#include "ssd1306.h"
#include <ctype.h>

// ---- Fill these in locally, never share ----
#define WIT_TOKEN   "Wit AI Token Code"
#define WIFI_SSID   "WIFI Name"
#define WIFI_PASS   "WIFI Password"

// ---- Pins ----
#define ADC_CHANNEL     ADC_CHANNEL_3
#define SAMPLE_RATE     16000
#define LED_PIN         GPIO_NUM_2
#define BUZZER_PIN      GPIO_NUM_5
#define WHITE_LED_PIN   GPIO_NUM_15
#define BUTTON_PIN      GPIO_NUM_17

// ---- MAX7219 8x8 matrix pins (bit-banged, no hardware SPI needed) ----
#define MAX_DIN_PIN     GPIO_NUM_11
#define MAX_CLK_PIN     GPIO_NUM_12
#define MAX_CS_PIN      GPIO_NUM_10

// MAX7219 register addresses
#define MAX7219_REG_NOOP        0x00
#define MAX7219_REG_DECODEMODE  0x09
#define MAX7219_REG_INTENSITY   0x0A
#define MAX7219_REG_SCANLIMIT   0x0B
#define MAX7219_REG_SHUTDOWN    0x0C
#define MAX7219_REG_DISPLAYTEST 0x0F

// ---- MP3-TF-16P (DFPlayer Mini) UART link ----
#define DF_TX_PIN       GPIO_NUM_6     // ESP32 TX -> DFPlayer RX (through 1k resistor)
#define DF_RX_PIN       GPIO_NUM_16    // ESP32 RX <- DFPlayer TX
#define DF_UART_NUM     UART_NUM_1

// ---- "Mood" red LED ----
#define RED_LED_PIN     GPIO_NUM_18

// If any of DF_TX_PIN / DF_RX_PIN / RED_LED_PIN collide with how you've
// actually wired things, just change the numbers here — nothing else
// in the code needs to know.

// ---- GM009605V4 OLED (SSD1306, 128x64, I2C) ----
#define OLED_SDA_PIN    GPIO_NUM_8
#define OLED_SCL_PIN    GPIO_NUM_9

// ---- Recording: 3 seconds at 16kHz ----
#define RECORD_SAMPLES  (SAMPLE_RATE * 3)

static const char *TAG = "JARVIS";
static adc_continuous_handle_t adc_handle = NULL;
static bool wifi_connected = false;

// ---- WiFi event handler ----
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *data)
{
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        ESP_LOGI(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        ESP_LOGI(TAG, "WiFi connected!");
    }
}

// ---- WiFi init ----
static void init_wifi(void)
{
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                &wifi_event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    // Wait up to 10 seconds for connection
    int retries = 0;
    while (!wifi_connected && retries < 100) {
        vTaskDelay(pdMS_TO_TICKS(100));
        retries++;
    }
    if (!wifi_connected) {
        ESP_LOGE(TAG, "WiFi failed to connect!");
    }
}

// ---- ADC init ----
static void init_adc(void)
{
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 2048,
        .conv_frame_size = 256,
    };
    adc_continuous_new_handle(&adc_config, &adc_handle);
    adc_digi_pattern_config_t adc_pattern[1] = {{
        .atten     = ADC_ATTEN_DB_12,
        .channel   = ADC_CHANNEL,
        .unit      = ADC_UNIT_1,
        .bit_width = ADC_BITWIDTH_12,
    }};
    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = SAMPLE_RATE,
        .conv_mode      = ADC_CONV_SINGLE_UNIT_1,
        .format         = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
        .pattern_num    = 1,
        .adc_pattern    = adc_pattern,
    };
    adc_continuous_config(adc_handle, &dig_cfg);
    adc_continuous_start(adc_handle);
}

// ---- Read ADC samples ----
static void read_adc_samples(int16_t *out_buffer, int num_samples)
{
    int filled = 0;
    while (filled < num_samples) {
        uint8_t raw_buf[256];
        uint32_t out_len = 0;
        esp_err_t ret = adc_continuous_read(
            adc_handle, raw_buf, sizeof(raw_buf), &out_len, 1000);
        if (ret == ESP_OK) {
            int count = out_len / SOC_ADC_DIGI_RESULT_BYTES;
            for (int i = 0; i < count && filled < num_samples; i++) {
                adc_digi_output_data_t *p =
                    (adc_digi_output_data_t*)
                    &raw_buf[i * SOC_ADC_DIGI_RESULT_BYTES];
                out_buffer[filled++] =
                    (int16_t)(((int32_t)p->type2.data - 2048) * 8);
            }
        } else {
            break;
        }
    }
}

// ---- HTTP response buffer ----
static char http_response[1024];
static int  http_response_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int copy_len = evt->data_len;
        if (http_response_len + copy_len >= sizeof(http_response) - 1)
            copy_len = sizeof(http_response) - 1 - http_response_len;
        memcpy(http_response + http_response_len, evt->data, copy_len);
        http_response_len += copy_len;
        http_response[http_response_len] = '\0';
    }
    return ESP_OK;
}

// ---- Send audio to Wit.ai, return intent string ----
// Returns: "light_on", "love", "mood", or "unknown"
static const char* send_to_wit(int16_t *audio, int num_samples)
{
    http_response_len = 0;
    memset(http_response, 0, sizeof(http_response));

    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", WIT_TOKEN);

    esp_http_client_config_t config = {
        .url            = "https://api.wit.ai/speech?v=20240101",
        .method         = HTTP_METHOD_POST,
        .event_handler  = http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .skip_cert_common_name_check = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };


    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type",
                               "audio/raw;encoding=signed-integer;"
                               "bits=16;rate=16000;endian=little");

    // Send as raw PCM bytes
    int byte_len = num_samples * sizeof(int16_t);
    esp_http_client_set_post_field(client, (const char*)audio, byte_len);

    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        return "unknown";
    }

    ESP_LOGI(TAG, "Wit.ai response: %s", http_response);

    // Parse intent from response
    // Wit.ai returns JSON with "intents":[{"name":"light_on",...}]
    // Check intents first
    if (strstr(http_response, "\"name\": \"light_on\""))  return "light_on";
    if (strstr(http_response, "\"name\": \"love\""))      return "love";
    if (strstr(http_response, "\"name\": \"mood\""))      return "mood";

    // Fallback: check raw transcribed text for keywords
    // Convert response to lowercase for case-insensitive matching
    char lower_response[1024];
    strncpy(lower_response, http_response, sizeof(lower_response) - 1);
    lower_response[sizeof(lower_response) - 1] = '\0';
    for (int i = 0; lower_response[i]; i++) {
        lower_response[i] = tolower((unsigned char)lower_response[i]);
    }
    if (strstr(lower_response, "light"))  return "light_on";
    if (strstr(lower_response, "love"))   return "love";
    if (strstr(lower_response, "mood"))   return "mood";

    return "unknown";
    
}

// =====================================================================
// ---- MAX7219 8x8 LED matrix driver (bit-banged, no hardware SPI) ----
// =====================================================================

static void max7219_gpio_init(void)
{
    gpio_set_direction(MAX_DIN_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(MAX_CLK_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(MAX_CS_PIN,  GPIO_MODE_OUTPUT);
    gpio_set_level(MAX_CLK_PIN, 0);
    gpio_set_level(MAX_CS_PIN, 1);   // CS idles high
}

// Shifts out one byte, MSB first
static void max7219_send_byte(uint8_t data)
{
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(MAX_CLK_PIN, 0);
        gpio_set_level(MAX_DIN_PIN, (data >> i) & 0x01);
        esp_rom_delay_us(1);
        gpio_set_level(MAX_CLK_PIN, 1);   // MAX7219 latches DIN on rising edge
        esp_rom_delay_us(1);
    }
}

// Writes one 16-bit (register, data) frame
static void max7219_write(uint8_t reg, uint8_t data)
{
    gpio_set_level(MAX_CS_PIN, 0);   // begin frame
    max7219_send_byte(reg);
    max7219_send_byte(data);
    gpio_set_level(MAX_CS_PIN, 1);   // latch on CS rising edge
}

static void max7219_clear(void)
{
    for (int row = 1; row <= 8; row++) {
        max7219_write(row, 0x00);
    }
}

static void max7219_init(void)
{
    max7219_gpio_init();
    max7219_write(MAX7219_REG_DISPLAYTEST, 0x00); // normal operation
    max7219_write(MAX7219_REG_SCANLIMIT,   0x07); // drive all 8 rows
    max7219_write(MAX7219_REG_DECODEMODE,  0x00); // no BCD decode, raw pixels
    max7219_write(MAX7219_REG_INTENSITY,   0x08); // brightness 0x00-0x0F
    max7219_write(MAX7219_REG_SHUTDOWN,    0x01); // wake up from shutdown
    max7219_clear();
}

// 8x8 heart bitmap, one byte per row (MSB = leftmost column)
// Rotated 90 degrees clockwise from the original "point down" heart —
// the point now faces left.
static const uint8_t heart_bitmap[8] = {
    0x1C,
    0x3E,
    0x7E,
    0xFC,
    0xFC,
    0x7E,
    0x3E,
    0x1C
};

static void max7219_display_heart(void)
{
    for (int row = 0; row < 8; row++) {
        max7219_write(row + 1, heart_bitmap[row]);
    }
}

// =====================================================================
// ---- MP3-TF-16P (DFPlayer Mini) driver — simple UART command frames ----
// =====================================================================

#define DF_START_BYTE   0x7E
#define DF_VERSION      0xFF
#define DF_FRAME_LEN    0x06
#define DF_NO_FEEDBACK  0x00
#define DF_END_BYTE     0xEF

#define DF_CMD_PLAY_TRACK   0x03   // play track N from SD card root
#define DF_CMD_SET_VOLUME   0x06   // 0-30
#define DF_CMD_STOP         0x16

// Builds and sends one 10-byte DFPlayer command frame
static void df_send_command(uint8_t cmd, uint8_t param1, uint8_t param2)
{
    uint8_t frame[10];
    frame[0] = DF_START_BYTE;
    frame[1] = DF_VERSION;
    frame[2] = DF_FRAME_LEN;
    frame[3] = cmd;
    frame[4] = DF_NO_FEEDBACK;
    frame[5] = param1;
    frame[6] = param2;

    // Checksum = two's complement of the sum of bytes[1..6]
    uint16_t sum = 0;
    for (int i = 1; i <= 6; i++) sum += frame[i];
    uint16_t checksum = (uint16_t)(0 - sum);
    frame[7] = (checksum >> 8) & 0xFF;
    frame[8] = checksum & 0xFF;
    frame[9] = DF_END_BYTE;

    uart_write_bytes(DF_UART_NUM, (const char*)frame, sizeof(frame));
}

static void df_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(DF_UART_NUM, &uart_config);
    uart_set_pin(DF_UART_NUM, DF_TX_PIN, DF_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(DF_UART_NUM, 256, 0, 0, NULL, 0);

    // DFPlayer needs ~1-3s after power-up to mount the SD card
    vTaskDelay(pdMS_TO_TICKS(1500));

    df_send_command(DF_CMD_SET_VOLUME, 0x00, 20);  // volume 0-30, adjust to taste
}

// Plays /0001.mp3, /0002.mp3, ... from the root of the SD card
static void df_play_track(uint16_t track_num)
{
    df_send_command(DF_CMD_PLAY_TRACK, (track_num >> 8) & 0xFF, track_num & 0xFF);
}

static void df_stop(void)
{
    df_send_command(DF_CMD_STOP, 0, 0);
}

// =====================================================================
// ---- GM009605V4 OLED (SSD1306, 128x64) status display ----
// =====================================================================

static i2c_master_bus_handle_t i2c0_bus_hdl = NULL;
static ssd1306_handle_t oled_hdl = NULL;

static void oled_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port  = I2C_NUM_0,
        .sda_io_num = OLED_SDA_PIN,
        .scl_io_num = OLED_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &i2c0_bus_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED I2C bus init failed: %s", esp_err_to_name(err));
        return;
    }

    ssd1306_config_t dev_cfg = I2C_SSD1306_128x64_CONFIG_DEFAULT;
    ssd1306_init(i2c0_bus_hdl, &dev_cfg, &oled_hdl);
    if (oled_hdl == NULL) {
        ESP_LOGE(TAG, "OLED handle init failed");
        return;
    }

    ssd1306_clear_display(oled_hdl, false);
}

// Shows up to two lines of status text, clearing whatever was there before.
// Safe to call even if the OLED failed to init — it just does nothing.
static void oled_show(const char *line0, const char *line1)
{
    if (oled_hdl == NULL) return;

    ssd1306_clear_display(oled_hdl, false);
    if (line0 != NULL) {
        ssd1306_display_text(oled_hdl, 0, line0, false);
    }
    if (line1 != NULL) {
        ssd1306_display_text(oled_hdl, 1, line1, false);
    }
}

void app_main(void)
{
    // GPIO init
    gpio_set_direction(LED_PIN,       GPIO_MODE_OUTPUT);
    gpio_set_direction(BUZZER_PIN,    GPIO_MODE_OUTPUT);
    gpio_set_direction(WHITE_LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(WHITE_LED_PIN, 0);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(BUTTON_PIN);
    gpio_set_direction(RED_LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RED_LED_PIN, 0);

    // MAX7219 heart display init
    max7219_init();

    // DFPlayer Mini (MP3-TF-16P) init
    df_init();

    // OLED status display init
    oled_init();

    // Connect WiFi
    init_wifi();
    init_adc();

    // Load WakeNet
    srmodel_list_t *models = esp_srmodel_init("model");
    char *model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, "hilexin");
    esp_wn_iface_t *wakenet =
        (esp_wn_iface_t*)esp_wn_handle_from_name(model_name);
    model_iface_data_t *model_data =
        wakenet->create(model_name, DET_MODE_90);
    int chunksize = wakenet->get_samp_chunksize(model_data);
    printf("chunksize: %d\n", chunksize);

    // Main detection buffer
    int16_t *buffer = (int16_t*)malloc(chunksize * sizeof(int16_t));

    // Recording buffer — 3 seconds of audio
    int16_t *rec_buffer = (int16_t*)malloc(RECORD_SAMPLES * sizeof(int16_t));
    if (!rec_buffer) {
        ESP_LOGE(TAG, "Failed to allocate recording buffer!");
        return;
    }

    static bool light_on = false;
    printf("Listening for Hi Lexin...\n");
    oled_show("Listening...", NULL);

    while (1) {
        // Button check
        if (gpio_get_level(BUTTON_PIN) == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get_level(BUTTON_PIN) == 0) {
                light_on = false;
                gpio_set_level(WHITE_LED_PIN, 0);
                printf("Button — light off\n");
                while (gpio_get_level(BUTTON_PIN) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        }

        // Wake word detection
        read_adc_samples(buffer, chunksize);
        wakenet_state_t wn_state = wakenet->detect(model_data, buffer);

        if (wn_state == WAKENET_DETECTED) {
            printf("Hi Lexin detected! Recording command...\n");
            oled_show("Recording...", NULL);

            // Confirmation beep
            gpio_set_level(LED_PIN,    1);
            gpio_set_level(BUZZER_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(LED_PIN,    0);
            gpio_set_level(BUZZER_PIN, 0);

            // Record 3 seconds of audio
            printf("Recording...\n");
            read_adc_samples(rec_buffer, RECORD_SAMPLES);
            printf("Recording done. Sending to Wit.ai...\n");

            if (!wifi_connected) {
                printf("No WiFi — skipping recognition\n");
                oled_show("No WiFi", "Skipping...");
                vTaskDelay(pdMS_TO_TICKS(1500));
                oled_show("Listening...", NULL);
                continue;
            }

            // Send to Wit.ai and get intent
            const char *intent = send_to_wit(rec_buffer, RECORD_SAMPLES);
            printf("Intent: %s\n", intent);

            if (strcmp(intent, "light_on") == 0) {
                light_on = true;
                gpio_set_level(WHITE_LED_PIN, 1);
                printf("Light: ON\n");
                oled_show("Lights On", NULL);
                // Double beep for confirmation
                gpio_set_level(BUZZER_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_set_level(BUZZER_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_set_level(BUZZER_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_set_level(BUZZER_PIN, 0);
            } else if (strcmp(intent, "love") == 0) {
                printf("Love command — displaying heart on MAX7219 (press button to clear)\n");
                oled_show("Showing Love", "Press button");
                max7219_display_heart();
                // Hold the heart on screen until the button (GPIO17) is pressed.
                // Button is active-low with the internal pull-up enabled.
                while (gpio_get_level(BUTTON_PIN) == 1) {
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                vTaskDelay(pdMS_TO_TICKS(50));   // debounce
                max7219_clear();
                printf("Button pressed — heart cleared\n");
                // Wait for release so the outer button-check loop (which
                // turns off the white LED) doesn't also fire on this press.
                while (gpio_get_level(BUTTON_PIN) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            } else if (strcmp(intent, "mood") == 0) {
                printf("Mood command — red light on, playing track (press button to stop)\n");
                oled_show("Setting Mood", "Press button");
                gpio_set_level(RED_LED_PIN, 1);
                df_play_track(1);   // plays 0001.mp3 from the SD card root
                // Hold, same pattern as the heart, until the button is pressed
                while (gpio_get_level(BUTTON_PIN) == 1) {
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                vTaskDelay(pdMS_TO_TICKS(50));   // debounce
                df_stop();
                gpio_set_level(RED_LED_PIN, 0);
                printf("Button pressed — mood off\n");
                while (gpio_get_level(BUTTON_PIN) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            } else {
                printf("Command not recognized\n");
                oled_show("Sorry, I did not", "hear that. Try again");
                // Single low beep = didn't understand
                gpio_set_level(BUZZER_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(500));
                gpio_set_level(BUZZER_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(1500));  // let the message be readable
            }

            oled_show("Listening...", NULL);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    wakenet->destroy(model_data);
    free(buffer);
    free(rec_buffer);
}