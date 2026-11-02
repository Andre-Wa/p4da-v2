/**
 * audio_decode.c — WAV/MP3 -> PCM estéreo (ver audio_decode.h).
 *
 * MP3 via minimp3 (header-only, CC0 — minimp3/LICENSE): decodificação
 * fixa-ponto; aqui roda na task do player (au_play/spk_play, pilha
 * PSRAM). API real do minimp3: mp3dec_decode_frame() devolve amostras
 * POR CANAL (pcm recebe samples*channels), info.frame_bytes = consumo,
 * info.hz/channels do frame; ID3v1/2 o próprio decoder pula (retorna 0
 * amostras com frame_bytes>0).
 *
 * Janela de entrada de 16 KB: cabe um frame MPEG inteiro (máx ~1440 B
 * @320 kbps) + folga; frame_bytes==0 com janela cheia = lixo/resync
 * (avança 1 byte).
 */

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "audio_decode.h"
#include "wav_parse.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

#define PCM_IN_BUF   (16 * 1024)
#define PCM_PEND_FR  1152              /* máx amostras/canal por frame MPEG */
#define PCM_RATE_OK(r) ((r) == 44100 || (r) == 48000)

struct pcm_src {
    FILE *f;
    bool is_mp3;
    uint32_t rate;
    /* wav */
    wav_info_t wav;
    uint32_t wav_left;
    uint16_t wav_ch;
    /* mp3 */
    mp3dec_t mp3;
    uint8_t *in;            /* janela de entrada */
    size_t in_len;
    bool in_eof;
    int16_t *pend;          /* PCM estéreo decodido não consumido */
    int pend_n;             /* frames estéreo pendentes */
    int pend_off;
};

bool audio_path_is_playable(const char *name)
{
    size_t n = strlen(name);
    if (n > 4 && !strcasecmp(name + n - 4, ".wav")) return true;
    if (n > 4 && !strcasecmp(name + n - 4, ".mp3")) return true;
    return false;
}

static bool looks_like_mp3(const uint8_t *h, size_t n, const char *path)
{
    if (n >= 3 && !memcmp(h, "ID3", 3)) return true;
    if (n >= 2 && h[0] == 0xFF && (h[1] & 0xE0) == 0xE0) return true;  /* sync */
    size_t pl = strlen(path);
    return pl > 4 && !strcasecmp(path + pl - 4, ".mp3");
}

/* decodifica frames até sair som (pulando ID3/info) ou acabar a janela */
static int mp3_fill(struct pcm_src *s)
{
    for (;;) {
        if (s->in_len < PCM_IN_BUF && !s->in_eof) {
            size_t rd = fread(s->in + s->in_len, 1, PCM_IN_BUF - s->in_len, s->f);
            s->in_len += rd;
            if (rd == 0) s->in_eof = true;
        }
        if (s->in_len == 0) return 0;                 /* EOF limpo */
        mp3dec_frame_info_t fi;
        memset(&fi, 0, sizeof(fi));
        int samples = mp3dec_decode_frame(&s->mp3, s->in, (int)s->in_len,
                                          s->pend, &fi);
        if (fi.frame_bytes <= 0) {
            /* sem frame achado na janela: se não eof, espera refill (o
             * refill acima não aconteceu pq a janela já estava cheia =
             * lixo: resync de 1 byte) */
            if (s->in_eof || s->in_len >= PCM_IN_BUF) {
                s->in_len--;
                memmove(s->in, s->in + 1, s->in_len);
                continue;
            }
            return 0;
        }
        s->in_len -= (size_t)fi.frame_bytes;
        memmove(s->in, s->in + fi.frame_bytes, s->in_len);
        if (samples <= 0) continue;                   /* ID3/info: pula */
        /* mono -> estéreo no próprio pend (de trás p/ frente) */
        if (fi.channels == 1) {
            for (int i = samples - 1; i >= 0; i--) {
                s->pend[2 * i] = s->pend[i];
                s->pend[2 * i + 1] = s->pend[i];
            }
        } else if (fi.channels != 2) {
            continue;                                 /* 5.1 etc: não */
        }
        s->pend_n = samples;
        s->pend_off = 0;
        return (int)fi.channels ? fi.hz : 0;          /* taxa do frame */
    }
}

esp_err_t pcm_src_open(const char *path, pcm_src_t **out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_ERR_NOT_FOUND;
    uint8_t h[12] = { 0 };
    size_t hn = fread(h, 1, sizeof(h), f);
    fseek(f, 0, SEEK_SET);

    pcm_src_t *s = calloc(1, sizeof(*s));
    if (!s) { fclose(f); return ESP_ERR_NO_MEM; }
    s->f = f;

    if (hn >= 12 && !memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4)) {
        if (!wav_parse(f, &s->wav) || s->wav.bits != 16 ||
            (s->wav.channels != 1 && s->wav.channels != 2) ||
            !PCM_RATE_OK(s->wav.sample_rate)) {
            fclose(f); free(s);
            return ESP_ERR_NOT_SUPPORTED;
        }
        s->is_mp3 = false;
        s->rate = s->wav.sample_rate;
        s->wav_ch = s->wav.channels;
        s->wav_left = s->wav.data_len;
        fseek(f, s->wav.data_off, SEEK_SET);
        *out = s;
        return ESP_OK;
    }
    if (looks_like_mp3(h, hn, path)) {
        s->in = malloc(PCM_IN_BUF);
        s->pend = malloc((size_t)PCM_PEND_FR * 2 * sizeof(int16_t));
        if (!s->in || !s->pend) {
            pcm_src_close(s);
            return ESP_ERR_NO_MEM;
        }
        mp3dec_init(&s->mp3);
        s->is_mp3 = true;
        int hz = mp3_fill(s);                 /* 1º frame com som decide */
        if (!hz || !PCM_RATE_OK((uint32_t)hz)) {
            pcm_src_close(s);
            return ESP_ERR_NOT_SUPPORTED;
        }
        s->rate = (uint32_t)hz;
        *out = s;
        return ESP_OK;
    }
    fclose(f); free(s);
    return ESP_ERR_NOT_SUPPORTED;
}

uint32_t pcm_src_rate(const pcm_src_t *s) { return s->rate; }
const char *pcm_src_kind(const pcm_src_t *s) { return s->is_mp3 ? "mp3" : "wav"; }

bool pcm_src_read(pcm_src_t *s, int16_t *out, int frames, int *got)
{
    *got = 0;
    if (!s->is_mp3) {
        if (s->wav_ch == 2) {
            size_t want = (size_t)frames * 4;
            if (want > s->wav_left) want = s->wav_left;
            size_t rd = fread(out, 1, want, s->f);
            s->wav_left -= (uint32_t)rd;
            *got = (int)(rd / 4);
            return true;
        }
        static int16_t mono[1024];
        int n = 0;
        while (n < frames && s->wav_left >= 2) {
            int chunk = frames - n;
            if (chunk > 1024) chunk = 1024;
            size_t want = (size_t)chunk * 2;
            if (want > s->wav_left) want = s->wav_left;
            size_t rd = fread(mono, 1, want, s->f);
            s->wav_left -= (uint32_t)rd;
            int fr = (int)(rd / 2);
            for (int i = 0; i < fr; i++) {
                out[2 * (n + i)] = mono[i];
                out[2 * (n + i) + 1] = mono[i];
            }
            n += fr;
            if (rd < want) break;
        }
        *got = n;
        return true;
    }
    int n = 0;
    while (n < frames) {
        if (s->pend_off < s->pend_n) {
            int c = s->pend_n - s->pend_off;
            if (c > frames - n) c = frames - n;
            memcpy(out + 2 * n, s->pend + 2 * s->pend_off, (size_t)c * 4);
            s->pend_off += c;
            n += c;
            continue;
        }
        int hz = mp3_fill(s);
        if (!hz) break;                       /* EOF (ou resync exausto) */
    }
    *got = n;
    return true;
}

bool pcm_src_seek_bytes(pcm_src_t *s, uint32_t off)
{
    if (s->is_mp3) return false;   /* resume de MP3 = recomeço (log honesto) */
    if (off > s->wav.data_len) return false;
    if (fseek(s->f, s->wav.data_off + (long)off, SEEK_SET) != 0) return false;
    s->wav_left = s->wav.data_len - off;
    return true;
}

void pcm_src_close(pcm_src_t *s)
{
    if (!s) return;
    if (s->f) fclose(s->f);
    free(s->in);
    free(s->pend);
    free(s);
}
