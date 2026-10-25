/**
 * audio_route.h — roteamento da saída de música (SOM.2, v2.2.0).
 *
 * AUTO (default): UAC se houver device conectado, senão falante local.
 * A escolha é feita NO PLAY de cada faixa (trocar o destino no meio de
 * uma faixa não reabre o stream — vale na próxima); o destino efetivo
 * da faixa atual é exposto p/ a UI.
 *
 * RAM-only de propósito: persistir a rota em system.lua é conforto da
 * fase SOM.6 (docs/AUDIO.md); por enquanto todo boot começa em AUTO.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_ROUTE_AUTO = 0,
    AUDIO_ROUTE_INTERNAL,   /* falante local (ES8311+NS4150) */
    AUDIO_ROUTE_UAC,        /* device USB (UAC1) */
} audio_route_t;

void audio_route_set(audio_route_t r);
audio_route_t audio_route_get(void);

/** Resolve AUTO contra o estado atual do UAC. */
bool audio_route_use_uac(void);

/** "auto" | "falante" | "USB" (p/ linha Saída da tela Música). */
const char *audio_route_label(void);

#ifdef __cplusplus
}
#endif
