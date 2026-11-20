/**
 * audio_scan.h — varredura do acervo de música (SOM.4, v2.3.2).
 *
 * Nasceu do M4 da rodada v2.3.1: a tela Música listava SÓ o topo de
 * music/, então faixa em subpasta não aparecia — e como a tela Arquivos
 * não toca áudio, não havia caminho até ela (a fila "Próxima" nunca tinha
 * o que tocar fora do topo). A varredura desce nas subpastas; a fila da
 * tela passa a cobrir tudo que a lista mostra.
 *
 * Módulo puro (dirent/stdio), testável no host como audio_playlist —
 * a decisão de DISPLAY (título do tag, prefixo da pasta) fica na UI.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Chamada por entrada tocável (.wav/.mp3) ou playlist (.m3u/.m3u8),
 *  em ordem de caminho relativo crescente (agrupa por pasta/álbum).
 *  `full` = caminho absoluto; `rel` = relativo a root (sem barra inicial). */
typedef void (*audio_scan_fn)(const char *full, const char *rel, void *ctx);

/** Varre `root` em profundidade.
 *  max_depth   = níveis de subpasta abaixo de root (0 = só o topo);
 *  max_entries = teto de entradas (<=0 = sem teto); ao bater, para e
 *                marca *truncated (a lista avisa o usuário em vez de
 *                fingir que mostrou tudo).
 *  Devolve o nº de entradas entregues ao callback, ou -1 se `root` não
 *  abre. Iterativo (pilha explícita de pastas): não recursa, para não
 *  comer a pilha da task de I/O. */
int audio_scan_tree(const char *root, int max_depth, int max_entries,
                    audio_scan_fn cb, void *ctx, bool *truncated);

#ifdef __cplusplus
}
#endif
