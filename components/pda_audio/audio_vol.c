/**
 * audio_vol.c — dono dos volumes por fonte (ver audio_vol.h).
 *
 * Debounce NVS: mudança aplica NA HORA no backend; a gravação espera 3 s
 * de quietude (one-shot rearmed a cada mudança) — slider de 0→100 em um
 * gesto = 1 write, não 100.
 */
#include "audio_vol.h"
#include "audio_i2s.h"
#include "audio_uac.h"
#include "board_config.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

static const char *TAG = "audio_vol";

static int s_vol[2] = { BOARD_SPK_VOL_DEFAULT, 70 };
static esp_timer_handle_t s_save_t[2] = { NULL, NULL };
static const char *s_key[2] = { "vol_i", "vol_u" };

static void save_now(int src)
{
    nvs_handle_t h;
    if (nvs_open("pdaaudio", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, s_key[src], (uint8_t)s_vol[src]);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "volume %s=%d persistido", s_key[src], s_vol[src]);
}

static void save_cb(void *arg)
{
    save_now((int)(intptr_t)arg);
}

void audio_vol_init(void)
{
    nvs_handle_t h;
    if (nvs_open("pdaaudio", NVS_READONLY, &h) == ESP_OK) {
        uint8_t v;
        if (nvs_get_u8(h, "vol_i", &v) == ESP_OK && v <= 100) s_vol[0] = v;
        if (nvs_get_u8(h, "vol_u", &v) == ESP_OK && v <= 100) s_vol[1] = v;
        nvs_close(h);
    }
    for (int i = 0; i < 2; i++) {
        esp_timer_create_args_t a = {
            .callback = save_cb,
            .arg = (void *)(intptr_t)i,
            .name = i ? "vol_save_u" : "vol_save_i",
        };
        if (esp_timer_create(&a, &s_save_t[i]) != ESP_OK) s_save_t[i] = NULL;
    }
    ESP_LOGI(TAG, "volumes: falante=%d usb=%d", s_vol[0], s_vol[1]);
}

int audio_vol_get(audio_vol_src_t src) { return s_vol[src == AUDIO_VOL_UAC ? 1 : 0]; }

void audio_vol_set(audio_vol_src_t src, int pct)
{
    int i = (src == AUDIO_VOL_UAC) ? 1 : 0;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (pct == s_vol[i]) return;
    s_vol[i] = pct;
    if (i == 0) audio_i2s_set_vol(pct);
    else audio_uac_volume_set(pct);
    /* debounce: rearma o one-shot de 3 s (trailing garantido) */
    if (s_save_t[i]) {
        esp_timer_stop(s_save_t[i]);              /* parado = inofensivo */
        esp_timer_start_once(s_save_t[i], 3 * 1000 * 1000);
    } else {
        save_now(i);                              /* sem timer: síncrono */
    }
}

void audio_vol_step(audio_vol_src_t src, int delta)
{
    audio_vol_set(src, audio_vol_get(src) + delta);
}
