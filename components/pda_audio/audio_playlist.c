/**
 * audio_playlist.c — ver audio_playlist.h.
 */
#include "audio_playlist.h"
#include "audio_decode.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

bool audio_path_is_playlist(const char *name)
{
    size_t n = strlen(name);
    if (n > 4 && !strcasecmp(name + n - 4, ".m3u")) return true;
    if (n > 5 && !strcasecmp(name + n - 5, ".m3u8")) return true;
    return false;
}

static bool file_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

int audio_playlist_load(const char *m3u_path, audio_playlist_fn cb, void *ctx,
                        int *skipped)
{
    if (skipped) *skipped = 0;
    FILE *f = fopen(m3u_path, "r");
    if (!f) return -1;

    /* pasta da playlist p/ relativos (v2.3.1: 'pl' era sobra do rascunho
     * do limite — o guard já usa a distância do último '/') */
    char base[160] = "";
    const char *sl = strrchr(m3u_path, '/');
    if (sl && (size_t)(sl - m3u_path) < sizeof(base)) {
        memcpy(base, m3u_path, (size_t)(sl - m3u_path));
        base[(size_t)(sl - m3u_path)] = 0;
    }

    char line[256];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r' ||
                     line[l - 1] == ' '))
            line[--l] = 0;
        if (l == 0 || line[0] == '#') continue;      /* comentários e #EXT* */
        /* 416 = base (≤159) + '/' + line (≤255) + NUL → não trunca por
         * construção. v2.3.1: antes era 320 e o GCC apontava
         * -Wformat-truncation — linha longa de .m3u virava caminho
         * cortado e a faixa era pulada em silêncio. */
        char full[416];
        int w = (line[0] == '/')
              ? snprintf(full, sizeof(full), "%s", line)
              : snprintf(full, sizeof(full), "%s/%s", base, line);
        if (w < 0 || (size_t)w >= sizeof(full)) {    /* impossível hoje */
            if (skipped) (*skipped)++;
            continue;
        }
        if (!audio_path_is_playable(full) || !file_exists(full)) {
            if (skipped) (*skipped)++;
            continue;
        }
        if (cb) cb(full, ctx);
        n++;
    }
    fclose(f);
    return n;
}
