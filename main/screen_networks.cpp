/**
 * screen_networks.cpp — tela Redes (camada L2): scan + conexão.
 *
 * Herdado da v1 (M4b + M5.2/M5.3): scan lista SSID+RSSI+cadeado ordenado
 * por sinal; toque em rede aberta conecta; rede fechada com senha salva
 * conecta DIRETO (sem prompt); sem senha salva abre o prompt global do
 * gerenciador (prompt_action "wifipass" — o MESMO overlay/OSK dos renames,
 * herança M4.8). Sair da tela sem escolher retoma o autoreconnect
 * (app_main, on_app_back).
 *
 * O scan pausa o autoreconnect durante a varredura (rádio ocupado longe
 * de casa) — e o rescan da tela é intervenção manual que reavalia o
 * manager (v5.15.0).
 */

#include "screens.h"

#include "wifi_net.h"

#include "esp_log.h"

#include <cstdio>
#include <string>
#include <vector>

static const char *TAG = "networks";

/* SSID da rede fechada escolhida na tela enquanto o prompt de senha está
 * aberto (o prompt em si é estado do gerenciador — g_fm). */
std::string g_wifi_pick_ssid;

static std::vector<wifi_net_ap_t> g_aps;

static void on_scan_done(const wifi_net_ap_t *aps, int count, void *ctx)
{
    (void)ctx;
    g_aps.clear();
    for (int i = 0; i < count; i++) g_aps.push_back(aps[i]);
    auto ssids = std::make_shared<slint::VectorModel<slint::SharedString>>();
    auto infos = std::make_shared<slint::VectorModel<slint::SharedString>>();
    auto locked = std::make_shared<slint::VectorModel<bool>>();
    for (int i = 0; i < count; i++) {
        ssids->push_back(slint::SharedString(aps[i].ssid));
        char b[24];
        snprintf(b, sizeof(b), "%d dBm", aps[i].rssi);
        infos->push_back(slint::SharedString(b));
        locked->push_back(!aps[i].open);
    }
    if (count == 0) {
        ssids->push_back(slint::SharedString("(nenhuma rede encontrada)"));
        infos->push_back(slint::SharedString(""));
        locked->push_back(false);
    }
    ui_post([ssids, infos, locked]() {
        g_ui->set_net_ssids(ssids);
        g_ui->set_net_infos(infos);
        g_ui->set_net_locked(locked);
        g_ui->set_net_current(slint::SharedString(
            wifi_net_connected() ? wifi_net_ssid() : "(offline)"));
    });
}

void net_start_scan(void)
{
    /* M5.3.2: rádio desligado → aviso em vez de scan que falha. */
    if (!wifi_net_enabled()) {
        log_line("[wifi] rádio desligado — ligue em Config > Redes", NULL);
        g_ui->set_net_current(slint::SharedString("(rádio desligado)"));
        return;
    }
    /* Longe da rede salva, o loop de reconexão a cada 2 s atrapalha o
     * scan (rádio ocupado); pausa durante a varredura da tela Redes. */
    wifi_net_set_autoreconnect(false);
    if (wifi_net_scan(on_scan_done, NULL) != ESP_OK) {
        log_line("[erro] scan não iniciado", NULL);
        wifi_net_set_autoreconnect(true);
    }
}

void networks_open(void)
{
    nav_goto(AppState::Networks);
    net_start_scan();
}

void networks_wire(AppWindow &ui)
{
    ui.on_net_rescan([]() {
        activity();
        net_start_scan();
    });
    ui.on_net_pick([](int i) {
        activity();
        wifi_net_set_autoreconnect(true);
        if (i < 0 || (size_t)i >= g_aps.size()) return;
        const wifi_net_ap_t &ap = g_aps[i];
        if (ap.open) {
            char msg[160];
            snprintf(msg, sizeof(msg), "[wifi] conectando em \"%s\" (aberta)", ap.ssid);
            log_line(msg, NULL);
            g_ui->set_cfg_wifi_on(true);   /* M5.3.2: switch reflete o rádio */
            wifi_net_connect(ap.ssid, "", true);
        } else if (const char *sp = wifi_net_saved_pass(ap.ssid)) {
            /* M5.2: rede já salva conecta direto, sem prompt. */
            char msg[160];
            snprintf(msg, sizeof(msg), "[wifi] conectando em \"%s\" (senha salva)", ap.ssid);
            log_line(msg, NULL);
            g_ui->set_cfg_wifi_on(true);   /* M5.3.2: switch reflete o rádio */
            wifi_net_connect(ap.ssid, sp, false);
        } else {
            g_wifi_pick_ssid = ap.ssid;
            g_fm.prompt = true;
            g_fm.prompt_needs_text = true;
            g_fm.prompt_title = "Senha para \"" + g_wifi_pick_ssid + "\":";
            g_fm.prompt_text = "";
            g_fm.prompt_pos = g_fm.prompt_text.size();
            g_fm.prompt_action = "wifipass";
            push_fm_ui();
        }
    });
}
