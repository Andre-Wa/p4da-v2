/**
 * screen_music.cpp — tela Música (camada L3): WAVs do cartão no falante
 * local e/ou USB, conforme a rota (SOM.2).
 *
 * Herdado da v1 (M5a/M5a.2.x): lista de .wav em music/, tap = fresh,
 * pause/resume com posição, próxima, refresh, linha dev/state/track.
 * Novo no v2: dois backends (audio_i2s local SOM.1 + audio_uac) atrás
 * da rota auto/interno/USB (RAM-only, botões na tela) e media keys do
 * HID despachadas aqui (music_on_media_key).
 *
 * A linha "Saída" mostra a rota e o device UAC; "Estado"/"Faixa" vêm do
 * backend DONO da faixa atual (não da rota resolvida agora — unplug no
 * meio da faixa não muda o que está tocando).
 */

#include "screens.h"

#include "audio_i2s.h"
#include "audio_route.h"
#include "audio_uac.h"
#include "storage_init.h"
#include "usb_hid_keyboard.h"

#include "esp_log.h"

#include <cstdio>
#include <dirent.h>
#include <strings.h>
#include <algorithm>

static const char *TAG = "music";

static std::vector<std::string> g_mus_names;
static int g_mus_cur = -1;

/* backend dono do momento: quem toca (ou, ocioso, a rota resolvida) */
static bool moment_uac(void)
{
    if (audio_uac_playing()) return true;
    if (audio_i2s_playing()) return false;
    return audio_route_use_uac();
}

void push_mus_ui(void)
{
    if (!g_ui) return;
    auto m = std::make_shared<slint::VectorModel<slint::SharedString>>();
    for (auto &n : g_mus_names) m->push_back(slint::SharedString(n));
    g_ui->set_mus_tracks(m);

    char dev[112];
    if (audio_route_use_uac())
        snprintf(dev, sizeof(dev), "%s — %s", audio_route_label(),
                 audio_uac_present() ? audio_uac_dev_name() : "(sem device UAC)");
    else
        snprintf(dev, sizeof(dev), "%s — falante ES8311", audio_route_label());
    g_ui->set_mus_dev(slint::SharedString(dev));

    const bool uac = moment_uac();
    g_ui->set_mus_state(slint::SharedString(uac ? audio_uac_state()
                                                : audio_i2s_state()));
    g_ui->set_mus_track(slint::SharedString(uac ? audio_uac_track()
                                                : audio_i2s_track()));
    g_ui->set_mus_route(slint::SharedString(audio_route_label()));
}

void refresh_music_list(void)
{
    spawn_thread("io_music_ls", 8192, []() {
        std::vector<std::string> out;
        std::string dir = std::string(pda_root()) + "/music";
        DIR *d = opendir(dir.c_str());
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                std::string n = e->d_name;
                if (n.size() > 4 && !strcasecmp(n.c_str() + n.size() - 4, ".wav"))
                    out.push_back(n);
            }
            closedir(d);
        }
        std::sort(out.begin(), out.end());
        ui_post([out]() {
            g_mus_names = out;
            push_mus_ui();
        });
    });
}

static void music_play_idx(int i, bool fresh)
{
    if (i < 0 || (size_t)i >= g_mus_names.size()) return;
    g_mus_cur = i;
    std::string full = std::string(pda_root()) + "/music/" + g_mus_names[i];
    /* M5a.2.4 (v1): fresh = tap na lista/navegação (do zero); fresh=false
     * = play/pause do fone com player parado (retoma ponto de unplug). */
    spawn_thread("io_music", 8192, [full, fresh]() {
        if (audio_route_use_uac()) {
            if (fresh) audio_uac_play_fresh(full.c_str());
            else audio_uac_play(full.c_str());
        } else {
            audio_i2s_play(full.c_str());   /* local: sempre do zero */
        }
    });
}

void music_open(void)
{
    refresh_music_list();
    nav_goto(AppState::Music);
}

/* ---- media keys do consumer control (H3S/teclados c/ mídia) ----
 * v1: chaveava só UAC; v2 atua no backend que toca (ou na rota, ocioso). */
void music_on_media_key(int act)
{
    switch (act) {
    case USB_MEDIA_PLAYPAUSE:
        if (audio_uac_playing()) {
            if (audio_uac_paused()) audio_uac_resume(); else audio_uac_pause();
        } else if (audio_i2s_playing()) {
            if (audio_i2s_paused()) audio_i2s_resume(); else audio_i2s_pause();
        } else if (!g_mus_names.empty()) {
            music_play_idx(g_mus_cur >= 0 ? g_mus_cur : 0, false);
        }
        break;
    case USB_MEDIA_NEXT:
        if (!g_mus_names.empty())
            music_play_idx((g_mus_cur + 1) % (int)g_mus_names.size(), true);
        break;
    case USB_MEDIA_PREV:
        if (!g_mus_names.empty())
            music_play_idx((g_mus_cur - 1 + (int)g_mus_names.size()) %
                           (int)g_mus_names.size(), true);
        break;
    case USB_MEDIA_STOP:
        audio_uac_stop();
        audio_i2s_stop();
        break;
    case USB_MEDIA_VOL_UP:
        if (moment_uac()) audio_uac_volume_step(+10);
        else audio_i2s_volume_step(+10);
        break;
    case USB_MEDIA_VOL_DOWN:
        if (moment_uac()) audio_uac_volume_step(-10);
        else audio_i2s_volume_step(-10);
        break;
    case USB_MEDIA_MUTE:
        if (moment_uac()) audio_uac_mute_toggle();
        else audio_i2s_mute_toggle();
        break;
    default:
        ESP_LOGI(TAG, "mídia: usage desconhecido");
        break;
    }
    push_mus_ui();
}

void music_wire(AppWindow &ui)
{
    ui.on_mus_play([](int i) { activity(); music_play_idx(i, true); push_mus_ui(); });
    ui.on_mus_stop([]() {
        activity();
        audio_uac_stop();
        audio_i2s_stop();
        push_mus_ui();
    });
    ui.on_mus_next([]() {
        activity();
        if (!g_mus_names.empty())
            music_play_idx((g_mus_cur + 1) % (int)g_mus_names.size(), true);
        push_mus_ui();
    });
    ui.on_mus_refresh([]() { activity(); refresh_music_list(); });
    ui.on_mus_pause([]() {
        activity();
        if (moment_uac()) {
            if (audio_uac_paused()) audio_uac_resume(); else audio_uac_pause();
        } else {
            if (audio_i2s_paused()) audio_i2s_resume(); else audio_i2s_pause();
        }
        push_mus_ui();
    });
    ui.on_mus_set_route([](int r) {
        activity();
        audio_route_set((audio_route_t)r);
        ESP_LOGI(TAG, "rota de saída: %s", audio_route_label());
        push_mus_ui();
    });
}
