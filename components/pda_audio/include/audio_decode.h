/**
 * audio_decode.h — fonte de PCM unificada (SOM.4, v2.3.0): WAV PCM16 e
 * MP3 (minimp3, CC0, vendorizado em minimp3/) atrás da MESMA interface,
 * para os dois backends (falante/UAC) não saberem que formato existe.
 *
 * Saída sempre ESTÉREO 16-bit (mono é duplicado no módulo); taxa = a do
 * arquivo (44100/48000 aceitos; outras recusadas na abertura, como no
 * round 1 do SOM.1). MP3 é decodido sob demanda (chunk por chunk) — nada
 * de bufferizar a faixa inteira (a motivação do pedido: WAV 36 MB vs
 * MP3 7 MB no cartão).
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pcm_src pcm_src_t;

/** .wav / .mp3 (case-insensitive) — usado pelas listas da UI também. */
bool audio_path_is_playable(const char *name);

/** Detecta por magia (RIFF/ID3/sync-word), não só pela extensão. */
esp_err_t pcm_src_open(const char *path, pcm_src_t **out);

uint32_t pcm_src_rate(const pcm_src_t *s);
/** "wav" | "mp3" */
const char *pcm_src_kind(const pcm_src_t *s);

/** Decod/lê até `frames` frames estéreo em out (16-bit interleaved).
 *  true = conseguiu frames (pode ser menos que o pedido no fim);
 *  *got == 0 com true = EOF limpo; false = erro de leitura/codec. */
bool pcm_src_read(pcm_src_t *s, int16_t *out, int frames, int *got);

/** Resume pós-unplug: só WAV (offset de bytes no chunk data).
 * MP3 retorna false → caller recomeça do zero (log honesto). */
bool pcm_src_seek_bytes(pcm_src_t *s, uint32_t off);

void pcm_src_close(pcm_src_t *s);

#ifdef __cplusplus
}
#endif
