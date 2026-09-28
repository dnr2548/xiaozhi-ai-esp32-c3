#include "board.h"
#include "audio/audio_codecs/audio_codec.h"
#include "audio/audio_codecs/no_audio_codec.h"
#include "display/no_display.h"
#include "system_info.h"
#include <wifi_station.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <driver/gpio.h>

#define TAG "ESP32C3_NRF_Board"

// Definisi Pin Software SPI untuk NRF24L01
#define NRF_CE_PIN   GPIO_NUM_0
#define NRF_CSN_PIN  GPIO_NUM_1
#define NRF_SCK_PIN  GPIO_NUM_10
#define NRF_MOSI_PIN GPIO_NUM_2
#define NRF_MISO_PIN GPIO_NUM_21

class Esp32C3NrfBoard : public Board {
private:
    uint8_t lampStates[4] = {0, 0, 0, 0};

    void saveStatesToNVS() {
        nvs_handle_t my_handle;
        esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
        if (err == ESP_OK) {
            nvs_set_blob(my_handle, "lamp_states", lampStates, sizeof(lampStates));
            nvs_commit(my_handle);
            nvs_close(my_handle);
        }
    }

    void loadStatesFromNVS() {
        nvs_handle_t my_handle;
        esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
        if (err == ESP_OK) {
            size_t required_size = sizeof(lampStates);
            nvs_get_blob(my_handle, "lamp_states", lampStates, &required_size);
            nvs_close(my_handle);
        }
    }

public:
    Esp32C3NrfBoard() {
        // Inisialisasi NVS dan muat kondisi terakhir
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            nvs_flash_init();
        }
        loadStatesFromNVS();
        ESP_LOGI(TAG, "Kondisi terakhir dimuat dari NVS: [%d, %d, %d, %d]", 
                 lampStates[0], lampStates[1], lampStates[2], lampStates[3]);
        
        // Inisialisasi pin GPIO untuk NRF24L01 (Software SPI / Direct GPIO)
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << NRF_CE_PIN) | (1ULL << NRF_CSN_PIN) | 
                               (1ULL << NRF_SCK_PIN) | (1ULL << NRF_MOSI_PIN);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);

        // Konfigurasi MISO sebagai Input
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << NRF_MISO_PIN);
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&io_conf);
    }

    virtual AudioCodec* GetAudioCodec() override {
        // Menggunakan codec audio bawaan untuk INMP441 / MAX98357A
        static NoAudioCodec audio_codec(AudioCodecConfig{
            .input_sample_rate = 16000,
            .output_sample_rate = 16000,
        });
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        static NoDisplay display;
        return &display;
    }

    // Fungsi untuk mengubah status dan menyimpan parameter terakhir
    void SetLampState(uint8_t index, uint8_t state) {
        if (index < 4) {
            lampStates[index] = state;
            saveStatesToNVS();
            ESP_LOGI(TAG, "Lampu %d diubah ke status: %d (Tersimpan di NVS)", index + 1, state);
        }
    }
};

DECLARE_BOARD(Esp32C3NrfBoard);