/**
 * screens.h — entradas públicas dos módulos de tela da camada app.
 *
 * Cada screen_*.cpp é DONO do seu estado ( structs abaixo quando o estado
 * precisa ser visível a outro módulo) e expõe:
 *   - <tela>_wire(ui): registra os callbacks Slint da tela (chamado UMA
 *     vez pelo app_main, na ordem documentada em app_main.cpp);
 *   - entradas usadas por launcher/sessão/navegação.
 *
 * Na v1 tudo isso eram `static` de main.cpp; os structs EditorState e
 * FmState ficam aqui porque há um cruzamento legítimo e único: o OSK do
 * editor também edita o campo de texto do prompt do gerenciador
 * (rename/novo-arquivo/novo-dir), herança do M4.8 da v1.
 */
#pragma once

#include <cstddef>
#include <string>

#include "app_context.h"

/* ================================================================== */
/* EDITOR (screen_editor.cpp)                                          */
/* ================================================================== */
struct EditorState {
    std::string path;       /* caminho completo; "" = ainda sem arquivo */
    std::string text;
    size_t cursor = 0;
    bool dirty = false;
    bool is_new = false;
    int osk_override = -1;     /* -1 auto, 0 força off, 1 força on */
    bool reading = false;      /* modo leitura (markdown renderizado) */
    bool osk_shift = false;
    bool osk_mode = false;     /* false = abc, true = 123/símbolos */
};
extern EditorState g_ed;

void editor_wire(AppWindow &ui);      /* cria também o timer do cursor */
void ed_push_ui(bool scroll_to_cursor);
void ed_load(const std::string &path, const std::string &content_in,
             bool is_new, bool start_reading);
void ed_open_path_async(const std::string path, bool start_reading);
void ed_insert_str(const char *s, size_t n);
void ed_backspace(void);
void ed_delete_key(void);
void ed_move(int dcol, int dline, bool home, bool end);
void ed_push_osk_rows(void);
std::string ed_title(void);           /* relativo à raiz ativa p/ sessão */

/* ================================================================== */
/* GERENCIADOR DE ARQUIVOS (screen_files.cpp)                          */
/* ================================================================== */
struct FmState {
    bool sheet = false;
    std::string sheet_name;
    bool sheet_isdir = false;
    bool picker = false;
    std::string picker_op;      /* "copy" | "move" */
    std::string picker_src;
    std::string picker_label;
    bool prompt = false;
    std::string prompt_title;
    std::string prompt_text;
    size_t prompt_pos = 0;      /* cursor do prompt (offset de byte UTF-8) */
    std::string prompt_action;  /* rename | newdir | newfile | overwrite | delete */
    bool prompt_needs_text = false;
};
extern FmState g_fm;            /* visível p/ o OSK do editor (M4.8) */

void files_wire(AppWindow &ui);
void files_open_root(void);     /* entra em Arquivos na raiz ativa (launcher/sessão) */
void files_refresh(void);       /* re-lista o diretório corrente (nav_back) */
void list_dir_async(const std::string dir);
void push_fm_ui(void);

/* ================================================================== */
/* NOTAS (screen_notes.cpp)                                            */
/* ================================================================== */
void notes_wire(AppWindow &ui);
void notes_open(void);          /* lista + navega p/ NotesList */
void refresh_notes_list(void);

/* ================================================================== */
/* SCRIPTS (screen_scripts.cpp)                                        */
/* ================================================================== */
void scripts_wire(AppWindow &ui);
void scripts_open(void);        /* lista + navega p/ Scripts */
void refresh_scripts_list(void);

/* ================================================================== */
/* CONFIG + QUICK PANEL (screen_settings.cpp)                          */
/* ================================================================== */
void settings_wire(AppWindow &ui);
void settings_open(void);       /* push + navega p/ Settings */
void push_settings_to_ui(void); /* também usado no boot e pós-wake */

/* ================================================================== */
/* REDES (screen_networks.cpp) — camada L2                             */
/* ================================================================== */
void networks_wire(AppWindow &ui);
void networks_open(void);       /* navega p/ Networks + scan */
void net_start_scan(void);      /* launcher/quick panel/rescan */
extern std::string g_wifi_pick_ssid;  /* SSID aguardando senha no prompt */

/* ================================================================== */
/* MÚSICA (screen_music.cpp) — camada L3                               */
/* ================================================================== */
void music_wire(AppWindow &ui);
void music_open(void);          /* lista + navega p/ Music */
void refresh_music_list(void);
void push_mus_ui(void);         /* refresh de estado p/ callbacks de driver */
void music_on_media_key(int act);

/* ================================================================== */
/* LAUNCHER (screen_launcher.cpp)                                      */
/* ================================================================== */
void launcher_wire(AppWindow &ui);

/* ================================================================== */
/* STATUS BAR (status_bar.cpp)                                         */
/* ================================================================== */
void status_bar_wire(AppWindow &ui);
void update_clock(void);        /* relógio + estado de energia (+store no wake) */

/* ================================================================== */
/* SESSÃO de hibernação (session.cpp)                                  */
/* ================================================================== */
void session_set_app(const char *app);       /* "files"|"notes"|"scripts"|"settings"|"noteedit" */
void session_set_note(const std::string &note);
void session_save(void *ctx);                /* callback pda_power_hibernate_save_cb */
void session_restore_if_needed(void);        /* boot: só PARSEIA (M4.15.3) */
void session_restore_apply(void);            /* roda no event loop via ui_post */
extern bool s_rest_pending;

/* ================================================================== */
/* ENTRADA USB (input_usb.cpp)                                         */
/* ================================================================== */
void input_usb_init(void);      /* teclado HID -> editor + has_keyboard */
