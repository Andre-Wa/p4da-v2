/* STUB de esp_codec_dev.h p/ syntaxcheck — ver stubs-i2s/README.md.
 * Só os campos/funções que audio_i2s.c usa; campos reais extras ficam de
 * fora de propósito (designated initializers não exigem struct completa). */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef void *esp_codec_dev_handle_t;
typedef struct audio_codec_ctrl_if_s audio_codec_ctrl_if_t;
typedef struct audio_codec_data_if_s audio_codec_data_if_t;
typedef struct audio_codec_gpio_if_s audio_codec_gpio_if_t;
typedef struct audio_codec_if_s audio_codec_if_t;

typedef enum { ESP_CODEC_DEV_TYPE_OUT, ESP_CODEC_DEV_TYPE_IN } esp_codec_dev_type_t;
typedef enum {
    ESP_CODEC_DEV_WORK_MODE_NONE,
    ESP_CODEC_DEV_WORK_MODE_ADC,
    ESP_CODEC_DEV_WORK_MODE_DAC,
} esp_codec_dev_work_mode_t;

typedef struct {
    uint32_t sample_rate;
    uint8_t channel;
    uint8_t bits_per_sample;
} esp_codec_dev_sample_info_t;

typedef struct {
    const audio_codec_if_t *codec_if;
    const audio_codec_data_if_t *data_if;
    esp_codec_dev_type_t dev_type;
} esp_codec_dev_cfg_t;

esp_codec_dev_handle_t esp_codec_dev_new(const esp_codec_dev_cfg_t *cfg);
int esp_codec_dev_open(esp_codec_dev_handle_t h, const esp_codec_dev_sample_info_t *fs);
int esp_codec_dev_write(esp_codec_dev_handle_t h, void *data, int len);
int esp_codec_dev_close(esp_codec_dev_handle_t h);
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t h, float vol);
