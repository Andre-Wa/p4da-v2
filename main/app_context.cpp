/**
 * app_context.cpp — utilidades compartilhadas da camada app.
 *
 * Herdado da v1 (main.cpp): UI-LOOP-GATE (v5.16.0), spawn_thread como
 * FreeRTOS task em PSRAM, navegação com pilha de telas, helpers de
 * caminho/UTF-8. Código 1:1 onde possível — os comentários de campo da
 * v1 (datas/rodadas) foram mantidos de propósito: são a memória das
 * causas raízes.
 */

#include "app_context.h"
#include "screens.h"

#include "ui_gate.h"

#include "pda_config.h"
#include "power_mgmt.h"
#include "display_init.h"
#include "storage_init.h"

#include "esp_log.h"
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstring>
#include <strings.h>
#include <vector>

static const char *TAG = "app";

/* ---- estado global compartilhado ---------------------------------- */
AppWindow *g_ui = nullptr;   /* ponteiro do component (handle vive em app_main) */
std::shared_ptr<slint::VectorModel<slint::SharedString>> g_script_log;

/* ---- UI-LOOP-GATE (v5.16.0) ----
 * Crash da 15ª rodada da v1 (boot pós-hibernação com device UAC ligado):
 * `assert failed: vTaskGenericNotifyGiveFromISR tasks.c:6171 (xTaskToNotify)`.
 * Causa: slint::invoke_from_event_loop ANTES do run_event_loop notifica a
 * task estática do backend ainda NULL — slint-esp.cpp v1.12.1: task={}
 * (L336), atribuída só na 1ª linha do loop (L132), notify incondicional
 * (L332); configASSERT(xTaskToNotify) no tasks.c do IDF v5.5.1. O buraco
 * é estrutural (qualquer callback de driver durante o bring-up: HID
 * connect, print do Lua, evento de storage...). ui_post() substitui o
 * invoke em TODA a camada app: antes do loop acumula (FIFO), depois
 * encaminha. A task ui_gate dá flush 100 ms após a criação da ui_loop.
 * Evidência e teste de host: ui_gate.h, tools/hosttest/host_uigate_test.cpp. */
static void ui_invoke_real(std::function<void()> fn)
{
    slint::invoke_from_event_loop(std::move(fn));
}
static UiGate<void (*)(std::function<void()>)> g_ui_gate(&ui_invoke_real);

void ui_post(std::function<void()> fn) { g_ui_gate.post(std::move(fn)); }
int ui_gate_flush(void) { return g_ui_gate.flush(); }

/* ---- tasks de I/O -------------------------------------------------- */
/* Threads de I/O e de scripts como tasks FreeRTOS com pilha em PSRAM:
 * a RAM interna ficou escassa com o ESP-Hosted na v1 e o std::thread
 * falhava com "pthread: Failed to create task!" -> exceção -> abort
 * (2026-09-24). Semântica de detach: a task se auto-apaga ao terminar.
 * (Na L1 o hosted não está no build, mas o padrão permanece: é o que
 * está validado e a PSRAM segue sendo o lugar certo p/ pilhas grandes.) */
static void thread_trampoline(void *arg)
{
    ThreadCtx *c = static_cast<ThreadCtx *>(arg);
    c->fn();
    delete c;
    vTaskDelete(NULL);
}

void spawn_thread_impl(const char *name, size_t stack, ThreadCtx *ctx)
{
    if (!ctx) {
        ESP_LOGE(TAG, "spawn_thread(%s): sem memória p/ contexto", name);
        return;
    }
    BaseType_t ok = xTaskCreateWithCaps(thread_trampoline, name, (uint32_t)stack,
                                        ctx, 5, NULL, MALLOC_CAP_SPIRAM);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "spawn_thread(%s): xTaskCreateWithCaps falhou", name);
        delete ctx;
    }
}

void activity(void) { power_mgmt_activity(); }

/* ---- console/log ---------------------------------------------------- */
void log_line(const char *line, void *ctx)
{
    (void)ctx;
    std::string s(line);
    ui_post([s]() {
        if (g_script_log) {
            g_script_log->push_back(slint::SharedString(s));
            while (g_script_log->row_count() > 200) g_script_log->erase(0);
        }
    });
}

void toast(const char *msg, void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "toast: %s", msg);
    char buf[300];
    snprintf(buf, sizeof(buf), "[toast] %s", msg);
    log_line(buf, NULL);
}

/* ---- brilho ---------------------------------------------------------- */
void apply_brightness_from_settings(void)
{
    if (power_mgmt_state() == PDA_PWR_ACTIVE) {
        board_display_backlight_set((uint8_t)pda_settings()->brightness);
    }
}

void settings_changed_by_lua(void)
{
    ui_post([]() { apply_brightness_from_settings(); });
}

/* ---- navegação -------------------------------------------------------- */
static std::vector<AppState> g_nav_hist;   /* pilha de telas p/ Voltar global */

void nav_goto(AppState st)
{
    AppState cur = g_ui->get_active_app();
    if (cur != st) g_nav_hist.push_back(cur);
    g_ui->set_active_app(st);
}

void nav_back(void)
{
    while (!g_nav_hist.empty()) {
        AppState prev = g_nav_hist.back();
        g_nav_hist.pop_back();
        g_ui->set_active_app(prev);
        if (prev == AppState::FileManager) { files_refresh(); return; }
        if (prev == AppState::NotesList) { refresh_notes_list(); return; }
        if (prev == AppState::Scripts) { refresh_scripts_list(); return; }
        if (prev != AppState::Editor) return;   /* Editor sem estado p/ restaurar: continua descendo */
    }
    g_ui->set_active_app(AppState::Launcher);
}

/* ---- caminhos + classificação ---------------------------------------- */
std::string notes_dir(void)
{
    char buf[160];
    pda_path(buf, sizeof(buf), "notes");
    return std::string(buf);
}

/* Join de caminho que respeita a raiz "/" (evita "//sdcard"). */
std::string path_join(const std::string &dir, const std::string &name)
{
    if (dir == "/") return "/" + name;
    return dir + "/" + name;
}

std::string base_name(const std::string &p)
{
    size_t i = p.find_last_of('/');
    return (i == std::string::npos) ? p : p.substr(i + 1);
}

bool is_textish(const char *name)
{
    const char *exts[] = { ".txt", ".lua", ".md", ".csv", ".log", ".ini", ".json" };
    size_t n = strlen(name);
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        size_t e = strlen(exts[i]);
        if (n > e && !strcasecmp(name + n - e, exts[i])) return true;
    }
    return false;
}

/* Categoria de ícone (o codepoint vive estático em icons.slint). */
const char *icon_kind_for(const char *name, bool isdir)
{
    if (isdir) return "dir";
    size_t n = strlen(name);
    struct { const char *ext; const char *kind; } map[] = {
        { ".lua", "lua" }, { ".sh", "sh" },
        { ".mp3", "audio" }, { ".wav", "audio" }, { ".flac", "audio" }, { ".ogg", "audio" },
        { ".png", "img" }, { ".jpg", "img" }, { ".jpeg", "img" },
        { ".gif", "img" }, { ".bmp", "img" },
        { ".txt", "txt" }, { ".md", "txt" }, { ".csv", "txt" },
        { ".log", "txt" }, { ".ini", "txt" }, { ".json", "txt" },
    };
    for (auto &m : map) {
        size_t e = strlen(m.ext);
        if (n > e && !strcasecmp(name + n - e, m.ext)) return m.kind;
    }
    return "file";
}

/* ---- UTF-8 ------------------------------------------------------------- */
bool utf8_is_cont(char c) { return ((unsigned char)c & 0xC0) == 0x80; }

size_t utf8_prev(const std::string &t, size_t pos)
{
    if (pos == 0) return 0;
    size_t p = pos - 1;
    while (p > 0 && utf8_is_cont(t[p])) p--;
    return p;
}

size_t utf8_next(const std::string &t, size_t pos)
{
    if (pos >= t.size()) return t.size();
    size_t n = pos + 1;
    while (n < t.size() && utf8_is_cont(t[n])) n++;
    return n;
}

/* codepoints em t[0..end) — p/ converter coluna-bytes em coluna-células
 * (o overlay do cursor-bloco no .slint posiciona por célula mono). */
size_t utf8_count(const std::string &t, size_t end)
{
    size_t n = 0;
    for (size_t i = 0; i < end && i < t.size(); i++)
        if (!utf8_is_cont(t[i])) n++;
    return n;
}

/* snap p/ trás até um limite de codepoint */
size_t utf8_snap(const std::string &t, size_t pos)
{
    while (pos > 0 && pos < t.size() && utf8_is_cont(t[pos])) pos--;
    return pos;
}
