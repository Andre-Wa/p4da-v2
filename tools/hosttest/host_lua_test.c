/* host_lua_test.c — LUA-BIND (v2/L1, herdado da v1 v5.16.2 sem bateria).
 *
 * Linka o lua_runtime.c REAL ao Lua 5.4 do host (lua55_shim.h casa o
 * lua_newstate de 3 args do Lua 5.5 do alvo) com stubs: storage = stdio
 * real em /tmp/luabind, relógio que avança +2 s por leitura (o sleep é
 * no-op), pda_config dummy.
 *
 * Seções:
 *   A — smoke de bindings (version/root/sd_mounted/uptime);
 *   B — pda.fs.mkdir (resolve, criação real, rejeição de "..");
 *   C — pda.sleep_ms (clamp silencioso, erros de tipo);
 *   D — script REAL sdcard-template/pda/scripts/logger_exemplo.lua
 *       end-to-end: header único + 8 linhas crescentes em uptime;
 *   E — reexecução: continua sem duplicar header e sem escrever além do
 *       máximo;
 *   F — sem diretório p/ gravar: falha graciosa, anuncia no console.
 *
 * A bateria por cenário (seções A/D/E/F da v1) volta na camada L4 junto
 * de pda.battery.* — ver docs/MIGRATION.md. Disciplina REGRESSOES_BUILD:
 * lógica pura nasce com teste de host — aqui nasce testada até a
 * integração C↔Lua↔arquivo.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lua_runtime.h"

#define ROOT   "/tmp/luabind"
#define SCRIPT "sdcard-template/pda/scripts/logger_exemplo.lua"
#define CSV    ROOT "/logs/tempo.csv"
#define HEADER "n;uptime_s"

static int fails;
#define CHECK(cond, fmt, ...)                                   \
    do {                                                        \
        if (!(cond)) {                                          \
            fails++;                                            \
            printf("FAIL: " fmt "\n", ##__VA_ARGS__);           \
        }                                                       \
    } while (0)

/* ---------------- strlcpy (glibc < 2.38 não tem; mesmo shim do
 * host_cfg_test.c — binários separados, sem colisão) ---------------- */
size_t strlcpy(char *dst, const char *src, size_t sz)
{
    size_t n = strlen(src);
    if (sz) {
        size_t c = n >= sz ? sz - 1 : n;
        memcpy(dst, src, c);
        dst[c] = '\0';
    }
    return n;
}

/* ---------------- relógio falso: +2 s por leitura ------------------ */
static int64_t s_clk_us = 1000000;
int64_t esp_timer_get_time(void)
{
    int64_t t = s_clk_us;
    s_clk_us += 2000000;
    return t;
}
uint32_t esp_random(void) { return 0x5EED5EEDu; }

/* ---------------- storage: stdio real em /tmp/luabind -------------- */
static char g_mkdir_last[192];
static int g_mkdir_calls, g_mkdir_fail;

const char *pda_root(void) { return ROOT; }
esp_err_t pda_path(char *out, size_t sz, const char *rel)
{
    int n = snprintf(out, sz, "%s/%s", ROOT, rel);
    return (n > 0 && (size_t)n < sz) ? ESP_OK : ESP_FAIL;
}
esp_err_t storage_ensure_dir(const char *p)
{
    g_mkdir_calls++;
    snprintf(g_mkdir_last, sizeof(g_mkdir_last), "%s", p);
    if (g_mkdir_fail) return ESP_FAIL;
    if (mkdir(p, 0755) != 0 && errno != EEXIST) return ESP_FAIL;
    return ESP_OK;
}
bool storage_sd_mounted(void) { return true; }
bool storage_file_exists(const char *p)
{ struct stat st; return stat(p, &st) == 0 && S_ISREG(st.st_mode); }
bool storage_is_dir(const char *p)
{ struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
int64_t storage_file_size(const char *p)
{ struct stat st; return stat(p, &st) == 0 ? (int64_t)st.st_size : -1; }
esp_err_t storage_read_file_alloc(const char *p, char **out, size_t *len)
{
    FILE *f = fopen(p, "rb");
    if (!f) return ESP_FAIL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)sz + 1);
    if (!b) { fclose(f); return ESP_FAIL; }
    size_t rd = fread(b, 1, (size_t)sz, f);
    fclose(f);
    b[rd] = '\0';
    *out = b;
    *len = rd;
    return ESP_OK;
}
esp_err_t storage_delete_file(const char *p)
{ return remove(p) == 0 ? ESP_OK : ESP_FAIL; }

/* ---------------- pda_config: dummies (script não usa) ------------- */
bool pda_settings_get(const char *k, double *n, bool *b, char *s, size_t sz)
{ (void)k; (void)n; (void)b; (void)s; (void)sz; return false; }
bool pda_settings_set(const char *k, double n, bool ib, bool b, const char *s)
{ (void)k; (void)n; (void)ib; (void)b; (void)s; return false; }
esp_err_t pda_config_save(void) { return ESP_OK; }

/* ---------------- captura do pda.log ------------------------------- */
static char g_log[16384];
static size_t g_logpos;
static void cap_log(const char *line, void *ctx)
{
    (void)ctx;
    int w = snprintf(g_log + g_logpos, sizeof(g_log) - g_logpos, "%s\n", line);
    if (w > 0 && g_logpos + (size_t)w < sizeof(g_log)) g_logpos += (size_t)w;
}
static void cap_reset(void) { g_logpos = 0; g_log[0] = '\0'; }
static int cap_has(const char *s) { return strstr(g_log, s) != NULL; }

/* ---------------- helpers de CSV ----------------------------------- */
static int csv_load(const char *path, char lines[][256], int max)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = 0;
    while (n < max && fgets(lines[n], 256, f)) {
        size_t l = strlen(lines[n]);
        while (l && (lines[n][l - 1] == '\n' || lines[n][l - 1] == '\r'))
            lines[n][--l] = '\0';
        if (l) n++;
    }
    fclose(f);
    return n;
}

int main(void)
{
    if (system("rm -rf " ROOT) != 0 || mkdir(ROOT, 0755) != 0) {
        printf("LUA TEST: prep do %s falhou\n", ROOT);
        return 2;
    }
    CHECK(lua_runtime_init() == ESP_OK, "init da VM Lua falhou");
    lua_runtime_set_log_callback(cap_log, NULL);
    char err[256];
    esp_err_t e;

    /* ---- A: smoke de bindings básicas ---- */
    {
        cap_reset();
        e = lua_runtime_run_string(
            "assert(pda.version() == 'p4da 2.0.0', 'version: '..pda.version())\n"
            "assert(pda.root() == '" ROOT "', 'root errado')\n"
            "assert(pda.sd_mounted() == true, 'sd_mounted errado')\n"
            "assert(pda.uptime() >= 0, 'uptime negativo')\n",
            err, sizeof(err));
        CHECK(e == ESP_OK, "A: smoke falhou: %s", err);
        printf("[lua] A: version/root/sd_mounted/uptime  OK\n");
    }

    /* ---- B: pda.fs.mkdir (resolve, criação, rejeição de ..) ---- */
    {
        g_mkdir_calls = 0; g_mkdir_last[0] = '\0';
        cap_reset();
        e = lua_runtime_run_string("assert(pda.fs.mkdir('testdir'))",
                                   err, sizeof(err));
        CHECK(e == ESP_OK, "B: mkdir falhou: %s", err);
        CHECK(g_mkdir_calls == 1 && !strcmp(g_mkdir_last, ROOT "/testdir"),
              "B: resolve_path errado: calls=%d last='%s'",
              g_mkdir_calls, g_mkdir_last);
        CHECK(storage_is_dir(ROOT "/testdir"), "B: diretório não criado");
        e = lua_runtime_run_string("pda.fs.mkdir('a/../b')", err, sizeof(err));
        CHECK(e == ESP_FAIL, "B: mkdir com '..' devia ser rejeitado");
        printf("[lua] B: pda.fs.mkdir (raiz+rel, criação real, '..' rejeitado)  OK\n");
    }

    /* ---- C: pda.sleep_ms (clamp silencioso, erro de tipo) ---- */
    printf("[lua] C: (as linhas 'E (lua_runtime)' abaixo são casos de erro INTENCIONAIS)\n");
    {
        cap_reset();
        e = lua_runtime_run_string(
            "pda.sleep_ms(50); pda.sleep_ms(0); pda.sleep_ms(-5); pda.sleep_ms(99999999)",
            err, sizeof(err));
        CHECK(e == ESP_OK, "C: sleep válido falhou: %s", err);
        e = lua_runtime_run_string("pda.sleep_ms('x')", err, sizeof(err));
        CHECK(e == ESP_FAIL, "C: sleep com string devia falhar");
        e = lua_runtime_run_string("pda.sleep_ms()", err, sizeof(err));
        CHECK(e == ESP_FAIL, "C: sleep sem argumento devia falhar");
        printf("[lua] C: pda.sleep_ms (0/clamp/inválidos sem travar)  OK\n");
    }

    /* ---- D: script REAL end-to-end, arquivo novo ---- */
    {
        g_mkdir_calls = 0; g_mkdir_last[0] = '\0';
        cap_reset();
        e = lua_runtime_run_file(SCRIPT, err, sizeof(err));
        CHECK(e == ESP_OK, "D: script falhou: %s", err);
        CHECK(g_mkdir_calls == 1 && !strcmp(g_mkdir_last, ROOT "/logs"),
              "D: mkdir('logs'): calls=%d last='%s'", g_mkdir_calls, g_mkdir_last);

        char lines[64][256];
        int nl = csv_load(CSV, lines, 64);
        CHECK(nl == 9, "D: CSV com %d linhas (esperado 9: header + 8)", nl);
        if (nl == 9) {
            CHECK(!strcmp(lines[0], HEADER), "D: header errado: '%s'", lines[0]);
            int prev_ut = -1;
            for (int i = 1; i <= 8; i++) {
                int n = -1, ut = -1;
                int ok = sscanf(lines[i], "%d;%d", &n, &ut) == 2;
                CHECK(ok, "D: linha %d fora do formato: '%s'", i, lines[i]);
                if (!ok) continue;
                CHECK(n == i, "D: linha %d com n=%d", i, n);
                CHECK(ut > prev_ut, "D: uptime_s não cresce (linha %d)", i);
                prev_ut = ut;
            }
        }
        CHECK(cap_has("logger_exemplo[8]"), "D: console sem a linha 8");
        CHECK(cap_has("fim — 8 linha(s)"), "D: resumo final errado");
        printf("[lua] D: script REAL — CSV novo, header + 8 linhas crescentes  OK\n");
    }

    /* ---- E: reexecução — continua sem duplicar header, respeita o máx. ---- */
    {
        cap_reset();
        e = lua_runtime_run_file(SCRIPT, err, sizeof(err));
        CHECK(e == ESP_OK, "E: script falhou: %s", err);
        char lines[64][256];
        int nl = csv_load(CSV, lines, 64);
        CHECK(nl == 9, "E: CSV mudou p/ %d linhas (esperado 9 — máx. atingido)", nl);
        if (nl == 9) {
            int headers = 0;
            for (int i = 0; i < nl; i++)
                if (!strcmp(lines[i], HEADER)) headers++;
            CHECK(headers == 1, "E: headers=%d (esperado 1 — sem duplicar)", headers);
        }
        CHECK(cap_has("continuando"), "E: não anunciou continuação");
        CHECK(cap_has("fim — 0 linha(s)"), "E: resumo errado");
        printf("[lua] E: arquivo existente continua (header único, máx. respeitado)  OK\n");
    }

    /* ---- F: sem diretório p/ gravar — falha graciosa ---- */
    {
        CHECK(system("rm -rf " ROOT "/logs") == 0, "F: prep falhou");
        g_mkdir_fail = 1;
        cap_reset();
        e = lua_runtime_run_file(SCRIPT, err, sizeof(err));
        g_mkdir_fail = 0;
        CHECK(e == ESP_OK, "F: script devia encerrar graciosamente: %s", err);
        CHECK(!storage_file_exists(CSV), "F: CSV não devia existir");
        CHECK(cap_has("FALHA ao gravar"), "F: não anunciou a falha");
        CHECK(cap_has("fim — 0 linha(s)"), "F: resumo errado");
        printf("[lua] F: sem onde gravar — encerra sem crash, anuncia no console  OK\n");
    }

    if (fails) { printf("LUA TEST: %d FALHA(S)\n", fails); return 1; }
    printf("LUA TEST: OK (bindings v2 + logger_exemplo.lua end-to-end: "
           "criação, continuação, falha graciosa)\n");
    return 0;
}
