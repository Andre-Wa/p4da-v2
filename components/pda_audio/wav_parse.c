/**
 * wav_parse.c — parser de WAV PCM16 (lógica pura, ver wav_parse.h).
 *
 * Semântica herdada do parse_wav da v1 (audio_uac.c, M5a): percorre
 * chunks após o RIFF/WAVE; "fmt " valida PCM (fmt==1) e extrai
 * ch/rate/bits (avança o excedente de sz>16 p/ fmt estendido); "data"
 * devolve offset+len e termina; qualquer outro chunk (LIST, FACT, ...)
 * é pulado com pad de word alignment (sz+1 & ~1).
 */
#include "wav_parse.h"

#include <string.h>

bool wav_parse(FILE *f, wav_info_t *out)
{
    if (!f || !out) return false;
    uint8_t h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
        return false;
    bool fmt_ok = false;
    for (;;) {
        uint8_t c[8];
        if (fread(c, 1, 8, f) != 8) return false;
        uint32_t sz = (uint32_t)c[4] | ((uint32_t)c[5] << 8) |
                      ((uint32_t)c[6] << 16) | ((uint32_t)c[7] << 24);
        if (!memcmp(c, "fmt ", 4)) {
            uint8_t fm[16];
            if (sz < 16 || fread(fm, 1, 16, f) != 16) return false;
            uint16_t fmt = (uint16_t)(fm[0] | (fm[1] << 8));
            out->channels   = (uint16_t)(fm[2] | (fm[3] << 8));
            out->sample_rate = (uint32_t)fm[4] | ((uint32_t)fm[5] << 8) |
                               ((uint32_t)fm[6] << 16) | ((uint32_t)fm[7] << 24);
            out->bits = (uint16_t)(fm[14] | (fm[15] << 8));
            if (fmt != 1) return false;              /* só PCM */
            fmt_ok = true;
            long adv = (long)sz - 16;
            if (adv > 0 && fseek(f, adv, SEEK_CUR)) return false;
        } else if (!memcmp(c, "data", 4)) {
            out->data_off = ftell(f);
            out->data_len = sz;
            return fmt_ok;
        } else {
            if (fseek(f, (long)((sz + 1u) & ~1u), SEEK_CUR)) return false;
        }
    }
}

bool wav_parse_path(const char *path, wav_info_t *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = wav_parse(f, out);
    fclose(f);
    return ok;
}
