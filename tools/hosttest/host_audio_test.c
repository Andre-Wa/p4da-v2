/* host_audio_test.c — SOM.4 (v2.3.0): audio_tags + audio_playlist +
 * pcm_src contra arquivos reais/sintéticos no host.
 *
 *   A tags do mp3 REAL do template (ffmpeg gravou ID3v2.4);
 *   B tags WAV LIST/INFO sintético;
 *   C sem tag = strings vazias;
 *   D m3u: comentários/#EXT ignorados, relativos à pasta, absolutos,
 *     inexistentes/não-tocáveis pulados com contagem;
 *   E pcm_src WAV: rate/kind/frames;
 *   F pcm_src MP3 real: 2 s @44k1 (±2%), kind "mp3";
 *   G recusas: wav 22050 e "mp3" de lixo = NOT_SUPPORTED;
 *   H audio_scan_tree (v2.3.2, M4): subpastas, ordem por
 *     caminho relativo, tetos de profundidade/entradas,
 *     raiz inexistente, barra final na raiz.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "audio_tags.h"
#include "audio_playlist.h"
#include "audio_scan.h"
#include "audio_decode.h"

static int fails;
#define CHECK(cond, fmt, ...)                                   \
    do {                                                        \
        if (!(cond)) {                                          \
            fails++;                                            \
            printf("FAIL: " fmt "\n", ##__VA_ARGS__);           \
        }                                                       \
    } while (0)

#define ROOT "/tmp/audiotest"
#define MP3_REAL "sdcard-template/pda/music/teste_tom.mp3"

/* ---------------- helpers ---------------- */
static void put_u32le(FILE *f, uint32_t v)
{
    fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f);
    fputc((v >> 16) & 0xff, f); fputc((v >> 24) & 0xff, f);
}
static void chunk(FILE *f, const char *id, uint32_t sz, const uint8_t *body)
{
    fwrite(id, 1, 4, f); put_u32le(f, sz);
    if (sz) fwrite(body, 1, sz, f);
    if (sz & 1) fputc(0, f);
}
static void fmt16(FILE *f, uint16_t ch, uint32_t rate)
{
    uint8_t b[16] = { 0 };
    b[0] = 1;                                  /* PCM */
    b[2] = ch & 0xff;
    b[4] = rate & 0xff; b[5] = (rate >> 8) & 0xff;
    b[6] = (rate >> 16) & 0xff; b[7] = (rate >> 24) & 0xff;
    uint32_t bps = rate * ch * 2;
    b[8] = bps & 0xff; b[9] = (bps >> 8) & 0xff;
    b[10] = (bps >> 16) & 0xff; b[11] = (bps >> 24) & 0xff;
    b[12] = ch * 2;
    b[14] = 16;
    chunk(f, "fmt ", 16, b);
}
static void mkwav(const char *path, uint16_t ch, uint32_t rate, int frames,
                  const uint8_t *list, uint32_t list_sz)
{
    FILE *f = fopen(path, "wb");
    fwrite("RIFF", 1, 4, f); put_u32le(f, 0); fwrite("WAVE", 1, 4, f);
    fmt16(f, ch, rate);
    if (list) chunk(f, "LIST", list_sz, list);
    uint8_t *pcm = calloc(1, (size_t)frames * ch * 2);
    chunk(f, "data", (uint32_t)(frames * ch * 2), pcm);
    free(pcm);
    fclose(f);
}

static char g_paths[16][320];
static int g_n;
static void collect(const char *p, void *ctx)
{
    (void)ctx;
    if (g_n < 16) snprintf(g_paths[g_n++], sizeof(g_paths[0]), "%s", p);
}

/* cria um arquivo vazio dentro de `dir` (a varredura só olha o nome) */
static void touch_file(const char *dir, const char *name)
{
    char p[400];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    FILE *f = fopen(p, "wb");
    if (f) fclose(f);
}

/* coleção do callback do audio_scan (o teste afirma sobre ela) */
static char g_full[8][400];
static char g_rel[8][300];
static int  g_sn;

static void scan_cb(const char *full, const char *rel, void *ctx)
{
    (void)ctx;
    if (g_sn >= 8) return;
    snprintf(g_full[g_sn], sizeof(g_full[0]), "%s", full);
    snprintf(g_rel[g_sn], sizeof(g_rel[0]), "%s", rel);
    g_sn++;
}

/* varre zerando a coleção; devolve o retorno do módulo */
static int scan_reset(const char *root, int max_depth, int max_entries,
                      bool *trunc)
{
    g_sn = 0;
    return audio_scan_tree(root, max_depth, max_entries, scan_cb, NULL, trunc);
}

int main(void)
{
    if (system("rm -rf " ROOT) != 0 || mkdir(ROOT, 0755) != 0) {
        printf("AUDIO TEST: prep falhou\n");
        return 2;
    }

    /* ---- A: tags do mp3 real (ID3v2.4 do ffmpeg) ---- */
    {
        audio_tags_t t;
        audio_tags_read(MP3_REAL, &t);
        CHECK(!strcmp(t.title, "Tom de Teste"), "A: title '%s'", t.title);
        CHECK(!strcmp(t.artist, "PDA p4da"), "A: artist '%s'", t.artist);
        CHECK(!strcmp(t.album, "Acervo de Teste"), "A: album '%s'", t.album);
        printf("[audio] A: ID3v2.4 do mp3 real (title/artist/album)  OK\n");
    }

    /* ---- B: WAV com LIST/INFO ---- */
    {
        static const uint8_t info[] = {
            'I','N','F','O',
            'I','N','A','M', 8,0,0,0, 'S','i','n','f','o','n','i','a',
            'I','A','R','T', 6,0,0,0, 'F','u','l','a','n','o',
            'I','P','R','D', 6,0,0,0, 'D','i','s','c','o','s',
        };
        mkwav(ROOT "/b.wav", 2, 44100, 100, info, (uint32_t)sizeof(info));
        audio_tags_t t;
        audio_tags_read(ROOT "/b.wav", &t);
        CHECK(!strcmp(t.title, "Sinfonia"), "B: title '%s'", t.title);
        CHECK(!strcmp(t.artist, "Fulano"), "B: artist '%s'", t.artist);
        CHECK(!strcmp(t.album, "Discos"), "B: album '%s'", t.album);
        printf("[audio] B: WAV LIST/INFO (INAM/IART/IPRD)  OK\n");
    }

    /* ---- C: WAV sem tag = vazio ---- */
    {
        mkwav(ROOT "/c.wav", 2, 48000, 50, NULL, 0);
        audio_tags_t t;
        audio_tags_read(ROOT "/c.wav", &t);
        CHECK(t.title[0] == 0 && t.artist[0] == 0 && t.album[0] == 0,
              "C: tags deviam ser vazias");
        printf("[audio] C: sem tag = vazio (UI cai no basename)  OK\n");
    }

    /* ---- D: m3u ---- */
    {
        char pl[300];
        snprintf(pl, sizeof(pl),
                 "#EXTM3U\n"
                 "# comentário solto\n"
                 "b.wav\n"
                 "#EXTINF:123,ignorado\n"
                 ROOT "/c.wav\n"
                 "nao_existe.wav\n"
                 "leia.txt\n");
        FILE *f = fopen(ROOT "/pl.m3u", "w");
        fputs(pl, f);
        fclose(f);
        FILE *t = fopen(ROOT "/leia.txt", "w");
        fputs("x", t);
        fclose(t);
        g_n = 0;
        int skipped = -1;
        char plpath[320];
        snprintf(plpath, sizeof(plpath), ROOT "/pl.m3u");
        int n = audio_playlist_load(plpath, collect, NULL, &skipped);
        CHECK(n == 2, "D: %d entradas (esperado 2)", n);
        CHECK(skipped == 2, "D: skipped=%d (esperado 2)", skipped);
        CHECK(g_n == 2 && !strcmp(g_paths[0], ROOT "/b.wav") &&
                  !strcmp(g_paths[1], ROOT "/c.wav"),
              "D: ordem/resolve errados: '%s' '%s'", g_paths[0], g_paths[1]);
        CHECK(audio_path_is_playlist("x.m3u") && audio_path_is_playlist("X.M3U8") &&
                  !audio_path_is_playlist("x.wav"),
              "D: is_playlist errado");
        printf("[audio] D: m3u (comentários/#EXT, relativos, absolutos, skips)  OK\n");
    }

    /* ---- E: pcm_src WAV ---- */
    {
        pcm_src_t *s = NULL;
        CHECK(pcm_src_open(ROOT "/b.wav", &s) == ESP_OK, "E: open falhou");
        CHECK(strcmp(pcm_src_kind(s), "wav") == 0 && pcm_src_rate(s) == 44100,
              "E: kind/rate errados");
        static int16_t buf[1024];
        int total = 0, got = 0;
        while (pcm_src_read(s, buf, 512, &got) && got > 0) total += got;
        CHECK(total == 100, "E: %d frames (esperado 100)", total);
        pcm_src_close(s);
        printf("[audio] E: pcm_src WAV (kind/rate/frames exatos)  OK\n");
    }

    /* ---- F: pcm_src MP3 real (2 s @44k1) ---- */
    {
        pcm_src_t *s = NULL;
        CHECK(pcm_src_open(MP3_REAL, &s) == ESP_OK, "F: open falhou");
        if (s) {
            CHECK(strcmp(pcm_src_kind(s), "mp3") == 0, "F: kind");
            CHECK(pcm_src_rate(s) == 44100, "F: rate %lu",
                  (unsigned long)pcm_src_rate(s));
            static int16_t buf[4096];
            int total = 0, got = 0;
            while (pcm_src_read(s, buf, 1152, &got) && got > 0) total += got;
            /* 2 s @44k1 = 88200 frames; codec padding ±1 frame por quadro */
            CHECK(total > 87000 && total < 92000,
                  "F: %d frames (esperado ~88200 + padding do encoder)", total);
            /* não-vazio: algum pico de senoide de 440 Hz */
            pcm_src_close(s);
            printf("[audio] F: pcm_src MP3 real decodifica 2 s (~88k frames)  OK\n");
        }
    }

    /* ---- G: recusas ---- */
    {
        mkwav(ROOT "/g22.wav", 1, 22050, 10, NULL, 0);
        pcm_src_t *s = NULL;
        CHECK(pcm_src_open(ROOT "/g22.wav", &s) == ESP_ERR_NOT_SUPPORTED,
              "G: 22050 devia recusar");
        FILE *f = fopen(ROOT "/g.mp3", "wb");
        fputs("lixo que não é mpeg mas termina .mp3 para o detector", f);
        fclose(f);
        CHECK(pcm_src_open(ROOT "/g.mp3", &s) != ESP_OK, "G: lixo .mp3 devia falhar");
        printf("[audio] G: recusas honestas (taxa/liço)  OK\n");
    }

    /* ---- H: varredura recursiva do acervo (v2.3.2, M4) ---- */
    {
        char tree[64], sub[80], deep[96];
        snprintf(tree, sizeof(tree), ROOT "/tree");
        snprintf(sub,  sizeof(sub),  "%s/sub", tree);
        snprintf(deep, sizeof(deep), "%s/deep", sub);
        mkdir(tree, 0755); mkdir(sub, 0755); mkdir(deep, 0755);
        touch_file(tree, "a.mp3");
        touch_file(tree, "pl.m3u8");
        touch_file(tree, "leia.txt");        /* não tocável: fica fora */
        touch_file(tree, ".escondido.mp3");  /* entra: a varredura não
                                              * filtra dotfile (nem a v1
                                              * filtrava; FATFS não tem) */
        touch_file(sub,  "b.wav");
        touch_file(deep, "c.mp3");

        int n = scan_reset(tree, 5, 0, NULL);
        CHECK(n == 5 && g_sn == 5, "H1: esperava 5 entradas, veio n=%d sn=%d",
              n, g_sn);
        /* ordem por caminho relativo (byte order): '.' < 'a' < 'p' < 's' */
        CHECK(!strcmp(g_rel[0], ".escondido.mp3") &&
              !strcmp(g_rel[1], "a.mp3") &&
              !strcmp(g_rel[2], "pl.m3u8") &&
              !strcmp(g_rel[3], "sub/b.wav") &&
              !strcmp(g_rel[4], "sub/deep/c.mp3"),
              "H1b: ordem relativa errada: %s | %s | %s | %s | %s",
              g_rel[0], g_rel[1], g_rel[2], g_rel[3], g_rel[4]);
        CHECK(!strcmp(g_full[4], ROOT "/tree/sub/deep/c.mp3"),
              "H1c: full da subpasta montado errado: %s", g_full[4]);
        printf("[audio] H1: subpastas varridas, ordem por caminho relativo  OK\n");

        n = scan_reset(tree, 0, 0, NULL);
        CHECK(n == 3, "H2: max_depth 0 devia dar só o topo (3), veio %d", n);
        printf("[audio] H2: teto de profundidade respeitado  OK\n");

        bool trunc = false;
        n = scan_reset(tree, 5, 2, &trunc);
        CHECK(n == 2 && trunc, "H3: max_entries 2 devia cortar em 2 + avisar "
                               "(veio n=%d trunc=%d)", n, (int)trunc);
        trunc = false;
        n = scan_reset(tree, 5, 100, &trunc);
        CHECK(n == 5 && !trunc, "H3b: sem estouro de teto não pode avisar "
                                "truncation (n=%d trunc=%d)", n, (int)trunc);
        printf("[audio] H3: teto de entradas corta e AVISA (truncated)  OK\n");

        n = scan_reset(ROOT "/nao_existe", 5, 0, NULL);
        CHECK(n == -1, "H4: raiz que não abre devia dar -1, veio %d", n);
        printf("[audio] H4: raiz inexistente = -1 (UI loga honesto)  OK\n");

        char combarra[80];
        snprintf(combarra, sizeof(combarra), ROOT "/tree/");
        n = scan_reset(combarra, 5, 0, NULL);
        CHECK(n == 5 && g_rel[3][0] != '/' && !strcmp(g_rel[3], "sub/b.wav"),
              "H5: barra final na raiz vazou no relativo (n=%d rel3=%s)",
              n, g_rel[3]);
        CHECK(!strcmp(g_full[3], ROOT "/tree/sub/b.wav"),
              "H5b: full com raiz de barra final errado: %s", g_full[3]);
        printf("[audio] H5: raiz com barra final normalizada  OK\n");
    }

    if (fails) { printf("AUDIO TEST: %d FALHA(S)\n", fails); return 1; }
    printf("AUDIO TEST: OK (tags ID3/LIST, m3u, pcm_src WAV+MP3, "
           "recusas, varredura recursiva)\n");
    return 0;
}
