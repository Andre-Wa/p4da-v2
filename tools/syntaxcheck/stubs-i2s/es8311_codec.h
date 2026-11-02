/* STUB de es8311_codec.h p/ syntaxcheck — ver stubs-i2s/README.md. */
#pragma once
#include "esp_codec_dev.h"

#define ES8311_CODEC_DEFAULT_ADDR (0x18)

typedef struct {
    esp_codec_dev_work_mode_t codec_mode;
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    int pa_pin;
    bool use_mclk;
} es8311_codec_cfg_t;

const audio_codec_if_t *es8311_codec_new(const es8311_codec_cfg_t *cfg);
