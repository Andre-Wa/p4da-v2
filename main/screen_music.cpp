/**
 * screen_music.cpp — tela Música (camada L3 + SOM.4 da v2.3.0).
 *
 * v2.3.0: três pedidos de campo entram aqui:
 *  - **outros formatos**: a lista aceita .wav E .mp3 (o decode é do
 *    pda_audio/pcm_src — minimp3 vendorizado; WAV 36 MB vs MP3 7 MB);
 *  - **playlists**: .m3u/.m3u8 em music/ viram fila de reprodução
 *    (audio_playlist); tap numa faixa comum monta a fila na ordem da
 *    lista (Próxima/media-key NEXT navegam nela);
 *  - **metadados**: linha "Faixa" mostra o título do tag (ID3v2/LIST
 *    INFO) quando existe; linha nova de artista/álbum (mus-meta).
 * Herdado: rota SOM.2 (auto/falante/USB), estado do backend DONO da
 * faixa, media keys, refresh async.
 */

#include "screens.h"

#include "audio_decode.h"
#include "audio_i2s.h"
#include "audio_playlist.h"
#include "audio_route.h"
#include "audio_vol.h"
#include "audio_tags.h"
#include "audio_uac.h"
#include "storage_init.h"
#include "usb_hid_keyboard.h"

#include "esp_log.h"

#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <strings.h>
#include <vector>

static const char *TAG = "music";

static std::vector<std::string> g_mus_names;    /* display (lista) */
static std::vector<std::string> g_mus_paths;    /* caminhos (mesmo índice) */
static std::vector<std::string> g_queue;        /* fila de reprodução */
static int g_queue_pos = -1;
static int g_mus_cur = -1;                      /* índice tocado na lista */

/* backend dono do momento: quem toca (ou, ocioso, a rota resolvida) */
static bool moment_uac(void)
{
    if (audio_uac_playing()) return true;
    if (audio_i2s_playing()) return false;
    return audio_route_use_uac();
}

static std::string current_path(void)
{
    if (g_queue_pos >= 0 && g_queue_pos < (int)g_queue.size())
        return g_queue[g_queue_pos];
    return std::string();
}

static std::string display_for(const std::string &path)
{
    audio_tags_t t;
    audio_tags_read(path.c_str(), &t);
    if (t.title[0]) return std::string(t.title);
    return base_name(path);
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

    const bool u = moment_uac();
    g_ui->set_mus_state(slint::SharedString(u ? audio_uac_state()
                                              : audio_i2s_state()));
    /* Faixa: título do tag do item em reprodução (fallback: o que o
     * backend reporta, que é o basename) */
    std::string cp = current_path();
    std::string track;
    char meta[160] = "";
    if (!cp.empty()) {
        track = display_for(cp);
        audio_tags_t t;
        audio_tags_read(cp.c_str(), &t);
        if (t.artist[0] || t.album[0])
            snprintf(meta, sizeof(meta), "%s%s%s", t.artist,
                     (t.artist[0] && t.album[0]) ? " — " : "", t.album);
        if (!g_queue.empty())
            g_ui->set_mus_track(slint::SharedString(track));
    }
    if (cp.empty() || meta[0] == 0) {
        /* ocioso ou sem tags: espelho do backend */
        if (cp.empty())
            g_ui->set_mus_track(slint::SharedString(u ? audio_uac_track()
                                                      : audio_i2s_track()));
    }
    g_ui->set_mus_meta(slint::SharedString(meta));
    g_ui->set_mus_route(slint::SharedString(audio_route_label()));
    const bool uu = moment_uac();
    g_ui->set_mus_volume((float)audio_vol_get(uu ? AUDIO_VOL_UAC
                                                 : AUDIO_VOL_INTERNAL));
    g_ui->set_mus_vol_label(slint::SharedString(uu ? "USB" : "falante"));
}

static void collect_entry(const char *path, void *ctx)
{
    auto *v = static_cast<std::vector<std::string> *>(ctx);
    v->push_back(path);
}

void refresh_music_list(void)
{
    spawn_thread("io_music_ls", 8192, []() {
        std::vector<std::string> paths, disps;
        std::string dir = std::string(pda_root()) + "/music";
        DIR *d = opendir(dir.c_str());
        if (d) {
            std::vector<std::string> raw;
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                std::string n = e->d_name;
                if (audio_path_is_playable(n.c_str()) ||
                    audio_path_is_playlist(n.c_str()))
                    raw.push_back(n);
            }
            closedir(d);
            std::sort(raw.begin(), raw.end());
            for (auto &n : raw) {
                std::string full = dir + "/" + n;
                paths.push_back(full);
                disps.push_back(audio_path_is_playlist(n.c_str())
                                    ? n
                                    : display_for(full));
            }
        }
        ui_post([paths, disps]() {
            g_mus_paths = paths;
            g_mus_names = disps;
            push_mus_ui();
        });
    });
}

static void play_path(const std::string &full, bool fresh)
{
    spawn_thread("io_music", 8192, [full, fresh]() {
        if (audio_route_use_uac()) {
            if (fresh) audio_uac_play_fresh(full.c_str());
            else audio_uac_play(full.c_str());
        } else {
            audio_i2s_play(full.c_str());   /* local: sempre do zero */
        }
    });
}

static void music_play_idx(int i, bool fresh)
{
    if (i < 0 || (size_t)i >= g_mus_paths.size()) return;
    g_mus_cur = i;
    const std::string p = g_mus_paths[i];
    if (audio_path_is_playlist(p.c_str())) {
        g_queue.clear();
        int skipped = 0;
        int n = audio_playlist_load(p.c_str(), collect_entry, &g_queue, &skipped);
        if (n <= 0) {
            char msg[220];
            snprintf(msg, sizeof(msg),
                     "[playlist] %s: nenhuma faixa válida (%d ignorada(s))",
                     base_name(p).c_str(), skipped);
            log_line(msg, NULL);
            g_queue_pos = -1;
            return;
        }
        g_queue_pos = 0;
        char msg[220];
        snprintf(msg, sizeof(msg), "[playlist] %s: %d faixa(s)%s",
                 base_name(p).c_str(), n,
                 skipped ? " (algumas ignoradas: arquivo saiu?)" : "");
        log_line(msg, NULL);
        play_path(g_queue[0], true);
        return;
    }
    /* faixa comum: fila = tocáveis da lista na ordem; pos = esta */
    g_queue.clear();
    g_queue_pos = -1;
    for (size_t k = 0; k < g_mus_paths.size(); k++) {
        if (audio_path_is_playlist(g_mus_paths[k].c_str())) continue;
        if ((int)k == i) g_queue_pos = (int)g_queue.size();
        g_queue.push_back(g_mus_paths[k]);
    }
    play_path(p, fresh);
}

static void queue_step(int dir)
{
    if (g_queue.empty()) return;
    g_queue_pos = (g_queue_pos + dir + (int)g_queue.size()) % (int)g_queue.size();
    play_path(g_queue[g_queue_pos], true);
}

void music_open(void)
{
    refresh_music_list();
    nav_goto(AppState::Music);
}

/* ---- media keys do consumer control (H3S/teclados c/ mídia) ---- */
void music_on_media_key(int act)
{
    switch (act) {
    case USB_MEDIA_PLAYPAUSE:
        if (audio_uac_playing()) {
            if (audio_uac_paused()) audio_uac_resume(); else audio_uac_pause();
        } else if (audio_i2s_playing()) {
            if (audio_i2s_paused()) audio_i2s_resume(); else audio_i2s_pause();
        } else if (!g_queue.empty() && g_queue_pos >= 0) {
            play_path(g_queue[g_queue_pos], false);
        } else if (!g_mus_paths.empty()) {
            music_play_idx(g_mus_cur >= 0 ? g_mus_cur : 0, false);
        }
        break;
    case USB_MEDIA_NEXT:
        queue_step(+1);
        break;
    case USB_MEDIA_PREV:
        queue_step(-1);
        break;
    case USB_MEDIA_STOP:
        audio_uac_stop();
        audio_i2s_stop();
        break;
    case USB_MEDIA_VOL_UP:
        audio_vol_step(moment_uac() ? AUDIO_VOL_UAC : AUDIO_VOL_INTERNAL, +10);
        break;
    case USB_MEDIA_VOL_DOWN:
        audio_vol_step(moment_uac() ? AUDIO_VOL_UAC : AUDIO_VOL_INTERNAL, -10);
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
        queue_step(+1);
        push_mus_ui();
    });
    ui.on_mus_refresh([]() { activity(); refresh_music_list(); });
    ui.on_mus_pause([]() {
        activity();
        const bool now_uac = audio_route_use_uac();
        const bool uac_on = audio_uac_playing() || audio_uac_paused();
        const bool loc_on = audio_i2s_playing() || audio_i2s_paused();
        if (now_uac) {
            if (uac_on) {
                if (audio_uac_paused()) audio_uac_resume(); else audio_uac_pause();
            } else {
                if (loc_on) audio_i2s_stop();
                if (!current_path().empty()) play_path(current_path(), true);
            }
        } else {
            if (loc_on) {
                if (audio_i2s_paused()) audio_i2s_resume(); else audio_i2s_pause();
            } else {
                if (uac_on) audio_uac_stop();
                if (!current_path().empty()) play_path(current_path(), true);
            }
        }
        push_mus_ui();
    });
    ui.on_mus_set_route([](int r) {
        activity();
        audio_route_set((audio_route_t)r);
        const bool now_uac = audio_route_use_uac();
        const bool uac_on = audio_uac_playing() || audio_uac_paused();
        const bool loc_on = audio_i2s_playing() || audio_i2s_paused();
        if (uac_on && !now_uac) audio_uac_stop();
        if (loc_on && now_uac) audio_i2s_stop();
        ESP_LOGI(TAG, "rota de saída: %s", audio_route_label());
        if ((uac_on || loc_on) && !current_path().empty())
            play_path(current_path(), true);   /* handover v2.2.1 */
        push_mus_ui();
    });
    ui.on_mus_set_volume([](float v) {
        activity();
        audio_vol_set(moment_uac() ? AUDIO_VOL_UAC : AUDIO_VOL_INTERNAL, (int)v);
    });
}
