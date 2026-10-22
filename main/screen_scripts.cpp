/**
 * screen_scripts.cpp — lista + execução de scripts Lua com console.
 *
 * Herdado da v1 (M1): a VM é single-thread — execuções serializadas por
 * g_lua_mtx (uma de cada vez; try_lock avisa "já existe script rodando").
 * Scripts rodam na task "lua_script" (32K em PSRAM) para não travar a UI;
 * pda.sleep_ms() suspende só essa task (o nome é contrato: power_mgmt
 * loga HWM por nome e docs/LUA.md §loggers se refere a ela).
 */

#include "screens.h"

#include "lua_runtime.h"
#include "storage_init.h"

#include <cstdio>
#include <dirent.h>
#include <mutex>
#include <string>

static std::mutex g_lua_mtx;

void refresh_scripts_list(void)
{
    spawn_thread("io_scripts", 8192, []() {
        auto model = std::make_shared<slint::VectorModel<slint::SharedString>>();
        char dir[160];
        pda_path(dir, sizeof(dir), "scripts");
        DIR *d = opendir(dir);
        int count = 0;
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                std::string n(ent->d_name);
                if (n.size() > 4 && n.compare(n.size() - 4, 4, ".lua") == 0) {
                    model->push_back(slint::SharedString(n));
                    count++;
                }
            }
            closedir(d);
        }
        if (count == 0)
            model->push_back(slint::SharedString("(sem .lua em scripts/)"));
        ui_post([model]() { g_ui->set_scripts_list(model); });
    });
}

static void run_script_async(const std::string name)
{
    spawn_thread("lua_script", 32768, [name]() {
        if (!g_lua_mtx.try_lock()) {
            log_line("[aviso] já existe script rodando", NULL);
            return;
        }
        std::lock_guard<std::mutex> lk(g_lua_mtx, std::adopt_lock);
        char path[200];
        snprintf(path, sizeof(path), "%s/scripts/%s", pda_root(), name.c_str());
        char msg[256];
        snprintf(msg, sizeof(msg), ">>> executando %s", name.c_str());
        log_line(msg, NULL);
        esp_err_t err = lua_runtime_run_file(path, msg, sizeof(msg));
        if (err == ESP_OK) {
            snprintf(msg, sizeof(msg), "<<< %s ok", name.c_str());
        } else {
            snprintf(msg, sizeof(msg), "<<< %s falhou (ver console)", name.c_str());
        }
        log_line(msg, NULL);
    });
}

void scripts_open(void)
{
    refresh_scripts_list();
    nav_goto(AppState::Scripts);
}

void scripts_wire(AppWindow &ui)
{
    ui.on_request_scripts([]() { activity(); refresh_scripts_list(); });
    ui.on_run_script([](slint::SharedString name) {
        activity();
        std::string n(name.data());
        if (n.rfind("(sem", 0) == 0) return;
        run_script_async(n);
    });
    ui.on_clear_script_log([]() {
        activity();
        if (g_script_log) g_script_log->clear();
    });
}
