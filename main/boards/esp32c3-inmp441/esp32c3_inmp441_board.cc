/**
 * ESP32-C3 with INMP441 Microphone, MAX98357A Speaker, SSD1306 OLED, 
 * 4 Channel Relays (NVS State), and Touch Sensor (TTP223)
 * 
 * Pin mapping follows config.h definitions.
 */

#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "application.h"
#include "button.h"
#include "config.h"

#include <esp_log.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_ssd1306.h>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <nvs_flash.h>
#include <nvs.h>

#define TAG "Esp32c3Inmp441Board"

class Esp32c3Inmp441Board : public WifiBoard {
private:
    Button boot_button_;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    uint8_t lampStates[4] = {0, 0, 0, 0};

    void SaveStatesToNVS() {
        nvs_handle_t my_handle;
        esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
        if (err == ESP_OK) {
            nvs_set_blob(my_handle, "lamp_states", lampStates, sizeof(lampStates));
            nvs_commit(my_handle);
            nvs_close(my_handle);
        }
    }

    void LoadStatesFromNVS() {
        nvs_handle_t my_handle;
        esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
        if (err == ESP_OK) {
            size_t required_size = sizeof(lampStates);
            nvs_get_blob(my_handle, "lamp_states", lampStates, &required_size);
            nvs_close(my_handle);
        }
    }

    void ApplyRelayHardwarePins() {
        gpio_set_level(RELAY_1_GPIO, lampStates[0]);
        gpio_set_level(RELAY_2_GPIO, lampStates[1]);
        gpio_set_level(RELAY_3_GPIO, lampStates[2]);
        gpio_set_level(RELAY_4_GPIO, lampStates[3]);
    }

    void InitializeRelaysAndTouch() {
        // Inisialisasi NVS untuk status lampu
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            nvs_flash_init();
        }
        LoadStatesFromNVS();

        // Konfigurasi Pin Relay sebagai Output (mengambil dari config.h)
        gpio_config_t relay_conf = {};
        relay_conf.intr_type = GPIO_INTR_DISABLE;
        relay_conf.mode = GPIO_MODE_OUTPUT;
        relay_conf.pin_bit_mask = (1ULL << RELAY_1_GPIO) | (1ULL << RELAY_2_GPIO) | 
                                  (1ULL << RELAY_3_GPIO) | (1ULL << RELAY_4_GPIO);
        relay_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        relay_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&relay_conf);

        // Terapkan status terakhir dari memori
        ApplyRelayHardwarePins();

        // Konfigurasi Pin Sensor Sentuh sebagai Input
        gpio_config_t touch_conf = {};
        touch_conf.intr_type = GPIO_INTR_DISABLE;
        touch_conf.mode = GPIO_MODE_INPUT;
        touch_conf.pin_bit_mask = (1ULL << TOUCH_SENSOR_GPIO);
        touch_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&touch_conf);

        ESP_LOGI(TAG, "4 Channel Relays & Touch Sensor initialized successfully.");
    }

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = true,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
        ESP_LOGI(TAG, "I2C bus initialized (SDA: %d, SCL: %d)", DISPLAY_SDA_PIN, DISPLAY_SCL_PIN);
    }

    void InitializeButtons() {
        boot_button_.OnPressDown([this]() {
            Application::GetInstance().StartListening();
        });
        boot_button_.OnPressUp([this]() {
            Application::GetInstance().StopListening();
        });
        
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
            }
        });
    }

public:
    Esp32c3Inmp441Board() : boot_button_(BOOT_BUTTON_GPIO) {
        ESP_LOGI(TAG, "Initializing ESP32-C3 INMP441 Board with OLED, Relays & Touch");
        
        InitializeI2c();
        InitializeButtons();
        InitializeRelaysAndTouch();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecDuplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN
        );
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        static Display* display = nullptr;
        
        if (display == nullptr) {
            uint8_t i2c_addresses[] = {0x3C, 0x3D};
            esp_lcd_panel_io_handle_t panel_io = nullptr;
            esp_lcd_panel_handle_t panel = nullptr;
            
            for (int i = 0; i < 2; i++) {
                uint8_t addr = i2c_addresses[i];
                
                esp_lcd_panel_io_i2c_config_t io_config = {
                    .dev_addr = addr,
                    .on_color_trans_done = nullptr,
                    .user_ctx = nullptr,
                    .control_phase_bytes = 1,
                    .dc_bit_offset = 6,
                    .lcd_cmd_bits = 8,
                    .lcd_param_bits = 8,
                    .flags = {
                        .dc_low_on_data = false,
                        .disable_control_phase = false,
                    },
                    .scl_speed_hz = 400000,
                };
                
                if (esp_lcd_new_panel_io_i2c(i2c_bus_, &io_config, &panel_io) != ESP_OK) {
                    continue;
                }

                esp_lcd_panel_dev_config_t panel_config = {
                    .reset_gpio_num = GPIO_NUM_NC,
                    .bits_per_pixel = 1,
                    .flags = {
                        .reset_active_high = false,
                    },
                    .vendor_config = nullptr,
                };
                
                if (esp_lcd_new_panel_ssd1306(panel_io, &panel_config, &panel) != ESP_OK) {
                    esp_lcd_panel_io_del(panel_io);
                    panel_io = nullptr;
                    continue;
                }
                
                if (esp_lcd_panel_reset(panel) != ESP_OK ||
                    esp_lcd_panel_init(panel) != ESP_OK ||
                    esp_lcd_panel_disp_on_off(panel, true) != ESP_OK) {
                    esp_lcd_panel_del(panel);
                    esp_lcd_panel_io_del(panel_io);
                    panel = nullptr;
                    panel_io = nullptr;
                    continue;
                }
                
                ESP_LOGI(TAG, "OLED initialized at address 0x%02X", addr);
                display = new OledDisplay(panel_io, panel, 
                                         DISPLAY_WIDTH, DISPLAY_HEIGHT, 
                                         DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
                break;
            }
            
            if (display == nullptr) {
                ESP_LOGW(TAG, "OLED not found, continuing without display");
                static NoDisplay no_display;
                display = &no_display;
            }
        }
        return display;
    }

    // Fungsi Publik untuk Mengontrol Relay
    void SetRelayState(int index, uint8_t state) {
        if (index >= 0 && index < 4) {
            lampStates[index] = state;
            ApplyRelayHardwarePins();
            SaveStatesToNVS();
            ESP_LOGI(TAG, "Relay %d set to %s", index + 1, state ? "ON" : "OFF");
        }
    }
};

DECLARE_BOARD(Esp32c3Inmp441Board);