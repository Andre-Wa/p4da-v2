/**
 * audio_scan.c — ver audio_scan.h.
 *
 * Iterativo (pilha explícita de pastas) de propósito: a task de I/O que
 * chama isto tem 8 KB de pilha, e recursão com DIR + std::string por
 * nível era um estouro esperando acontecer (a v2.3.1 acabou de morrer
 * de estouro de pilha no decoder — não vamos repetir a dose).
 */
#include "audio_scan.h"
#include "audio_decode.h"      /* audio_path_is_playable */
#include "audio_playlist.h"    /* audio_path_is_playlist */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct { char *path; int depth; } scan_dir_t;

static bool push_dir(scan_dir_t **p, size_t *n, size_t *cap,
                     const char *path, int depth)
{
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 8;
        scan_dir_t *np = realloc(*p, nc * sizeof(**p));
        if (!np) return false;
        *p = np; *cap = nc;
    }
    char *dup = strdup(path);
    if (!dup) return false;
    (*p)[*n].path = dup;
    (*p)[*n].depth = depth;
    (*n)++;
    return true;
}

static bool push_rel(char ***p, size_t *n, size_t *cap, const char *rel)
{
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 32;
        char **np = realloc(*p, nc * sizeof(**p));
        if (!np) return false;
        *p = np; *cap = nc;
    }
    char *dup = strdup(rel);
    if (!dup) return false;
    (*p)[(*n)++] = dup;
    return true;
}

static int cmp_rel(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int audio_scan_tree(const char *root, int max_depth, int max_entries,
                    audio_scan_fn cb, void *ctx, bool *truncated)
{
    if (truncated) *truncated = false;
    if (!root || !root[0]) return -1;

    /* normaliza: sem barra(s) no fim, para o rel não sair com "//" */
    size_t rl = strlen(root);
    while (rl > 1 && root[rl - 1] == '/') rl--;
    char *base = malloc(rl + 1);
    if (!base) return -1;
    memcpy(base, root, rl);
    base[rl] = 0;

    scan_dir_t *pend = NULL; size_t pend_n = 0, pend_cap = 0;
    char **rels = NULL;      size_t rel_n = 0, rel_cap = 0;
    bool trunc = false;
    int ret = -1;

    DIR *probe = opendir(base);
    if (!probe) goto done;          /* root não abre = -1 (lista avisa) */
    closedir(probe);

    if (!push_dir(&pend, &pend_n, &pend_cap, base, 0)) goto done;

    while (pend_n > 0 && !trunc) {
        scan_dir_t cur = pend[--pend_n];
        DIR *d = opendir(cur.path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                    continue;
                size_t fl = strlen(cur.path) + 1 + strlen(e->d_name) + 1;
                char *full = malloc(fl);
                if (!full) { trunc = true; break; }
                snprintf(full, fl, "%s/%s", cur.path, e->d_name);

                bool is_dir = (e->d_type == DT_DIR);
                if (e->d_type == DT_UNKNOWN) {   /* VFS que não informa */
                    struct stat st;
                    is_dir = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
                }
                if (is_dir) {
                    if (cur.depth < max_depth)
                        push_dir(&pend, &pend_n, &pend_cap, full, cur.depth + 1);
                    free(full);
                    continue;
                }
                if (!audio_path_is_playable(e->d_name) &&
                    !audio_path_is_playlist(e->d_name)) {
                    free(full);
                    continue;
                }
                if (max_entries > 0 && rel_n >= (size_t)max_entries) {
                    trunc = true;
                    free(full);
                    break;
                }
                /* rel = o que vem depois de base + '/' */
                if (!push_rel(&rels, &rel_n, &rel_cap, full + rl + 1))
                    trunc = true;
                free(full);
                if (trunc) break;
            }
            closedir(d);
        }
        free(cur.path);
    }

    /* ordem estável por caminho relativo: agrupa por pasta/álbum */
    if (rel_n > 1) qsort(rels, rel_n, sizeof(*rels), cmp_rel);

    for (size_t i = 0; i < rel_n; i++) {
        size_t fl = rl + 1 + strlen(rels[i]) + 1;
        char *full = malloc(fl);
        if (!full) { trunc = true; break; }
        snprintf(full, fl, "%s/%s", base, rels[i]);
        if (cb) cb(full, rels[i], ctx);
        free(full);
    }
    ret = (int)rel_n;

done:
    for (size_t i = 0; i < pend_n; i++) free(pend[i].path);
    free(pend);
    for (size_t i = 0; i < rel_n; i++) free(rels[i]);
    free(rels);
    free(base);
    /* cuidado: o teste é da FLAG local, não do ponteiro de saída
     * (v2.3.2: na primeira versão isto era `if (truncated)`, que marcava
     * truncation para todo caller que passasse o argumento — pego pelo
     * host test H3b, antes de qualquer flash) */
    if (trunc && truncated) *truncated = true;
    return ret;
}
