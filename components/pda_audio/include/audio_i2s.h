#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Saída local: codec ES8311 (I2S0 + I2C compartilhado c/ GT911) +
 * amplificador NS4150 (PA_EN GPIO11). Dois consumidores do MESMO canal:
 *  - beep de UI (senoide 440 Hz, serializado por mutex desde a v5.13.0);
 *  - player de WAV PCM16 do cartão (SOM.1, v2.2.0): task "spk_play",
 *    44k1/48k, mono->estéreo, pause/resume, stop por flag.
 * Handshake beep×música (SOM.1): beep com música tocando suspende o
 * player entre chunks (fecha o codec), toca, e o player reabre na taxa
 * dele — um canal I2S, uma taxa por vez.
 */

esp_err_t audio_i2s_init(void);
bool audio_i2s_ready(void);

/** Senoide 440 Hz, estéreo, ~ms ms, volume atual. Serializado por mutex:
 *  2º beep na janela do 1º retorna ESP_ERR_INVALID_STATE sem tocar. */
esp_err_t audio_i2s_beep(int ms);

/** Volume do codec 0–100 (mapa do esp_codec_dev). */
void audio_i2s_set_vol(int pct);
int  audio_i2s_get_vol(void);
void audio_i2s_volume_step(int delta);
void audio_i2s_mute_toggle(void);

/* ---- player local (SOM.1) ---- */
/** Fila de 1 comando (troca direta = para a atual e toca a nova). */
esp_err_t audio_i2s_play(const char *path);
void audio_i2s_stop(void);
void audio_i2s_pause(void);
void audio_i2s_resume(void);
bool audio_i2s_playing(void);
bool audio_i2s_paused(void);
const char *audio_i2s_track(void);
/** "sem init"|"pronto"|"tocando"|"pausado"|"parado"|"fim da faixa"|
 *  "erro: ..." — humano, p/ linha Estado da tela Música. */
const char *audio_i2s_state(void);

/** Chama (de qualquer contexto) quando algo muda p/ UI refrescar. */
typedef void (*audio_i2s_event_cb)(void *ctx);
void audio_i2s_set_event_cb(audio_i2s_event_cb cb, void *ctx);

#ifdef __cplusplus
}
#endif
