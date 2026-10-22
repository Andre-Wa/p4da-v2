/**
 * screen_editor.cpp — editor de texto com cursor real + leitura markdown.
 *
 * Herdado da v1 (M2 + M4.6/M4.7/M4.8/M4.10/M4.13):
 *  - vista VIRTUALIZADA: C++ dono da posição; UI recebe fatia visível +
 *    estilos por linha (arquivos grandes não instanciam milhares de
 *    elementos — limitação do Flickable resolvida no M4.6);
 *  - cursor ocupa o SLOT do caractere (M4.7: "exe_plo", não "exe_mplo");
 *    bar/under = glifo in-flow, block = espaço + overlay Rectangle;
 *  - UTF-8 por codepoints (setas/backspace nunca partem acentos);
 *  - markdown de bloco+inline via md_render.h (o MESMO parser que o
 *    harness offscreen renderiza — evidência sem hardware);
 *  - OSK 4 fileiras abc/123 + shift + setas/del, e o MESMO handler de
 *    tecla edita o campo do prompt do gerenciador quando ele está aberto
 *    (M4.8 — único cruzamento de estado entre módulos, ver screens.h);
 *  - toque posiciona linha/coluna direto do ponto (fonte mono:
 *    ED_LINE_H/ED_CHAR_W são o contrato de métrica com o theme).
 */

#include "screens.h"

#include "md_render.h"

#include "pda_config.h"
#include "storage_init.h"
#include "usb_hid_keyboard.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <utility>
#include <vector>

static const char *TAG = "editor";

/* ---- métricas espelhadas do theme.slint (M4.7) ------------------------
 * line-h 28px: um Text com height menor que a line-box da fonte renderiza
 * ZERO linhas (bug do título h1 invisível, 23px em linha de 25px). */
static const float ED_LINE_H = 28.f;
static const float ED_OSK_H = 193.f;
static const float ED_STATUS_H = 34.f;
static const float ED_BTN_SM = 39.f;
/* avanço de glifo da fonte mono 16px (medido no harness: fontpitch) */
static const float ED_CHAR_W = 11.0f;

/* ---- estado dono deste módulo ------------------------------------------ */
EditorState g_ed;

/* ---- vista virtualizada do editor (M4.6) ----
 * Modos: edição (linhas cruas + cursor) e leitura (markdown renderizado). */
static std::vector<std::string> g_ed_disp;    /* modo edição (cursor embutido) */
static std::vector<std::string> g_md_text;    /* modo leitura */
static std::vector<int> g_md_style;
static std::shared_ptr<slint::VectorModel<slint::SharedString>> g_ed_model;
static std::shared_ptr<slint::VectorModel<int>> g_ed_style_model;
/* runs de markdown inline por linha da fatia visível (modelo de modelos) */
static std::shared_ptr<slint::VectorModel<std::shared_ptr<slint::Model<MdRun>>>> g_ed_runs_model;
static float s_ed_top = 0.f;
static float s_ed_left = 0.f;
static bool s_cursor_on = true;
static int s_cur_line = 0;   /* posição do cursor p/ overlay do block (M4.7) */
static int s_cur_col = 0;    /* coluna em BYTES (scroll horizontal) */
static int s_cur_cell = 0;   /* coluna em CÉLULAS/codepoints (overlay .slint) */
static esp_timer_handle_t s_blink_t = nullptr;   /* criado em editor_wire */

std::string ed_title(void)
{
    if (g_ed.path.empty()) return std::string("(novo)");
    std::string root = pda_root();
    if (g_ed.path.rfind(root + "/", 0) == 0) return g_ed.path.substr(root.size() + 1);
    return g_ed.path;
}

static bool ed_osk_should_show(void)
{
    if (g_ed.reading) return false;
    if (g_ed.osk_override >= 0) return g_ed.osk_override == 1;
    return pda_settings()->onscreen_keyboard_auto && !usb_hid_keyboard_connected();
}

/* Rearma a fase do piscar: o cursor acende já e a próxima alternância só
 * vem um ciclo inteiro depois. Chamado a cada atividade de cursor (digita,
 * seta, toque) para ele não sumir logo após um toque de tecla. */
static void cursor_rearm(void)
{
    s_cursor_on = true;
    if (s_blink_t != nullptr) {
        esp_timer_stop(s_blink_t);   /* não rodando -> retorna erro, inofensivo */
        esp_timer_start_periodic(s_blink_t, 530 * 1000);
    }
}

/* 0=bar 1=under 2=block (M4.7: block é overlay Rectangle no .slint —
 * "█" não existe em nenhuma fonte embutida). */
static int cursor_kind(void)
{
    const char *st = pda_settings()->cursor_style;
    if (!strcmp(st, "under")) return 1;
    if (!strcmp(st, "block")) return 2;
    return 0;
}

static const char *cursor_glyph(void)
{
    return cursor_kind() == 1 ? "_" : "|";
}

/* Markdown de bloco+inline (M4.10): o parser mora em md_render.h e é o
 * MESMO que o harness offscreen renderiza (evidência sem hardware).
 * Estilos: 0=mono edição 1=h1 2=h2 8=h3 3=bullet 4=citação 5=código
 * 6=hr (SÓ "---"/"***") 7=parágrafo 9=linha em branco (sem régua).
 * Tolerante: aceita "#" sem espaço e até 3 níveis; BOM/CRLF saneados no
 * ed_load. Inline: **negrito**, __negrito__ e `código` (runs por linha). */
static std::vector<std::vector<MdRunLite>> g_md_runs;

static void md_render_doc(const std::string &text)
{
    std::vector<MdLineOut> doc;
    md_render(text, doc);
    g_md_text.clear(); g_md_style.clear(); g_md_runs.clear();
    g_md_text.reserve(doc.size());
    g_md_style.reserve(doc.size());
    g_md_runs.reserve(doc.size());
    for (auto &l : doc) {
        g_md_text.push_back(l.text);
        g_md_style.push_back(l.sty);
        g_md_runs.push_back(std::move(l.runs));
    }
}

static int ed_area_h(void)
{
    return 480 - (int)ED_STATUS_H - ((int)ED_BTN_SM + 14) - 16 -
           (ed_osk_should_show() ? (int)ED_OSK_H + 6 : 0);
}

static int ed_area_w(void) { return 800 - 2 * 9 - 2; }

static void ed_refresh_view(void)
{
    const std::vector<std::string> &src = g_ed.reading ? g_md_text : g_ed_disp;
    const std::vector<int> &srcs = g_md_style;

    float content_h = (float)src.size() * ED_LINE_H;
    float maxtop = content_h - (float)ed_area_h();
    if (maxtop < 0) maxtop = 0;
    if (s_ed_top < 0) s_ed_top = 0;
    if (s_ed_top > maxtop) s_ed_top = maxtop;
    float maxleft = g_ui->get_ed_content_w() - (float)ed_area_w();
    if (maxleft < 0) maxleft = 0;
    if (s_ed_left < 0) s_ed_left = 0;
    if (s_ed_left > maxleft) s_ed_left = maxleft;

    int start = (int)(s_ed_top / ED_LINE_H);
    int rows = ed_area_h() / (int)ED_LINE_H + 2;
    if (!g_ed_model) {
        g_ed_model = std::make_shared<slint::VectorModel<slint::SharedString>>();
        g_ui->set_ed_lines(g_ed_model);
    }
    if (!g_ed_style_model) {
        g_ed_style_model = std::make_shared<slint::VectorModel<int>>();
        g_ui->set_ed_line_style(g_ed_style_model);
    }
    if (!g_ed_runs_model) {
        g_ed_runs_model =
            std::make_shared<slint::VectorModel<std::shared_ptr<slint::Model<MdRun>>>>();
        g_ui->set_ed_runs(g_ed_runs_model);
    }
    std::vector<slint::SharedString> tv;
    std::vector<int> sv;
    std::vector<std::shared_ptr<slint::Model<MdRun>>> rv;
    for (int i = start; i < (int)src.size() && i < start + rows; i++) {
        tv.push_back(slint::SharedString(src[i]));
        sv.push_back(g_ed.reading ? srcs[i] : 0);
        auto rm = std::make_shared<slint::VectorModel<MdRun>>();
        if (g_ed.reading && i < (int)g_md_runs.size()) {
            for (const auto &r : g_md_runs[i]) {
                MdRun m;
                m.text = slint::SharedString(r.text);
                m.col = r.col;
                m.kind = r.kind;
                rm->push_back(m);
            }
        }
        rv.push_back(rm);
    }
    if (tv.empty()) { tv.push_back(slint::SharedString("")); sv.push_back(0); }
    size_t nslice = tv.size();
    g_ed_model->set_vector(std::move(tv));
    g_ed_style_model->set_vector(std::move(sv));
    g_ed_runs_model->set_vector(std::move(rv));
    g_ui->set_ed_offset(s_ed_top - (float)start * ED_LINE_H);
    g_ui->set_ed_offset_x(s_ed_left);
    /* overlay do cursor-bloco (M4.7): linha dentro da fatia + coluna */
    int crow = -1;
    if (!g_ed.reading && cursor_kind() == 2 && s_cursor_on) {
        int r = s_cur_line - start;
        if (r >= 0 && r < (int)nslice) crow = r;
    }
    g_ui->set_ed_cursor_row(crow);
    g_ui->set_ed_cursor_col(s_cur_cell);
    g_ui->set_ed_cursor_on(s_cursor_on);
    g_ui->set_ed_cursor_kind(cursor_kind());
}

static void ed_ensure_cursor_visible(int cursor_line, int cursor_col)
{
    float cy = (float)cursor_line * ED_LINE_H;
    if (cy < s_ed_top) s_ed_top = cy;
    else if (cy + ED_LINE_H > s_ed_top + (float)ed_area_h())
        s_ed_top = cy + ED_LINE_H - (float)ed_area_h();
    float cx = (float)cursor_col * ED_CHAR_W + 6;
    if (cx < s_ed_left) s_ed_left = cx;
    else if (cx + ED_CHAR_W > s_ed_left + (float)ed_area_w())
        s_ed_left = cx + ED_CHAR_W - (float)ed_area_w();
}

void ed_push_ui(bool scroll_to_cursor)
{
    /* atividade de cursor -> pisca recomeça com ele aceso */
    if (scroll_to_cursor && !g_ed.reading) cursor_rearm();

    std::vector<std::string> lines;
    int cursor_line = 0, cursor_col = 0;
    {
        size_t i = 0, line_idx = 0, line_start = 0;
        for (; i <= g_ed.text.size(); i++) {
            if (i == g_ed.text.size() || g_ed.text[i] == '\n') {
                lines.push_back(g_ed.text.substr(line_start, i - line_start));
                if (g_ed.cursor >= line_start && g_ed.cursor <= i) {
                    cursor_line = (int)line_idx;
                    cursor_col = (int)(g_ed.cursor - line_start);
                }
                line_idx++;
                line_start = i + 1;
            }
        }
    }
    if (lines.empty()) lines.push_back("");

    /* modo edição: cursor visível (pisca via s_cursor_on).
     * M4.7: o cursor OCUPA O SLOT do caractere em vez de inserir uma
     * célula extra — "exemplo" com cursor na col 3 vira "exe_plo", não
     * "exe_mplo". O caractere sob o cursor some enquanto ele está aceso
     * (bar/under: glifo no lugar; block: espaço + Rectangle no .slint).
     * No fim da linha o glifo é acrescentado após o último char. */
    g_ed_disp = lines;
    if (!g_ed.reading) {
        if ((size_t)cursor_col > g_ed_disp[cursor_line].size())
            cursor_col = (int)g_ed_disp[cursor_line].size();
        s_cur_line = cursor_line;
        s_cur_col = cursor_col;
        s_cur_cell = (int)utf8_count(lines[cursor_line], (size_t)cursor_col);
        if (s_cursor_on) {
            std::string &L = g_ed_disp[cursor_line];
            size_t cc = (size_t)cursor_col;
            size_t nx = (cc < L.size()) ? utf8_next(L, cc) : cc;
            if (cursor_kind() == 2) {
                L.replace(cc, nx - cc, " ");
            } else if (cc < L.size()) {
                L.replace(cc, nx - cc, cursor_glyph());
            } else {
                L += cursor_glyph();
            }
        }
    } else {
        s_cur_line = cursor_line;
        s_cur_col = cursor_col;
        s_cur_cell = (int)utf8_count(lines[cursor_line], (size_t)cursor_col);
    }
    md_render_doc(g_ed.text);

    size_t maxw = 0;
    const std::vector<std::string> &wsrc = g_ed.reading ? g_md_text : g_ed_disp;
    for (auto &d : wsrc) if (d.size() > maxw) maxw = d.size();

    static float s_last_w = -1.f;
    static std::string s_last_title;
    static int s_last_flags = -1;
    float want_w = (float)(maxw * ED_CHAR_W + 60);
    if (fabsf(want_w - s_last_w) > 0.5f) { g_ui->set_ed_content_w(want_w); s_last_w = want_w; }
    std::string title = ed_title();
    if (title != s_last_title) { g_ui->set_ed_title(slint::SharedString(title)); s_last_title = title; }
    bool can_del = g_ed.path.rfind(notes_dir() + "/", 0) == 0;
    bool osk = ed_osk_should_show();
    int flags = (g_ed.dirty ? 1 : 0) | (can_del ? 2 : 0) | (osk ? 4 : 0) | (g_ed.reading ? 8 : 0);
    if (flags != s_last_flags) {
        g_ui->set_ed_dirty(g_ed.dirty);
        g_ui->set_ed_can_delete(can_del);
        g_ui->set_ed_show_osk(osk);
        g_ui->set_ed_reading(g_ed.reading);
        s_last_flags = flags;
    }

    if (scroll_to_cursor && !g_ed.reading) ed_ensure_cursor_visible(cursor_line, cursor_col);
    ed_refresh_view();
}

void ed_insert_str(const char *s, size_t n)
{
    if (g_ed.cursor > g_ed.text.size()) g_ed.cursor = g_ed.text.size();
    g_ed.text.insert(g_ed.cursor, s, n);
    g_ed.cursor += n;
    g_ed.dirty = true;
    ed_push_ui(true);
}

void ed_backspace(void)
{
    if (g_ed.cursor == 0 || g_ed.text.empty()) return;
    size_t prev = utf8_prev(g_ed.text, g_ed.cursor);
    g_ed.text.erase(prev, g_ed.cursor - prev);
    g_ed.cursor = prev;
    g_ed.dirty = true;
    ed_push_ui(true);
}

void ed_delete_key(void)
{
    if (g_ed.cursor >= g_ed.text.size()) return;
    size_t next = utf8_next(g_ed.text, g_ed.cursor);
    g_ed.text.erase(g_ed.cursor, next - g_ed.cursor);
    g_ed.dirty = true;
    ed_push_ui(true);
}

void ed_move(int dcol, int dline, bool home, bool end)
{
    /* reconstrói linha/coluna atuais */
    size_t line_start = 0;
    int line_idx = 0;
    for (size_t i = 0; i < g_ed.cursor; i++) {
        if (g_ed.text[i] == '\n') { line_start = i + 1; line_idx++; }
    }
    int col = (int)(g_ed.cursor - line_start);

    if (home) { g_ed.cursor = line_start; ed_push_ui(true); return; }
    if (end) {
        size_t i = line_start;
        while (i < g_ed.text.size() && g_ed.text[i] != '\n') i++;
        g_ed.cursor = i;
        ed_push_ui(true);
        return;
    }
    if (dline != 0) {
        /* acha a linha alvo e clamp a coluna nela */
        int target = line_idx + dline;
        std::vector<std::pair<size_t, size_t>> spans;   /* start, end(incl newline pos) */
        size_t s0 = 0;
        for (size_t i = 0; i <= g_ed.text.size(); i++) {
            if (i == g_ed.text.size() || g_ed.text[i] == '\n') {
                spans.push_back({ s0, i });
                s0 = i + 1;
            }
        }
        if (spans.empty()) spans.push_back({ 0, 0 });
        if (target < 0) target = 0;
        if (target >= (int)spans.size()) target = (int)spans.size() - 1;
        size_t len = spans[target].second - spans[target].first;
        size_t c = (size_t)col > len ? len : (size_t)col;
        g_ed.cursor = utf8_snap(g_ed.text, spans[target].first + c);
        ed_push_ui(true);
        return;
    }
    if (dcol > 0) {
        g_ed.cursor = utf8_next(g_ed.text, g_ed.cursor);
    } else if (dcol < 0) {
        g_ed.cursor = utf8_prev(g_ed.text, g_ed.cursor);
    }
    ed_push_ui(true);
}

void ed_load(const std::string &path, const std::string &content_in,
             bool is_new, bool start_reading)
{
    /* M4.7: saneamento de texto vindo do cartão/USB: BOM UTF-8 na 1ª linha
     * cegava o md_render ("# título" virava parágrafo cru) e \r de CRLF
     * vazava para as linhas do .slint. */
    std::string content = content_in;
    if (content.size() >= 3 && (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF)
        content.erase(0, 3);
    if (content.find('\r') != std::string::npos) {
        std::string norm;
        norm.reserve(content.size());
        for (size_t i = 0; i < content.size(); i++) {
            if (content[i] == '\r') {
                if (i + 1 < content.size() && content[i + 1] == '\n') continue;
                norm.push_back('\n');
            } else {
                norm.push_back(content[i]);
            }
        }
        content.swap(norm);
    }
    g_ed.path = path;
    g_ed.text = content;
    g_ed.cursor = 0;
    g_ed.dirty = false;
    g_ed.is_new = is_new;
    g_ed.reading = start_reading;
    g_ed.osk_override = -1;
    s_ed_top = 0.f;
    s_ed_left = 0.f;
    ed_push_ui(false);
    nav_goto(AppState::Editor);
    session_set_app("noteedit");
    session_set_note(is_new ? std::string("") : ed_title());
}

void ed_open_path_async(const std::string path, bool start_reading)
{
    spawn_thread("io_edit", 8192, [path, start_reading]() {
        char *data = NULL; size_t len = 0;
        std::string content;
        if (storage_read_file_alloc(path.c_str(), &data, &len) == ESP_OK) {
            content.assign(data, len);
            free(data);
        } else {
            char buf[256];
            snprintf(buf, sizeof(buf), "[erro] não abriu %s", path.c_str());
            log_line(buf, NULL);
            return;
        }
        ui_post([path, content, start_reading]() {
            ed_load(path, content, false, start_reading);
        });
    });
}

static void ed_save(void)
{
    if (g_ed.path.empty()) return;
    if (storage_write_text_file(g_ed.path.c_str(), g_ed.text.data(), g_ed.text.size()) == ESP_OK) {
        g_ed.dirty = false;
        g_ed.is_new = false;
        /* Se o usuário editou o próprio system.lua, o buffer dele é a nova
         * fonte da verdade: recarrega em memória (com sanitize) para o
         * firmware não voltar a escrever valores velhos por cima
         * (loop de "sobrescrita no boot" de 2026-09-27). */
        if (g_ed.path.find("/config/system.lua") != std::string::npos) {
            pda_config_reload();
        }
        ESP_LOGI(TAG, "salvo: %s (%u bytes)", g_ed.path.c_str(), (unsigned)g_ed.text.size());
        ed_push_ui(false);
        if (g_ed.path.rfind(notes_dir() + "/", 0) == 0) refresh_notes_list();
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf), "[erro] falha ao salvar %s", g_ed.path.c_str());
        log_line(buf, NULL);
    }
}

/* ---------------- teclado virtual: modelos de linhas -------------- */
/* [modo][shift][fileira] — modo 0 = abc, modo 1 = 123/símbolos.
 * As camadas de símbolos espelham Lower/Raise do teclado USB
 * (reference/.../keyboard/key_mapping.md). */
static const char *s_osk_rows[2][2][3] = {
    {   /* modo abc */
        { "q w e r t y u i o p", "a s d f g h j k l ç", "z x c v b n m , . ;" },
        { "Q W E R T Y U I O P", "A S D F G H J K L Ç", "Z X C V B N M , . ;" },
    },
    {   /* modo 123/símbolos */
        { "1 2 3 4 5 6 7 8 9 0", "! @ # $ % ^ & * ( )", "` - = [ ] \\ _ + { } |" },
        { "` ~ € £ ¥ ° ¶ • ª º", "< > ? / : ; \" ' ´ ¨", "+ - × ÷ = ≠ ≈ ∞ § ¤" },
    },
};

void ed_push_osk_rows(void)
{
    const char **src = s_osk_rows[g_ed.osk_mode ? 1 : 0][g_ed.osk_shift ? 1 : 0];
    g_ui->set_ed_osk_mode(g_ed.osk_mode);
    for (int r = 0; r < 3; r++) {
        auto model = std::make_shared<slint::VectorModel<slint::SharedString>>();
        const char *p = src[r];
        std::string cur;
        /* separa por espaço; cada "tecla" pode ter 1 char ou multibyte (ç) */
        size_t i = 0;
        std::string row(p);
        cur.clear();
        while (i <= row.size()) {
            if (i == row.size() || row[i] == ' ') {
                if (!cur.empty()) { model->push_back(slint::SharedString(cur)); cur.clear(); }
                i++;
            } else {
                /* copia um codepoint UTF-8 inteiro */
                unsigned char c = (unsigned char)row[i];
                int extra = (c < 0x80) ? 0 : (c < 0xE0) ? 1 : (c < 0xF0) ? 2 : 3;
                cur.push_back(row[i]);
                for (int k = 0; k < extra && i + 1 < row.size(); k++) { i++; cur.push_back(row[i]); }
                i++;
            }
        }
        if (r == 0) g_ui->set_ed_osk_r1(model);
        else if (r == 1) g_ui->set_ed_osk_r2(model);
        else g_ui->set_ed_osk_r3(model);
    }
    g_ui->set_ed_osk_shift(g_ed.osk_shift);
}

static void ed_osk_key(const std::string k)
{
    if (k == "BACKSPACE") { ed_backspace(); return; }
    if (k == "DEL") { ed_delete_key(); return; }
    if (k == "ENTER") { ed_insert_str("\n", 1); return; }
    if (k == "SPACE") { ed_insert_str(" ", 1); return; }
    if (k == "LEFT")  { ed_move(-1, 0, false, false); return; }
    if (k == "RIGHT") { ed_move(1, 0, false, false); return; }
    if (k == "UP")    { ed_move(0, -1, false, false); return; }
    if (k == "DOWN")  { ed_move(0, 1, false, false); return; }
    if (k == "HOME")  { ed_move(0, 0, true, false); return; }
    if (k == "END")   { ed_move(0, 0, false, true); return; }
    if (k == "HIDE")  { g_ed.osk_override = 0; ed_push_ui(false); return; }
    if (k == "MODE")  { g_ed.osk_mode = !g_ed.osk_mode; ed_push_osk_rows(); return; }
    ed_insert_str(k.data(), k.size());
}

/* ---- wiring dos callbacks Slint ---------------------------------------- */
void editor_wire(AppWindow &ui)
{
    /* ---------- cursor piscante (~530 ms) ----------
     * Callback do esp_timer roda em task de sistema: só agenda o toggle no
     * loop de eventos do Slint. Alterna apenas com o Editor ativo e fora do
     * modo leitura; a fase é rearmada por cursor_rearm() (via ed_push_ui).
     * (v1: criado no app_main; v2: o módulo é dono do próprio timer.) */
    {
        esp_timer_create_args_t bargs = {};
        bargs.callback = [](void *arg) {
            (void)arg;
            ui_post([]() {
                if (g_ui && g_ui->get_active_app() == AppState::Editor &&
                    !g_ed.reading) {
                    s_cursor_on = !s_cursor_on;
                    ed_push_ui(false);
                }
            });
        };
        bargs.arg = nullptr;
        bargs.name = "cursor_blink";
        if (esp_timer_create(&bargs, &s_blink_t) == ESP_OK) {
            esp_timer_start_periodic(s_blink_t, 530 * 1000);
        } else {
            s_blink_t = nullptr;
        }
    }

    ui.on_ed_drag([](float dx, float dy) {
        activity();
        s_ed_left -= dx;
        s_ed_top -= dy;
        ed_refresh_view();
    });
    ui.on_ed_tap_at([](float x, float y) {
        activity();
        if (g_ed.reading) return;   /* leitura: toque só rola */
        /* fonte mono: linha e coluna direto do ponto tocado */
        int line = (int)((s_ed_top + y) / ED_LINE_H);
        int col = (int)((s_ed_left + x - 6) / ED_CHAR_W);
        if (line < 0) line = 0;
        if (col < 0) col = 0;
        size_t line_start = 0;
        int idx = 0;
        for (size_t i = 0; i <= g_ed.text.size(); i++) {
            if (i == g_ed.text.size() || g_ed.text[i] == '\n') {
                if (idx == line) {
                    size_t len = i - line_start;
                    size_t c = (size_t)col > len ? len : (size_t)col;
                    g_ed.cursor = utf8_snap(g_ed.text, line_start + c);
                    break;
                }
                idx++;
                line_start = i + 1;
            }
        }
        ed_push_ui(false);
    });
    ui.on_ed_osk_key([](slint::SharedString k) {
        activity();
        std::string key(k.data());
        if (g_ed.reading) return;   /* leitura: teclado não edita */
        if (g_fm.prompt && g_fm.prompt_needs_text) {
            /* edição do campo com cursor real (M4.8): antes as teclas
             * simbólicas caíam no else e eram anexadas LITERAIS
             * ("SPACE" no lugar de espaço) e as setas não faziam nada. */
            std::string &t = g_fm.prompt_text;
            if (g_fm.prompt_pos > t.size()) g_fm.prompt_pos = t.size();
            size_t &p = g_fm.prompt_pos;
            if (key == "BACKSPACE") {
                if (p > 0) { size_t q = utf8_prev(t, p); t.erase(q, p - q); p = q; }
            } else if (key == "DEL") {
                if (p < t.size()) { size_t q = utf8_next(t, p); t.erase(p, q - p); }
            } else if (key == "LEFT") {
                p = utf8_prev(t, p);
            } else if (key == "RIGHT") {
                p = utf8_next(t, p);
            } else if (key == "HOME") {
                p = 0;
            } else if (key == "END") {
                p = t.size();
            } else if (key == "ENTER") {
                g_ui->invoke_fm_prompt_ok();
                return;
            } else if (key == "MODE") {
                g_ed.osk_mode = !g_ed.osk_mode;
                ed_push_osk_rows();
                return;
            } else if (key == "HIDE" || key == "UP" || key == "DOWN") {
                return;
            } else if (key == "SPACE") {
                t.insert(p, " ");
                p += 1;
            } else {
                t.insert(p, key);
                p += key.size();
            }
            push_fm_ui();
            return;
        }
        ed_osk_key(key);
    });
    ui.on_ed_osk_shift_set([](bool s) {
        activity();
        g_ed.osk_shift = s;
        ed_push_osk_rows();
    });
    ui.on_ed_toggle_mode([]() {
        activity();
        g_ed.reading = !g_ed.reading;
        ed_push_ui(true);   /* volta p/ edição: rearma o piscar e mostra o cursor */
    });
    ui.on_ed_toggle_osk([]() {
        activity();
        g_ed.osk_override = ed_osk_should_show() ? 0 : 1;
        ed_push_ui(false);
    });
    ui.on_ed_save([]() { activity(); ed_save(); });
    ui.on_ed_delete([]() {
        activity();
        if (g_ed.path.rfind(notes_dir() + "/", 0) != 0) return;  /* segurança */
        storage_delete_file(g_ed.path.c_str());
        g_ed.path.clear();
        g_ui->set_active_app(AppState::NotesList);
        refresh_notes_list();
    });
}
