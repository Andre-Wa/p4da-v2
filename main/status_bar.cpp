/**
 * status_bar.cpp — relógio + armazenamento + estado de energia.
 *
 * v2.1.0 (L2): com NTP sincronizado o relógio é HH:MM local (TZ de
 * locale.timezone); sem rede, UPTIME HH:MM:SS (fallback da v1). O sink
 * de eventos de conexão (wifi_net_on_event_ui) mora aqui: é o único
 * símbolo que pda_net chama para cima (padrão da v1; candidato a
 * provider num refactor futuro, como o blind-provider do touch).
 * A linha de bateria volta na L4. O tick de 1 s vem do Timer do
 * app_ui.slint (on_tick).
 */

#include "screens.h"

#include "power_mgmt.h"
#include "wifi_net.h"

#include "esp_timer.h"

#include <cstdio>
#include <string>

/* Sink de eventos de conexão do pda_net (declarado extern "C" lá? não:
 * extern simples em wifi_net.c — mesmo TU-set C++/C sem mangling porque
 * wifi_net.c é C e esta definição é extern "C"). */
extern "C" void wifi_net_on_event_ui(bool connected, int rssi)
{
    char buf[40];
    if (connected) snprintf(buf, sizeof(buf), "wifi %d dBm", rssi);
    else snprintf(buf, sizeof(buf), "wifi --");
    std::string s(buf);
    ui_post([s]() {
        g_ui->set_status_wifi(slint::SharedString(s));
        g_ui->set_cfg_wifi_info(slint::SharedString(
            wifi_net_connected() ? wifi_net_ssid() : "offline"));
    });
}

void update_clock(void)
{
    char buf[32];
    /* L2: HH:MM local com NTP sincronizado; sem rede, uptime HH:MM:SS
     * (fallback histórico da v1/L1). */
    const char *hhmm = wifi_net_time_hhmm();
    if (hhmm) {
        snprintf(buf, sizeof(buf), "%s", hhmm);
    } else {
        int64_t up_s = esp_timer_get_time() / 1000000LL;
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                 (int)(up_s / 3600), (int)((up_s / 60) % 60), (int)(up_s % 60));
    }
    g_ui->set_status_clock(slint::SharedString(buf));

    const char *pwr = "ATIVO";
    switch (power_mgmt_state()) {
    case PDA_PWR_DIM: pwr = "DIM"; break;
    case PDA_PWR_STANDBY: pwr = "STANDBY"; break;
    default: pwr = "ATIVO"; break;
    }
    g_ui->set_status_pwr(slint::SharedString(pwr));
}

void status_bar_wire(AppWindow &ui)
{
    ui.on_tick([]() { update_clock(); });
}
