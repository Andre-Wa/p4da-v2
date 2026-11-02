/**
 * audio_tags.c — ver audio_tags.h. Best-effort deliberado: metadado é
 * conforto de tela, não integridade — tag truncada/esquisita vira string
 * vazia ou parcial, nunca erro de playback.
 */
#include "audio_tags.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static void copy_trim(char *dst, size_t sz, const char *src, size_t n)
{
    if (n >= sz) n = sz - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
    /* trim de \0 e espaços nas pontas (ID3 paddea com \0) */
    while (n && (dst[n - 1] == 0 || dst[n - 1] == ' ')) dst[--n] = 0;
    size_t b = 0;
    while (dst[b] == ' ') b++;
    if (b) memmove(dst, dst + b, n - b + 1);
}

/* utf16 (com ou sem BOM) -> latin1 best-effort */
static void utf16_to_latin1(char *dst, size_t sz, const uint8_t *p, size_t n)
{
    size_t o = 0, i = 0;
    if (n >= 2 && ((p[0] == 0xFF && p[1] == 0xFE) || (p[0] == 0xFE && p[1] == 0xFF))) {
        /* BOM: só o LE é dobrado simples; BE troca a ordem */
        bool be = (p[0] == 0xFE);
        i = 2;
        for (; i + 1 < n && o + 1 < sz; i += 2) {
            uint16_t c = be ? (uint16_t)((p[i] << 8) | p[i + 1])
                            : (uint16_t)(p[i] | (p[i + 1] << 8));
            dst[o++] = (char)(c < 256 ? c : '?');
        }
        dst[o] = 0;
        copy_trim(dst, sz, dst, o);
        return;
    }
    for (; i + 1 < n && o + 1 < sz; i += 2) {
        uint16_t c = (uint16_t)(p[i] | (p[i + 1] << 8));   /* LE default */
        dst[o++] = (char)(c < 256 ? c : '?');
    }
    dst[o] = 0;
    copy_trim(dst, sz, dst, o);
}

static void store(uint8_t enc, const uint8_t *v, size_t n, char *dst, size_t sz)
{
    if (n == 0) return;
    if (enc == 1 || enc == 2) { utf16_to_latin1(dst, sz, v, n); return; }
    /* 0=latin1, 3=utf8: passa direto (a UI é utf8; latin1 de acentos
     * aparecerá mojibake só se o tagger usou latin1 — raro hoje) */
    if (enc == 0) { copy_trim(dst, sz, (const char *)v, n); return; }
    copy_trim(dst, sz, (const char *)v, n);
}

static uint32_t syncsafe(const uint8_t *p)
{
    return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) |
           ((uint32_t)(p[2] & 0x7F) << 7) | (uint32_t)(p[3] & 0x7F);
}
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void parse_id3v2(FILE *f, audio_tags_t *t)
{
    uint8_t h[10];
    if (fread(h, 1, 10, f) != 10 || memcmp(h, "ID3", 3)) return;
    int ver = h[3];
    if (ver < 2 || ver > 4) return;
    uint32_t sz = (ver == 4) ? syncsafe(h + 6) : be32(h + 6);
    if (sz > 1u << 20) return;              /* tag > 1 MB: desconfia */
    uint32_t pos = 0;
    while (pos + 10 <= sz) {
        uint8_t fh[10];
        if (fread(fh, 1, 10, f) != 10) return;
        pos += 10;
        char id[5] = { 0 };
        uint32_t fsz;
        uint8_t hdr_sz = 10;
        if (ver == 2) {
            /* frames v2.2: id de 3 chars + size de 3 bytes (header 6 bytes:
             * já lemos 10 — reposiciona) */
            fseek(f, -10, SEEK_CUR);
            uint8_t f6[6];
            if (fread(f6, 1, 6, f) != 6) return;
            pos -= 4;
            hdr_sz = 6;
            memcpy(id, f6, 3);
            fsz = ((uint32_t)f6[3] << 16) | ((uint32_t)f6[4] << 8) | f6[5];
        } else {
            memcpy(id, fh, 4);
            fsz = (ver == 4) ? syncsafe(fh + 4) : be32(fh + 4);
        }
        (void)hdr_sz;
        if (fsz == 0 || pos + fsz > sz + 10) return;
        if (id[0] < '0' || id[0] > 'z') return;   /* padding/lixo: fim */
        uint8_t *v = malloc(fsz);
        if (!v) return;
        if (fread(v, 1, fsz, f) != fsz) { free(v); return; }
        pos += fsz;
        uint8_t enc = v[0];
        if (!strcmp(id, "TIT2") || !strcmp(id, "TT2"))
            store(enc, v + 1, fsz - 1, t->title, sizeof(t->title));
        else if (!strcmp(id, "TPE1") || !strcmp(id, "TP1"))
            store(enc, v + 1, fsz - 1, t->artist, sizeof(t->artist));
        else if (!strcmp(id, "TALB") || !strcmp(id, "TAL"))
            store(enc, v + 1, fsz - 1, t->album, sizeof(t->album));
        free(v);
    }
}

static void parse_wav_info(FILE *f, audio_tags_t *t)
{
    uint8_t h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
        return;
    for (;;) {
        uint8_t c[8];
        if (fread(c, 1, 8, f) != 8) return;
        uint32_t csz = (uint32_t)c[4] | ((uint32_t)c[5] << 8) |
                       ((uint32_t)c[6] << 16) | ((uint32_t)c[7] << 24);
        if (!memcmp(c, "LIST", 4) && csz >= 4) {
            uint8_t *b = malloc(csz);
            if (!b) return;
            if (fread(b, 1, csz, f) != csz) { free(b); return; }
            if (!memcmp(b, "INFO", 4)) {
                uint32_t p = 4;
                while (p + 8 <= csz) {
                    uint32_t esz = (uint32_t)b[p + 4] | ((uint32_t)b[p + 5] << 8) |
                                   ((uint32_t)b[p + 6] << 16) | ((uint32_t)b[p + 7] << 24);
                    if (p + 8 + esz > csz) break;
                    char *val = (char *)(b + p + 8);
                    if (!memcmp(b + p, "INAM", 4))
                        copy_trim(t->title, sizeof(t->title), val, esz);
                    else if (!memcmp(b + p, "IART", 4))
                        copy_trim(t->artist, sizeof(t->artist), val, esz);
                    else if (!memcmp(b + p, "IPRD", 4))
                        copy_trim(t->album, sizeof(t->album), val, esz);
                    p += 8 + esz + (esz & 1);
                }
            }
            free(b);
            return;
        }
        if (fseek(f, (long)((csz + 1u) & ~1u), SEEK_CUR)) return;
    }
}

void audio_tags_read(const char *path, audio_tags_t *out)
{
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f) return;
    uint8_t h[4] = { 0 };
    if (fread(h, 1, 4, f) == 4) {
        /* parsers relêem do zero (cada um confere a própria magia) */
        fseek(f, 0, SEEK_SET);
        if (!memcmp(h, "ID3", 3)) parse_id3v2(f, out);
        else if (!memcmp(h, "RIFF", 4)) parse_wav_info(f, out);
    }
    fclose(f);
}
