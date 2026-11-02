/* STUB de driver/i2s_std.h p/ syntaxcheck — ver stubs-i2s/README.md.
 * Assinaturas DERIVADAS DO USO em components/pda_audio/audio_i2s.c
 * (mesmo critério do stub usb/hid_host.h): o alvo é a autoridade. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

typedef void *i2s_chan_handle_t;
typedef int i2s_port_t;
#define I2S_NUM_0 ((i2s_port_t)0)
typedef enum { I2S_ROLE_MASTER = 0, I2S_ROLE_SLAVE } i2s_role_t;
typedef enum { I2S_DATA_BIT_WIDTH_16BIT = 16, I2S_DATA_BIT_WIDTH_32BIT = 32 } i2s_data_bit_width_t;
typedef enum { I2S_SLOT_MODE_MONO = 1, I2S_SLOT_MODE_STEREO = 2 } i2s_slot_mode_t;

typedef struct { uint32_t sample_rate_hz; float clk_coefficient; } i2s_std_clk_config_t;
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) \
    ((i2s_std_clk_config_t){ .sample_rate_hz = (rate), .clk_coefficient = 256 })

typedef struct {
    i2s_data_bit_width_t data_bit_width;
    i2s_data_bit_width_t slot_bit_width;
    i2s_slot_mode_t slot_mode;
} i2s_std_slot_config_t;
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, mode) \
    ((i2s_std_slot_config_t){ .data_bit_width = (bits), .slot_bit_width = (bits), \
                              .slot_mode = (mode) })

/* '.invert_flags' é STRUCT de bitfields no IDF 5.5.1 (causa raiz do
 * -Werror=missing-braces da v5.11.x na v1) — o stub mantém a forma. */
typedef struct {
    gpio_num_t mclk, bclk, ws, dout, din;
    struct { uint32_t mclk_inv : 1, bclk_inv : 1, ws_inv : 1; } invert_flags;
} i2s_std_gpio_config_t;

typedef struct {
    i2s_std_clk_config_t clk_cfg;
    i2s_std_slot_config_t slot_cfg;
    i2s_std_gpio_config_t gpio_cfg;
} i2s_std_config_t;

typedef struct { i2s_port_t id; i2s_role_t role; bool auto_clear; } i2s_chan_config_t;
#define I2S_CHANNEL_DEFAULT_CONFIG(port_id, rl) \
    ((i2s_chan_config_t){ .id = (port_id), .role = (rl), .auto_clear = true })

esp_err_t i2s_new_channel(const i2s_chan_config_t *cfg, i2s_chan_handle_t *tx,
                          i2s_chan_handle_t *rx);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t h, const i2s_std_config_t *cfg);
esp_err_t i2s_channel_enable(i2s_chan_handle_t h);
esp_err_t i2s_channel_disable(i2s_chan_handle_t h);
