/* host_stack_test.c — v2.3.2: orçamento de PILHA das tasks de player.
 *
 * POR QUE EXISTE
 * --------------
 * A v2.3.1 morreu no alvo com "Stack protection fault" na task au_play:
 * mp3dec_decode_frame() do minimp3 declara `mp3dec_scratch_t scratch` NA
 * PILHA (16.256 B) e os players tinham 8 KB (spk_play) / 10 KB (au_play),
 * tamanhos herdados da era WAV-only da v1. O log do usuário deu a conta
 * exata: SP 8.140 B abaixo do limite inferior de uma pilha de 10.228 B.
 *
 * Nenhum teste de host olhava pilha — a suíte media lógica, não recurso.
 * Este teste fecha isso de dois jeitos:
 *
 *   A) tamanho da struct que o decoder põe na pilha (dado do vendor);
 *   B) HIGH-WATER MARK real da cadeia pcm_src_open + pcm_src_read,
 *      medido rodando o decode numa pilha alternativa preenchida com
 *      sentinela (ucontext) e varrendo de baixo para cima;
 *   C) a constante AUDIO_PCM_TASK_STACK_BYTES (dona do número, usada
 *      pelos dois players) tem de cobrir a medição + margem.
 *
 * LIMITAÇÃO HONESTA: mede no host x86_64/glibc. O alvo é riscv32/newlib
 * (ponteiros de 4 B, vprintf diferente) — por isso a margem de 4 KB e por
 * isso o teste é um PISO, não uma garantia exata. O que ele garante é que
 * a ordem de grandeza está certa e que ninguém baixa a constante às cegas.
 *
 * Uso: compilado por tools/hosttest/run.sh (seção stack).
 */
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

/* O minimp3.h só expõe os tipos INTERNOS (mp3dec_scratch_t) dentro de
 * MINIMP3_IMPLEMENTATION. Para medir sizeof sem duplicar os símbolos
 * públicos — audio_decode.c já tem a implementação de verdade — esta TU
 * inclui uma cópia com as duas funções exportadas renomeadas. A cópia
 * daqui serve SÓ para o sizeof; quem decodifica é o audio_decode.c. */
#define MINIMP3_IMPLEMENTATION
#define mp3dec_decode_frame mp3dec_decode_frame__sizeof_probe
#define mp3dec_init         mp3dec_init__sizeof_probe
#include "minimp3.h"
#undef mp3dec_decode_frame
#undef mp3dec_init

#include "audio_decode.h"

static int fails;
#define CHECK(cond, fmt, ...)                                   \
    do {                                                        \
        if (!(cond)) {                                          \
            fails++;                                            \
            printf("FAIL: " fmt "\n", ##__VA_ARGS__);           \
        }                                                       \
    } while (0)

#define MP3_REAL "sdcard-template/pda/music/teste_tom.mp3"
#define WAV_REAL "sdcard-template/pda/music/teste_44k1_stereo.wav"

/* ---------------- medição por sentinela ---------------- */

#define PROBE_BYTES (192 * 1024)
#define SENTINEL    0xA5

static unsigned char *g_probe;
static ucontext_t g_back;
static const char *g_path;
static long g_frames;
static int g_ok;

/* Roda na pilha-sonda. O buffer de PCM é STATIC de propósito: no firmware
 * ele também é static (audio_i2s.c/audio_uac.c), então não deve contar. */
static void probe_run(void)
{
    static int16_t buf[2048];
    pcm_src_t *s = NULL;
    g_frames = 0;
    g_ok = 0;
    if (pcm_src_open(g_path, &s) != ESP_OK) return;
    int got = 0;
    while (pcm_src_read(s, buf, 1024, &got) && got > 0) g_frames += got;
    pcm_src_close(s);
    g_ok = 1;
}

/* devolve o high-water mark em bytes, ou -1 se a cadeia falhou */
static long measure(const char *path)
{
    g_path = path;
    memset(g_probe, SENTINEL, PROBE_BYTES);

    ucontext_t ctx;
    if (getcontext(&ctx) != 0) return -1;
    ctx.uc_stack.ss_sp = g_probe;
    ctx.uc_stack.ss_size = PROBE_BYTES;
    ctx.uc_link = &g_back;
    makecontext(&ctx, probe_run, 0);
    if (swapcontext(&g_back, &ctx) != 0) return -1;
    if (!g_ok) return -1;

    /* varre do fundo (endereço mais baixo) para cima: o primeiro byte que
     * não é mais sentinela marca o ponto mais fundo que a cadeia tocou */
    long i = 0;
    while (i < PROBE_BYTES && g_probe[i] == SENTINEL) i++;
    return PROBE_BYTES - i;
}

int main(void)
{
    /* ---- A: o que o vendor põe na pilha ---- */
    size_t scratch = sizeof(mp3dec_scratch_t);
    printf("[stack] A: sizeof(mp3dec_scratch_t) = %zu B (%.1f KB) "
           "[syn=%zu grbuf=%zu maindata=%zu]\n",
           scratch, scratch / 1024.0,
           sizeof(((mp3dec_scratch_t *)0)->syn),
           sizeof(((mp3dec_scratch_t *)0)->grbuf),
           sizeof(((mp3dec_scratch_t *)0)->maindata));
    CHECK(AUDIO_PCM_TASK_STACK_BYTES >= scratch + AUDIO_PCM_STACK_MARGIN_BYTES,
          "A: constante %d B não cobre scratch %zu B + margem %d B",
          AUDIO_PCM_TASK_STACK_BYTES, scratch, AUDIO_PCM_STACK_MARGIN_BYTES);

    /* ---- B/C: high-water real da cadeia, MP3 e WAV ---- */
    g_probe = malloc(PROBE_BYTES);
    if (!g_probe) { printf("FAIL: sem RAM p/ sonda\n"); return 1; }

    long mp3 = measure(MP3_REAL);
    CHECK(mp3 > 0, "B: decode do mp3 real falhou na sonda");
    if (mp3 > 0)
        printf("[stack] B: high-water MP3 (open+read do mp3 real, %ld frames) "
               "= %ld B (%.1f KB)\n", g_frames, mp3, mp3 / 1024.0);

    long wav = measure(WAV_REAL);
    CHECK(wav > 0, "C: leitura do wav real falhou na sonda");
    if (wav > 0)
        printf("[stack] C: high-water WAV = %ld B (%.1f KB) — era isto que "
               "cabia nos 8/10 KB da v1\n", wav, wav / 1024.0);

    /* ---- D: a constante cobre o pior caso + margem ---- */
    long worst = mp3 > wav ? mp3 : wav;
    if (worst > 0) {
        long need = worst + AUDIO_PCM_STACK_MARGIN_BYTES;
        printf("[stack] D: AUDIO_PCM_TASK_STACK_BYTES = %d B (%.1f KB) vs "
               "pior caso %ld B + margem %d B = %ld B -> folga %ld B\n",
               AUDIO_PCM_TASK_STACK_BYTES, AUDIO_PCM_TASK_STACK_BYTES / 1024.0,
               worst, AUDIO_PCM_STACK_MARGIN_BYTES, need,
               (long)AUDIO_PCM_TASK_STACK_BYTES - need);
        CHECK((long)AUDIO_PCM_TASK_STACK_BYTES >= need,
              "D: constante %d B < high-water %ld B + margem %d B — suba "
              "AUDIO_PCM_TASK_STACK_BYTES em audio_decode.h",
              AUDIO_PCM_TASK_STACK_BYTES, worst, AUDIO_PCM_STACK_MARGIN_BYTES);
        /* sanidade: o MP3 tem de custar MUITO mais que o WAV (é a assinatura
         * do bug da v2.3.1; se um dia igualar, o vendor mudou) */
        CHECK(mp3 > wav * 2, "D: MP3 (%ld B) não custa >> WAV (%ld B)?", mp3, wav);
    }

    free(g_probe);
    if (fails) { printf("STACK TEST: FALHOU (%d)\n", fails); return 1; }
    printf("STACK TEST: OK (pilha das tasks de player cobre o decode MP3 real)\n");
    return 0;
}
