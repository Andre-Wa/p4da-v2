/**
 * audio_tags.h — metadados mínimos p/ tela Música (SOM.4, v2.3.0).
 *
 * MP3: ID3v2.2/2.3/2.4 — TIT2/TT2 (título), TPE1/TP1 (artista),
 * TALB/TAL (álbum). Encodings: latin1 e utf8 passam direto; utf16(v2.3/4)
 * vira best-effort latin1 ( dobra 16->8 bits ) p/ não mostrar lixo —
 * acentos comuns sobrevivem, símbolos raros não.
 * WAV: chunk LIST/INFO (INAM título, IART artista, IPRD álbum).
 * Sem tag: strings vazias — a UI cai no basename (comportamento v2.2.x).
 * Lógica pura (stdio) → testada no host.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char title[64];
    char artist[64];
    char album[64];
} audio_tags_t;

void audio_tags_read(const char *path, audio_tags_t *out);

#ifdef __cplusplus
}
#endif
