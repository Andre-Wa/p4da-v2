/* host_wav_test.c — WAV-PARSE (v2.2.0/L3): o parser compartilhado
 * (wav_parse.c, lógica pura) contra WAVs sintéticos gerados aqui:
 *   A canônico PCM16 estéreo 44k1 (off/len exatos);
 *   B fmt estendido (sz=18, cbSize=2) — o excedente é pulado;
 *   C LIST de tamanho ÍMPAR (pad de word) + FACT antes do data;
 *   D não-PCM (fmt=3, float) — rejeitado;
 *   E RIFF sem WAVE / fmt truncado — rejeitados;
 *   F mono 22050 — parse aceita (quem recusa taxa é o player, SOM.1).
 * Disciplina REGRESSOES_BUILD: parser novo nasce testado no host.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "wav_parse.h"

static int fails;
#define CHECK(cond, fmt, ...)                                   \
    do {                                                        \
        if (!(cond)) {                                          \
            fails++;                                            \
            printf("FAIL: " fmt "\n", ##__VA_ARGS__);           \
        }                                                       \
    } while (0)

#define ROOT "/tmp/wavtest"

static void put_u32(FILE *f, uint32_t v)
{
    fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f);
    fputc((v >> 16) & 0xff, f); fputc((v >> 24) & 0xff, f);
}
static void chunk(FILE *f, const char *id, uint32_t sz, const uint8_t *body)
{
    fwrite(id, 1, 4, f);
    put_u32(f, sz);
    if (sz) fwrite(body, 1, sz, f);
    /* RIFF: corpo de chunk tem pad p/ word alignment — escritores reais
     * emitem o pad; o parser pula (sz+1)&~1 e por isso exige o byte. */
    if (sz & 1u) fputc(0, f);
}
/* fmt body PCM: tag,ch,rate,,align,bits */
static void fmt_body(uint8_t *b, uint16_t tag, uint16_t ch, uint32_t rate,
                     uint16_t bits)
{
    memset(b, 0, 32);
    b[0] = tag & 0xff; b[1] = tag >> 8;
    b[2] = ch & 0xff; b[3] = ch >> 8;
    b[4] = rate & 0xff; b[5] = (rate >> 8) & 0xff;
    b[6] = (rate >> 16) & 0xff; b[7] = (rate >> 24) & 0xff;
    uint32_t bps = rate * ch * (bits / 8);
    b[8] = bps & 0xff; b[9] = (bps >> 8) & 0xff;
    b[10] = (bps >> 16) & 0xff; b[11] = (bps >> 24) & 0xff;
    uint16_t align = ch * (bits / 8);
    b[12] = align & 0xff; b[13] = align >> 8;
    b[14] = bits & 0xff; b[15] = bits >> 8;
}

static FILE *mk(const char *name)
{
    char p[160];
    snprintf(p, sizeof(p), ROOT "/%s", name);
    return fopen(p, "w+b");
}
static bool parse_file(const char *name, wav_info_t *w)
{
    char p[160];
    snprintf(p, sizeof(p), ROOT "/%s", name);
    return wav_parse_path(p, w);
}

int main(void)
{
    if (system("rm -rf " ROOT) != 0 || mkdir(ROOT, 0755) != 0) {
        printf("WAV TEST: prep falhou\n");
        return 2;
    }
    uint8_t b[40];

    /* ---- A: canônico ---- */
    {
        FILE *f = mk("a.wav");
        fwrite("RIFF", 1, 4, f); put_u32(f, 36 + 100); fwrite("WAVE", 1, 4, f);
        fmt_body(b, 1, 2, 44100, 16);
        chunk(f, "fmt ", 16, b);
        uint8_t pcm[100]; memset(pcm, 0x55, sizeof(pcm));
        chunk(f, "data", 100, pcm);
        fclose(f);
        wav_info_t w;
        CHECK(parse_file("a.wav", &w), "A: parse falhou");
        CHECK(w.channels == 2 && w.sample_rate == 44100 && w.bits == 16,
              "A: fmt errado %u/%lu/%u", w.channels,
              (unsigned long)w.sample_rate, w.bits);
        CHECK(w.data_len == 100, "A: data_len %u", w.data_len);
        /* offset: 12 + 8+16 + 8 = 44 */
        CHECK(w.data_off == 44, "A: data_off %ld (esperado 44)", w.data_off);
        printf("[wav] A: canônico PCM16 44k1 st (off/len exatos)  OK\n");
    }

    /* ---- B: fmt estendido (cbSize) ---- */
    {
        FILE *f = mk("b.wav");
        fwrite("RIFF", 1, 4, f); put_u32(f, 0); fwrite("WAVE", 1, 4, f);
        fmt_body(b, 1, 2, 48000, 16);
        b[16] = 2; b[17] = 0;                 /* cbSize = 2 */
        chunk(f, "fmt ", 18, b);
        uint8_t pcm[40]; memset(pcm, 0xAA, sizeof(pcm));
        chunk(f, "data", 40, pcm);
        fclose(f);
        wav_info_t w;
        CHECK(parse_file("b.wav", &w), "B: parse falhou");
        CHECK(w.sample_rate == 48000 && w.data_off == 46 && w.data_len == 40,
              "B: off/len errados %ld/%u", w.data_off, w.data_len);
        printf("[wav] B: fmt estendido (sz=18) pulado certo  OK\n");
    }

    /* ---- C: LIST ímpar + FACT antes do data ---- */
    {
        FILE *f = mk("c.wav");
        fwrite("RIFF", 1, 4, f); put_u32(f, 0); fwrite("WAVE", 1, 4, f);
        fmt_body(b, 1, 1, 44100, 16);
        chunk(f, "fmt ", 16, b);
        uint8_t list[5] = { 'I', 'N', 'F', 'O', 0x21 };   /* tamanho ímpar */
        chunk(f, "LIST", 5, list);                        /* +1 pad no skip */
        uint8_t fact[4] = { 4, 0, 0, 0 };
        chunk(f, "fact", 4, fact);
        uint8_t pcm[20]; memset(pcm, 0x77, sizeof(pcm));
        chunk(f, "data", 20, pcm);
        fclose(f);
        wav_info_t w;
        CHECK(parse_file("c.wav", &w), "C: parse falhou");
        CHECK(w.channels == 1 && w.data_len == 20, "C: fmt/len errados");
        /* 12 + 24(fmt) + 8+5+1(LIST+pad) + 8+4(FACT) + 8 = 70 */
        CHECK(w.data_off == 70, "C: data_off %ld (esperado 70 — pad ímpar)",
              w.data_off);
        printf("[wav] C: LIST ímpar + FACT antes do data (pad ok)  OK\n");
    }

    /* ---- D: não-PCM (IEEE float) ---- */
    {
        FILE *f = mk("d.wav");
        fwrite("RIFF", 1, 4, f); put_u32(f, 0); fwrite("WAVE", 1, 4, f);
        fmt_body(b, 3, 2, 44100, 32);
        chunk(f, "fmt ", 16, b);
        chunk(f, "data", 8, b);
        fclose(f);
        wav_info_t w;
        CHECK(!parse_file("d.wav", &w), "D: float devia ser rejeitado");
        printf("[wav] D: não-PCM rejeitado  OK\n");
    }

    /* ---- E: RIFF sem WAVE / fmt truncado ---- */
    {
        FILE *f = mk("e1.wav");
        fwrite("RIFF", 1, 4, f); put_u32(f, 8); fwrite("AVI ", 1, 4, f);
        fclose(f);
        wav_info_t w;
        CHECK(!parse_file("e1.wav", &w), "E: AVI devia falhar");
        f = mk("e2.wav");
        fwrite("RIFF", 1, 4, f); put_u32(f, 0); fwrite("WAVE", 1, 4, f);
        fwrite("fmt ", 1, 4, f); put_u32(f, 8); fwrite(b, 1, 8, f);  /* sz<16 */
        fclose(f);
        CHECK(!parse_file("e2.wav", &w), "E: fmt truncado devia falhar");
        printf("[wav] E: RIFF!=WAVE e fmt truncado rejeitados  OK\n");
    }

    /* ---- F: mono 22050 — parser aceita (player recusa a taxa) ---- */
    {
        FILE *f = mk("f.wav");
        fwrite("RIFF", 1, 4, f); put_u32(f, 0); fwrite("WAVE", 1, 4, f);
        fmt_body(b, 1, 1, 22050, 16);
        chunk(f, "fmt ", 16, b);
        chunk(f, "data", 10, b);
        fclose(f);
        wav_info_t w;
        CHECK(parse_file("f.wav", &w) && w.channels == 1 &&
                  w.sample_rate == 22050,
              "F: mono 22050 devia parsear");
        printf("[wav] F: mono 22050 parseia (recusa de taxa é do player)  OK\n");
    }

    if (fails) { printf("WAV TEST: %d FALHA(S)\n", fails); return 1; }
    printf("WAV TEST: OK (parser SOM.1: canônico, fmt ext, chunks ímpares, "
           "não-PCM, truncados, mono)\n");
    return 0;
}
