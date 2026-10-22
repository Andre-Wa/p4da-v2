/**
 * screen_notes.cpp — lista de notas + abrir/nova.
 *
 * Herdado da v1 (M1 + M4.6): o display stripa ".txt" (notas antigas) mas
 * abre o arquivo REAL (.md/.lua/... aparecem com o nome cheio); "+ Nova
 * nota" gera nota<N> incremental; .md abre em modo LEITURA, demais em
 * edição. Notas vivem em <pda_root>/notes.
 */

#include "screens.h"

#include "storage_init.h"

#include "esp_log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <strings.h>
#include <utility>
#include <vector>

static const char *TAG = "notes";

static std::string generate_new_note_name(void)
{
    int max_n = 0;
    DIR *dir = opendir(notes_dir().c_str());
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            std::string name(ent->d_name);
            if (name.rfind("nota", 0) == 0) {
                size_t dot = name.find(".txt");
                if (dot != std::string::npos && dot > 4) {
                    int n = atoi(name.substr(4, dot - 4).c_str());
                    if (n > max_n) max_n = n;
                }
            }
        }
        closedir(dir);
    }
    return "nota" + std::to_string(max_n + 1);
}

/* Nomes REAIS dos arquivos de notas (o display stripa ".txt"). Preenchido
 * na thread de UI junto com o modelo; open_note usa para mapear
 * display -> arquivo (bug pré-M4.8: qualquer nota ganhava ".txt" extra e
 * ".md" nunca abria daqui). */
static std::vector<std::string> s_notes_real;

void refresh_notes_list(void)
{
    spawn_thread("io_notes", 8192, []() {
        auto model = std::make_shared<slint::VectorModel<slint::SharedString>>();
        auto pairs = std::make_shared<std::vector<std::pair<std::string, std::string>>>();
        DIR *dir = opendir(notes_dir().c_str());
        if (dir) {
            struct dirent *ent;
            while ((ent = readdir(dir)) != NULL) {
                std::string name(ent->d_name);
                if (name == "." || name == "..") continue;
                std::string disp = name;
                if (disp.size() > 4 && disp.compare(disp.size() - 4, 4, ".txt") == 0)
                    disp = disp.substr(0, disp.size() - 4);
                pairs->emplace_back(disp, name);
            }
            closedir(dir);
        }
        std::sort(pairs->begin(), pairs->end());
        for (auto &pr : *pairs) model->push_back(slint::SharedString(pr.first));
        if (pairs->empty())
            model->push_back(slint::SharedString("(nenhuma nota — toque em + Nova nota)"));
        ui_post([model, pairs]() {
            s_notes_real.clear();
            for (auto &pr : *pairs) s_notes_real.push_back(pr.second);
            g_ui->set_notes_list(model);
        });
    });
}

void notes_open(void)
{
    refresh_notes_list();
    nav_goto(AppState::NotesList);
}

void notes_wire(AppWindow &ui)
{
    ui.on_request_notes_list([]() { activity(); refresh_notes_list(); });
    ui.on_open_note([](slint::SharedString name) {
        activity();
        std::string n(name.data());
        if (n.rfind("(nenhuma", 0) == 0) return;
        /* display -> nome real (a lista stripa ".txt"; ".md"/".lua"/etc.
         * aparecem com o nome cheio). */
        std::string real;
        for (const auto &r : s_notes_real) {
            std::string d = r;
            if (d.size() > 4 && d.compare(d.size() - 4, 4, ".txt") == 0)
                d = d.substr(0, d.size() - 4);
            if (d == n) { real = r; break; }
        }
        if (real.empty()) real = n + ".txt";   /* fallback: nota antiga */
        if (!is_textish(real.c_str())) {
            ESP_LOGW(TAG, "notas: formato não suportado, ignorando %s", real.c_str());
            return;
        }
        size_t nl = real.size();
        bool md = nl > 3 && strcasecmp(real.c_str() + nl - 3, ".md") == 0;
        ed_open_path_async(path_join(notes_dir(), real), md);
    });
    ui.on_new_note([]() {
        activity();
        std::string name = generate_new_note_name();
        std::string path = notes_dir() + "/" + name + ".txt";
        ed_load(path, "", true, false);
    });
}
