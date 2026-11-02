/* STUB de esp_codec_dev_defaults.h p/ syntaxcheck — ver stubs-i2s/README.md. */
#pragma once
#include "esp_codec_dev.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"   /* i2s_chan_handle_t p/ audio_codec_i2s_cfg_t */

typedef struct {
    int port;
    uint8_t addr;
    i2c_master_bus_handle_t bus_handle;
} audio_codec_i2c_cfg_t;

typedef struct {
    i2s_chan_handle_t tx_handle;
    i2s_chan_handle_t rx_handle;
} audio_codec_i2s_cfg_t;

const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(const audio_codec_i2c_cfg_t *cfg);
const audio_codec_gpio_if_t *audio_codec_new_gpio(void);
const audio_codec_data_if_t *audio_codec_new_i2s_data(const audio_codec_i2s_cfg_t *cfg);
