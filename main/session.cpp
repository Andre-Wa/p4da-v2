/**
 * session.cpp — sessão de hibernação (salva no SD, restaura no boot).
 *
 * Herdado da v1 (M4.13b/M4.15.3 + correção v5.16.0):
 *  - hibernate não tem wake source no P4 (botões fora do domínio LP) —
 *    a volta é RESET/power-on; o flag NVS (pda_power) diz ao boot que há
 *    sessão para restaurar;
 *  - note= só pertence ao noteedit (v5.16.0: gravá-la junto de
 *    app=settings fazia o restore ler a tela de config como nota);
 *  - o restore só PARSEIA no bring-up e APLICA no event loop via
 *    ui_post (M4.15.3/UI-LOOP-GATE: aplicar antes do loop = assert
 *    xTaskToNotify==NULL).
 *
 * v2/L1: ramo "music" removido (volta na L3). Apps persistíveis:
 * files, notes, scripts, settings, noteedit.
 */

#include "screens.h"

#include "power_mgmt.h"
#include "storage_init.h"

#include "esp_log.h"

#include <cstdlib>
#include <cstring>

static const char *TAG = "session";

static std::string s_session_app = "launcher";
static std::string s_session_note = "";

static std::string s_rest_app, s_rest_note, s_rest_path;
bool s_rest_pending = false;

void session_set_app(const char *app) { s_session_app = app ? app : "launcher"; }
void session_set_note(const std::string &note) { s_session_note = note; }

void session_save(void *ctx)
{
    (void)ctx;
    char path[160];
    pda_path(path, sizeof(path), ".state/session.txt");
    char buf[320];
    /* v5.16.0 (15ª rodada): note= só pertence ao noteedit — o restore só o
     * usa nesse ramo. Gravá-la junto de app=settings fez o campo ler o log
     * como "hibernar gravou a TELA de config como nota". */
    if (s_session_app == "noteedit" && !s_session_note.empty())
        snprintf(buf, sizeof(buf), "app=%s\nnote=%s\n",
                 s_session_app.c_str(), s_session_note.c_str());
    else
        snprintf(buf, sizeof(buf), "app=%s\n", s_session_app.c_str());
    storage_write_text_file(path, buf, strlen(buf));
    ESP_LOGI(TAG, "sessão salva: %s", buf);
}

void session_restore_if_needed(void)
{
    if (!power_mgmt_woke_from_hibernate()) return;
    ESP_LOGI(TAG, "boot pós-hibernação detectado (flag NVS/reason)");
    char path[160];
    pda_path(path, sizeof(path), ".state/session.txt");
    char *data = NULL; size_t len = 0;
    if (storage_read_file_alloc(path, &data, &len) != ESP_OK) {
        ESP_LOGW(TAG, "pós-hibernação sem session.txt legível — nada a restaurar");
        return;
    }
    std::string s(data, len);
    free(data);

    std::string app, note;
    size_t p = s.find("app=");
    if (p != std::string::npos) app = s.substr(p + 4, s.find('\n', p) - p - 4);
    p = s.find("note=");
    if (p != std::string::npos) note = s.substr(p + 5, s.find('\n', p) - p - 5);

    /* M4.15.3: só PARSEIA aqui. Aplicar no bring-up crashava: o ramo
     * noteedit chamava slint::invoke_from_event_loop ANTES da task ui_loop
     * existir → assert xTaskToNotify==NULL (log de 2026-10-05). A
     * aplicação roda no event loop (app_main: ui_post após criar a
     * ui_loop; o gate garante a ordem). */
    ESP_LOGI(TAG, "sessão lida: app=%s note=%s (aplica no event loop)",
             app.c_str(), note.c_str());
    s_rest_app = app;
    s_rest_note = note;
    s_rest_path = path;
    s_rest_pending = true;
}

void session_restore_apply(void)
{
    if (!s_rest_pending) return;
    s_rest_pending = false;
    ESP_LOGI(TAG, "restaurando sessão: app=%s note=%s",
             s_rest_app.c_str(), s_rest_note.c_str());
    if (s_rest_app == "noteedit" && !s_rest_note.empty()) {
        std::string full = std::string(pda_root()) + "/" + s_rest_note;
        ed_open_path_async(full, true);
    } else if (s_rest_app == "notes") {
        notes_open();
    } else if (s_rest_app == "files") {
        files_open_root();
    } else if (s_rest_app == "scripts") {
        scripts_open();
    } else if (s_rest_app == "settings") {
        settings_open();
    }
    storage_delete_file(s_rest_path.c_str());
}
