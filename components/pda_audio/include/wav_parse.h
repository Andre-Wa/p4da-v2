/**
 * wav_parse.h — parser de WAV PCM16 puro (SOM.1, v2.2.0).
 *
 * Extraído do audio_uac.c da v1 (M5a) para que o MESMO parser sirva aos
 * dois backends (UAC e falante local) e aos testes de host com WAVs
 * sintéticos (tools/hosttest/host_wav_test.c): fmt estendido, chunks
 * ímpares com pad, LIST/FACT antes do data, rejeição de não-PCM.
 *
 * Sem nenhuma dependência de IDF: só stdio — é lógica pura testável.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t channels;      /* 1 ou 2 (o player local duplica mono) */
    uint32_t sample_rate;   /* Hz */
    uint16_t bits;          /* 16 p/ os players atuais */
    long     data_off;      /* offset do chunk data no arquivo */
    uint32_t data_len;      /* bytes de PCM */
} wav_info_t;

/** Parseia a partir da posição atual do FILE (geralmente 0).
 * false = não é WAV PCM16 válido p/ os players (header, fmt!=1, truncado). */
bool wav_parse(FILE *f, wav_info_t *out);

/** Conveniência: abre o caminho, parseia, fecha. */
bool wav_parse_path(const char *path, wav_info_t *out);

#ifdef __cplusplus
}
#endif
