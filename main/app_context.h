/**
 * app_context.h — contrato INTERNO da camada de aplicação (main/).
 *
 * A camada app do p4da-v2 é um conjunto de módulos de tela
 * (screen_*.cpp) + serviços de shell (status bar, sessão, entrada USB)
 * que compartilham UM handle de janela Slint e um punhado de utilidades.
 * Este header é o único ponto de acoplamento entre eles — na v1 tudo
 * isso eram `static` de um main.cpp de 2141 linhas.
 *
 * Regras (docs/ARCHITECTURE.md):
 *  1. Nada aqui bloqueia a UI: I/O de disco roda em spawn_thread() e o
 *     resultado volta por ui_post().
 *  2. ui_post() é o ÚNICO caminho p/ mutar propriedades Slint fora do
 *     event loop (UI-LOOP-GATE, ui_gate.h — herdado da v1 v5.16.0).
 *  3. Componentes (pda_board/pda_script/pda_power/pda_usb) não conhecem
 *     este header; a injeção de dependências acontece no app_main
 *     (composition root).
 */
#pragma once

#include <functional>
#include <memory>
#include <new>
#include <string>

#include "app_ui.h"   /* gerado a partir de ui/app_ui.slint */

/* ---- janela + modelos compartilhados ------------------------------ */
extern AppWindow *g_ui;   /* ponteiro cru; o handle vive no app_main */
extern std::shared_ptr<slint::VectorModel<slint::SharedString>> g_script_log;

/* ---- UI-LOOP-GATE (ui_gate.h) -------------------------------------- */
/* Seguro em QUALQUER thread e em qualquer momento do boot: antes do
 * event loop acumula (FIFO), depois encaminha p/ invoke_from_event_loop. */
void ui_post(std::function<void()> fn);
/* Chamado UMA vez pela task ui_gate (+100 ms após a ui_loop subir). */
int ui_gate_flush(void);

/* ---- tasks de I/O --------------------------------------------------- */
/* FreeRTOS task com pilha em PSRAM e semântica de detach (auto-delete).
 * Herdado da v1: std::thread falhava com o heap interno curto pós-hosted
 * (2026-09-24); os nomes de task são contrato (power_mgmt loga HWM por
 * nome: "io_edit", "io_list", "lua_script"...). */
struct ThreadCtx {
    std::function<void()> fn;
};
void spawn_thread_impl(const char *name, size_t stack, ThreadCtx *ctx);

template <typename F>
inline void spawn_thread(const char *name, size_t stack, F &&fn)
{
    spawn_thread_impl(name, stack,
                      new (std::nothrow) ThreadCtx { std::forward<F>(fn) });
}

/* ---- energia -------------------------------------------------------- */
/* power_mgmt_activity() — todo handler de UI/teclado/toque chama. */
void activity(void);

/* ---- console/log (tela Scripts) ------------------------------------- */
/* Assinatura (const char*, void*) = pda_lua_log_fn: registrado direto
 * como sink de log/toast da VM Lua. */
void log_line(const char *line, void *ctx = nullptr);
void toast(const char *msg, void *ctx = nullptr);

/* ---- brilho/config --------------------------------------------------- */
void apply_brightness_from_settings(void);
/* Callback de "script mudou settings": reaplica brilho no event loop. */
void settings_changed_by_lua(void);

/* ---- navegação entre telas ------------------------------------------- */
void nav_goto(AppState st);
void nav_back(void);

/* ---- caminhos + classificação de arquivos ---------------------------- */
std::string notes_dir(void);                       /* <pda_root>/notes */
std::string path_join(const std::string &dir, const std::string &name);
std::string base_name(const std::string &p);
bool is_textish(const char *name);                 /* abre no editor? */
const char *icon_kind_for(const char *name, bool isdir);  /* categorias p/ IconForKind */

/* ---- UTF-8: o cursor anda por CODEPOINTS, nunca por bytes ------------
 * Sem isso, setas/backspace em texto acentuado pousavam no meio de uma
 * sequência multibyte e o Slint (Rust) panicava com UTF-8 inválido
 * (Guru Meditation em 2026-09-24, v1). */
bool utf8_is_cont(char c);
size_t utf8_prev(const std::string &t, size_t pos);
size_t utf8_next(const std::string &t, size_t pos);
size_t utf8_count(const std::string &t, size_t end);
size_t utf8_snap(const std::string &t, size_t pos);
