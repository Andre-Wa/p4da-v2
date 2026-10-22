/**
 * status_bar.cpp — relógio + armazenamento + estado de energia.
 *
 * v2/L1: o relógio é UPTIME (HH:MM:SS desde o boot) — o seed de RTC e o
 * NTP vivem na camada de rede (L2, wifi_net da v1: wifi_net_seed_clock +
 * wifi_net_time_hhmm). A linha de bateria da v1 (BAT.0, 4 s de cache)
 * volta na L4; o indicador de Wi-Fi, na L2. O tick de 1 s vem do Timer
 * do app_ui.slint (on_tick).
 */

#include "screens.h"

#include "power_mgmt.h"

#include "esp_timer.h"

#include <cstdio>

void update_clock(void)
{
    char buf[32];
    /* L1: sem NTP — relógio de uptime, como o fallback da v1. Na L2 este
     * ramo ganha a hora de rede (wifi_net_time_hhmm na v1). */
    int64_t up_s = esp_timer_get_time() / 1000000LL;
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             (int)(up_s / 3600), (int)((up_s / 60) % 60), (int)(up_s % 60));
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
