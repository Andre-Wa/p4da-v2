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

/** Pilha das tasks que chamam pcm_src_* ("spk_play" e "au_play").
 *
 *  POR QUE 32 KB (v2.3.2): mp3dec_decode_frame() do minimp3 declara
 *  `mp3dec_scratch_t scratch` NA PILHA — 16.256 bytes:
 *      syn[33][64] floats  8.448 B   (síntese do subband)
 *      grbuf[2][576] floats 4.608 B
 *      maindata[2815]       2.815 B  (bit reservoir + payload)
 *      gr_info[4] + scf[40] + ist_pos + bs  ~370 B
 *  E a cadeia inteira (player_task -> do_play -> pcm_src_open/read ->
 *  mp3_fill -> mp3dec_decode_frame -> L3_decode/mp3d_synth + stdio) mede
 *  20.856 B de high-water mark no host (tools/hosttest/host_stack_test.c,
 *  que roda o decode do mp3 REAL numa pilha-sonda). O alvo é riscv32/
 *  newlib: o log do crash da v2.3.1 exigiu >=18.368 B só na au_play.
 *  32 KB = medição + ~11,6 KB de folga para vprintf/FreeRTOS/ISR.
 *  (24 KB, o primeiro palpite, REPROVOU no teste: sobravam 3,7 KB.)
 *
 *  HISTÓRICO: os tamanhos anteriores eram da era WAV-only da v1
 *  (spk_play 8 KB, au_play 10 KB — este último já tinha sido aumentado
 *  de 6148 na M5.4.5 por outro estouro). O caminho WAV mede só 3.448 B,
 *  por isso nunca estourou. Com SOM.4 os dois ficaram pequenos: a
 *  v2.3.1 morreu em "Stack protection fault" na au_play, SP 8.140 B
 *  abaixo do limite (≈18 KB exigidos contra 10 KB dados).
 *
 *  Quem vigia este número: host_stack_test.c mede o high-water REAL e
 *  reprova se a constante não cobrir a medição + margem. Não baixar sem
 *  rodar o teste. */
#define AUDIO_PCM_TASK_STACK_BYTES (32 * 1024)

/** Margem exigida entre a medição de host e a constante acima (o alvo é
 *  riscv32: ponteiros de 4 B, mas vprintf/FreeRTOS diferem do glibc). */
#define AUDIO_PCM_STACK_MARGIN_BYTES (4 * 1024)

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
