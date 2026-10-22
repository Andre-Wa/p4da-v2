/**
 * screen_files.cpp — gerenciador de arquivos (lista, navega, operações).
 *
 * Herdado da v1 (M1 + M3b + M3b.1): listagem async com ícones por
 * categoria, raiz virtual "/" sintetizando os mounts (o VFS do IDF não
 * enumera "/"), sheet de ações (abrir/renomear/copiar/mover/apagar),
 * picker de destino (não sobrescreve), prompt com cursor UTF-8,
 * histórico de diretórios p/ Voltar.
 *
 * Toda mutação de UI volta por ui_post(); toda I/O roda em spawn_thread.
 */

#include "screens.h"

#include "storage_init.h"

#include "esp_log.h"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <dirent.h>
#include <string>
#include <vector>

static const char *TAG = "files";

/* ---- estado dono deste módulo --------------------------------------- */
FmState g_fm;
static std::string g_fm_dir;                 /* diretório corrente */
static std::vector<std::string> g_dir_hist;  /* histórico p/ Voltar em Arquivos */

/* ---- listagem async -------------------------------------------------- */
void list_dir_async(const std::string dir)
{
    spawn_thread("io_list", 8192, [dir]() {
        auto names = std::make_shared<slint::VectorModel<slint::SharedString>>();
        auto isdir = std::make_shared<slint::VectorModel<bool>>();
        auto icons = std::make_shared<slint::VectorModel<slint::SharedString>>();

        /* O VFS do IDF não enumera "/" (opendir falha): sintetiza a lista
         * de mounts para a raiz virtual. */
        if (dir == "/") {
            if (storage_sd_mounted()) {
                names->push_back(slint::SharedString("sdcard"));
                isdir->push_back(true);
                icons->push_back(slint::SharedString(icon_kind_for("sdcard", true)));
            }
            names->push_back(slint::SharedString("internal"));
            isdir->push_back(true);
            icons->push_back(slint::SharedString(icon_kind_for("internal", true)));
            ui_post([names, isdir, icons, dir]() {
                g_ui->set_file_entries(names);
                g_ui->set_file_is_dir(isdir);
                g_ui->set_file_kind(icons);
                g_ui->set_file_path(slint::SharedString(dir));
            });
            return;
        }

        DIR *d = opendir(dir.c_str());
        if (d) {
            std::vector<std::string> dirs, files;
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                std::string n(ent->d_name);
                if (n == "." || n == "..") continue;
                std::string full = dir + "/" + n;
                if (storage_is_dir(full.c_str())) dirs.push_back(n);
                else files.push_back(n);
            }
            closedir(d);
            for (auto &n : dirs) {
                names->push_back(slint::SharedString(n));
                isdir->push_back(true);
                icons->push_back(slint::SharedString(icon_kind_for(n.c_str(), true)));
            }
            for (auto &n : files) {
                names->push_back(slint::SharedString(n));
                isdir->push_back(false);
                icons->push_back(slint::SharedString(icon_kind_for(n.c_str(), false)));
            }
            if (dirs.empty() && files.empty()) {
                names->push_back(slint::SharedString("(vazio)"));
                isdir->push_back(false);
                icons->push_back(slint::SharedString(""));
            }
        } else {
            names->push_back(slint::SharedString("(não montado)"));
            isdir->push_back(false);
            icons->push_back(slint::SharedString(""));
        }

        ui_post([names, isdir, icons, dir]() {
            g_ui->set_file_entries(names);
            g_ui->set_file_is_dir(isdir);
            g_ui->set_file_kind(icons);
            g_ui->set_file_path(slint::SharedString(dir));
        });
    });
}

/* ---- entradas p/ launcher/sessão/nav ---------------------------------- */
void files_open_root(void)
{
    g_fm_dir = pda_root();
    g_dir_hist.clear();
    list_dir_async(g_fm_dir);
    nav_goto(AppState::FileManager);
}

void files_refresh(void) { list_dir_async(g_fm_dir); }

/* ---- navegação interna ------------------------------------------------- */
static void fm_enter_dir(const std::string &dir)
{
    g_dir_hist.push_back(g_fm_dir);
    g_fm_dir = dir;
    list_dir_async(g_fm_dir);
}

void push_fm_ui(void)
{
    g_ui->set_fm_sheet(g_fm.sheet);
    g_ui->set_fm_sheet_name(slint::SharedString(g_fm.sheet_name));
    g_ui->set_fm_sheet_isdir(g_fm.sheet_isdir);
    g_ui->set_fm_picker(g_fm.picker);
    g_ui->set_fm_picker_label(slint::SharedString(g_fm.picker_label));
    g_ui->set_fm_prompt(g_fm.prompt);
    g_ui->set_fm_prompt_title(slint::SharedString(g_fm.prompt_title));
    /* campo com cursor: parte o texto no caret (o overlay desenha
     * pré | caret | sufixo — setas do OSK movem o caret, M4.8). */
    size_t pp = g_fm.prompt_pos;
    if (pp > g_fm.prompt_text.size()) pp = g_fm.prompt_text.size();
    g_ui->set_fm_prompt_pre(slint::SharedString(g_fm.prompt_text.substr(0, pp)));
    g_ui->set_fm_prompt_suf(slint::SharedString(g_fm.prompt_text.substr(pp)));
    g_ui->set_fm_prompt_needs_text(g_fm.prompt_needs_text);
}

static void fm_open_entry(const std::string name)
{
    if (name == "(vazio)" || name == "(não montado)") return;
    std::string full = path_join(g_fm_dir, name);
    if (g_fm.picker) {
        if (storage_is_dir(full.c_str())) {
            fm_enter_dir(full);
        } else {
            log_line("[seletor] navegue até um diretório e toque em Selecionar", NULL);
        }
        return;
    }
    if (storage_is_dir(full.c_str())) {
        fm_enter_dir(full);
        return;
    }
    if (is_textish(name.c_str())) {
        bool md = name.size() > 3 && !strcasecmp(name.c_str() + name.size() - 3, ".md");
        ed_open_path_async(full, md);
        return;
    }
    char buf[256];
    int64_t sz = storage_file_size(full.c_str());
    snprintf(buf, sizeof(buf), "[info] %s: %ld bytes (visualização no M3)",
             name.c_str(), (long)sz);
    log_line(buf, NULL);
}

/* ---- wiring dos callbacks Slint ---------------------------------------- */
void files_wire(AppWindow &ui)
{
    g_fm_dir = pda_root();

    ui.on_request_dir([](slint::SharedString arg) {
        activity();
        std::string a(arg.data());
        if (!a.empty() && a[0] == '/') {
            g_fm_dir = a;
            list_dir_async(g_fm_dir);
        } else {
            fm_open_entry(a);
        }
    });
    ui.on_dir_up([]() {
        activity();
        ESP_LOGI(TAG, "fm: Sobe (prompt=%d sheet=%d picker=%d dir=%s)",
                 (int)g_fm.prompt, (int)g_fm.sheet, (int)g_fm.picker, g_fm_dir.c_str());
        if (g_fm.prompt || g_fm.sheet) {
            g_fm.prompt = g_fm.sheet = false;
            push_fm_ui();
            return;
        }
        /* Sobe até "/" (onde o VFS lista os mounts /sdcard e /internal).
         * Sem clamp em pda_root: era isso que prendia o usuário em
         * /sdcard/pda (reporte 2026-09-28; o replace anterior não casou). */
        std::string d = g_fm_dir;
        if (d == "/") return;
        size_t slash = d.find_last_of('/');
        std::string parent = (slash == std::string::npos || slash == 0)
                                 ? std::string("/")
                                 : d.substr(0, slash);
        g_fm_dir = parent;
        g_dir_hist.push_back(d);
        ESP_LOGI(TAG, "fm: subindo p/ %s", g_fm_dir.c_str());
        list_dir_async(g_fm_dir);
    });

    /* ---------- operações de arquivo (M3b) ---------- */
    ui.on_fm_more([](slint::SharedString name, bool isdir) {
        activity();
        g_fm.sheet = true;
        g_fm.sheet_name = std::string(name.data());
        g_fm.sheet_isdir = isdir;
        push_fm_ui();
    });
    ui.on_fm_sheet_action([](slint::SharedString a) {
        activity();
        std::string act(a.data());
        if (act == "cancelar") { g_fm.sheet = false; push_fm_ui(); return; }
        if (act == "abrir") {
            g_fm.sheet = false;
            push_fm_ui();
            fm_open_entry(g_fm.sheet_name);
            return;
        }
        if (act == "renomear") {
            g_fm.sheet = false;
            g_fm.prompt = true;
            g_fm.prompt_needs_text = true;
            g_fm.prompt_title = "Renomear \"" + g_fm.sheet_name + "\" para:";
            g_fm.prompt_text = g_fm.sheet_name;
            g_fm.prompt_pos = g_fm.prompt_text.size();
            g_fm.prompt_action = "rename";
            push_fm_ui();
            return;
        }
        if (act == "copiar" || act == "mover") {
            g_fm.sheet = false;
            g_fm.picker = true;
            g_fm.picker_op = (act == "copiar") ? "copy" : "move";
            g_fm.picker_src = path_join(g_fm_dir, g_fm.sheet_name);
            g_fm.picker_label = (act == "copiar" ? "Copiar \"" : "Mover \"")
                              + g_fm.sheet_name + "\" para:";
            push_fm_ui();
            return;
        }
        if (act == "apagar") {
            g_fm.sheet = false;
            g_fm.prompt = true;
            g_fm.prompt_needs_text = false;
            g_fm.prompt_title = "Apagar \"" + g_fm.sheet_name + "\"? Não dá para desfazer.";
            g_fm.prompt_action = "delete";
            push_fm_ui();
            return;
        }
    });
    ui.on_fm_newfile([]() {
        activity();
        g_fm.prompt = true;
        g_fm.prompt_needs_text = true;
        g_fm.prompt_title = "Nome do novo arquivo (ex.: nota.txt):";
        g_fm.prompt_text = "";
        g_fm.prompt_pos = g_fm.prompt_text.size();
        g_fm.prompt_action = "newfile";
        push_fm_ui();
    });
    ui.on_fm_newdir([]() {
        activity();
        g_fm.prompt = true;
        g_fm.prompt_needs_text = true;
        g_fm.prompt_title = "Nome do novo diretório:";
        g_fm.prompt_text = "";
        g_fm.prompt_pos = g_fm.prompt_text.size();
        g_fm.prompt_action = "newdir";
        push_fm_ui();
    });
    ui.on_fm_picker_select([]() {
        activity();
        if (!g_fm.picker) return;
        std::string dest = path_join(g_fm_dir, base_name(g_fm.picker_src));
        char msg[320];
        esp_err_t err = (g_fm.picker_op == "copy")
            ? storage_copy_file(g_fm.picker_src.c_str(), dest.c_str())
            : storage_move_file(g_fm.picker_src.c_str(), dest.c_str());
        if (err == ESP_OK) {
            snprintf(msg, sizeof(msg), "[ok] %s -> %s",
                     g_fm.picker_op.c_str(), dest.c_str());
        } else {
            snprintf(msg, sizeof(msg), "[erro] %s falhou (%s) — destino existe?",
                     g_fm.picker_op.c_str(), esp_err_to_name(err));
        }
        log_line(msg, NULL);
        g_fm.picker = false;
        push_fm_ui();
        list_dir_async(g_fm_dir);
    });
    ui.on_fm_prompt_ok([]() {
        activity();
        if (!g_fm.prompt) return;
        std::string target = path_join(g_fm_dir, g_fm.sheet_name);
        char msg[320];
        if (g_fm.prompt_action == "rename") {
            std::string nt = g_fm.prompt_text;
            if (nt.empty() || nt == "." || nt == ".." || nt.find('/') != std::string::npos) {
                log_line("[erro] nome inválido", NULL);
            } else {
                std::string dest = path_join(g_fm_dir, nt);
                esp_err_t err = storage_move_file(target.c_str(), dest.c_str());
                snprintf(msg, sizeof(msg), err == ESP_OK ? "[ok] renomeado p/ %s" : "[erro] renomear (%s)",
                         err == ESP_OK ? nt.c_str() : esp_err_to_name(err));
                log_line(msg, NULL);
            }
        } else if (g_fm.prompt_action == "newdir") {
            std::string nt = g_fm.prompt_text;
            if (nt.empty() || nt.find('/') != std::string::npos) {
                log_line("[erro] nome inválido", NULL);
            } else {
                storage_ensure_dir((path_join(g_fm_dir, nt)).c_str());
                log_line("[ok] diretório criado", NULL);
            }
        } else if (g_fm.prompt_action == "overwrite") {
            /* segundo passo: confirmação de substituição */
            std::string path = path_join(g_fm_dir, g_fm.prompt_text);
            if (storage_write_text_file(path.c_str(), "", 0) == ESP_OK) {
                log_line("[ok] arquivo substituído", NULL);
                g_fm.prompt = false;
                push_fm_ui();
                ed_open_path_async(path, false);
                return;
            }
            log_line("[erro] falha ao substituir", NULL);
        } else if (g_fm.prompt_action == "newfile") {
            std::string nt = g_fm.prompt_text;
            if (nt.empty() || nt.find('/') != std::string::npos) {
                log_line("[erro] nome inválido", NULL);
            } else {
                std::string path = path_join(g_fm_dir, nt);
                if (storage_file_exists(path.c_str())) {
                    /* confirma antes de sobrescrever */
                    g_fm.prompt_title = "Já existe \"" + nt + "\". Substituir?";
                    g_fm.prompt_text = nt;
                    g_fm.prompt_pos = g_fm.prompt_text.size();
                    g_fm.prompt_needs_text = false;
                    g_fm.prompt_action = "overwrite";
                    push_fm_ui();
                    return;
                } else if (storage_write_text_file(path.c_str(), "", 0) == ESP_OK) {
                    log_line("[ok] arquivo criado", NULL);
                    g_fm.prompt = false;
                    push_fm_ui();
                    ed_open_path_async(path, false);   /* já abre p/ editar */
                    return;
                }
            }
        } else if (g_fm.prompt_action == "delete") {
            esp_err_t err = storage_rm_rf(target.c_str());
            snprintf(msg, sizeof(msg), err == ESP_OK ? "[ok] apagado: %s" : "[erro] apagar (%s)",
                     err == ESP_OK ? g_fm.sheet_name.c_str() : esp_err_to_name(err));
            log_line(msg, NULL);
        }
        g_fm.prompt = false;
        push_fm_ui();
        list_dir_async(g_fm_dir);
    });
    ui.on_fm_prompt_cancel([]() {
        activity();
        g_fm.prompt = false;
        push_fm_ui();
    });
    ui.on_fm_back([]() {
        activity();
        if (g_fm.prompt) { g_fm.prompt = false; push_fm_ui(); return; }
        if (g_fm.sheet) { g_fm.sheet = false; push_fm_ui(); return; }
        if (g_fm.picker) { g_fm.picker = false; push_fm_ui(); return; }
        if (!g_dir_hist.empty()) {
            g_fm_dir = g_dir_hist.back();
            g_dir_hist.pop_back();
            list_dir_async(g_fm_dir);
            return;
        }
        nav_back();
    });
}
