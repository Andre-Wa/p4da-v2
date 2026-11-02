/**
 * audio_playlist.h — listas .m3u/.m3u8 (SOM.4, v2.3.0).
 *
 * Formato aceito: linhas de caminho, uma por faixa; '#' comenta;
 * diretivas #EXT* são IGNORADAS (não-viradas em erro); caminhos
 * relativos à pasta da playlist OU absolutos (/sdcard|/internal).
 * Entradas inexistentes/não-tocáveis são puladas com contagem (log na
 * UI via retorno). Lógica pura → host test.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*audio_playlist_fn)(const char *path, void *ctx);

/** Chama cb p/ cada entrada válida (na ordem). Devolve o nº de entradas;
 * -1 = playlist não abriu. `skipped` (opcional) recebe ignoradas. */
int audio_playlist_load(const char *m3u_path, audio_playlist_fn cb, void *ctx,
                        int *skipped);

bool audio_path_is_playlist(const char *name);   /* .m3u / .m3u8 */

#ifdef __cplusplus
}
#endif
