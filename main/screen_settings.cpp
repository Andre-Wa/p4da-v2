/**
 * screen_settings.cpp — Config (4 abas) + pulldown de ajustes rápidos.
 *
 * Herdado da v1 (M1 + M4.12 + M5.0): Salvar grava system.lua (raiz ativa
 * + espelho) e devolve aos sliders os valores REAIS pós-clamp (A2: a UI
 * não mente); cursor/acento persistem NA HORA (M4.11b: depender do
 * próximo Salvar perdia o valor se o boot seguinte caísse na cura
 * espelho/NVS); o slider de brilho do pulldown aplica o backlight AO
 * VIVO mas só grava no FECHAMENTO do painel (debounce — sem isso cada
 * pixel de arrasto vira uma escrita no cartão).
 *
 * v2/L1: sem chave de Wi-Fi (L2), sem teste de som (L3), sem reset de
 * bateria (L4) — os callbacks correspondentes saíram do app_ui.slint.
 */

#include "screens.h"

#include "pda_config.h"
#include "power_mgmt.h"
#include "storage_init.h"
#include "wifi_net.h"
#include "audio_i2s.h"

#include "esp_log.h"

#include <cstdio>

static const char *TAG = "settings";

/* M4.12 — pulldown: brilho vivo + persistência só no fechamento. */
static bool s_qs_dirty = false;

void push_settings_to_ui(void)
{
    const pda_settings_t *s = pda_settings();
    g_ui->set_cfg_brightness((float)s->brightness);
    g_ui->set_cfg_dim_s((float)s->dim_after_s);
    g_ui->set_cfg_off_s((float)s->screen_off_after_s);
    g_ui->set_cfg_deep_s((float)s->deep_sleep_after_s);
    g_ui->set_cfg_wake_touch(s->wake_on_touch);
    g_ui->set_cfg_accent(slint::SharedString(s->accent));
    g_ui->set_cfg_boot_standby(s->boot_btn_standby);
    g_ui->set_cfg_osk_auto(s->onscreen_keyboard_auto);
    g_ui->set_cfg_light_sleep(s->light_sleep);
    g_ui->set_cfg_wifi_on(s->wifi_enabled);

    g_ui->set_cfg_wifi_info(slint::SharedString(
        wifi_net_connected() ? wifi_net_ssid() : "offline"));

    char buf[96];
    storage_sd_describe(buf, sizeof(buf));
    g_ui->set_cfg_store_info(slint::SharedString(buf));
    g_ui->set_status_store(slint::SharedString(buf));
    g_ui->set_status_store_icon(slint::SharedString(storage_sd_mounted() ? "sd" : "int"));
}

void settings_open(void)
{
    push_settings_to_ui();
    nav_goto(AppState::Settings);
}

void settings_wire(AppWindow &ui)
{
    ui.on_cfg_save([]() {
        activity();
        pda_settings_t s = *pda_settings();
        s.brightness = (int)g_ui->get_cfg_brightness();
        s.dim_after_s = (int)g_ui->get_cfg_dim_s();
        s.screen_off_after_s = (int)g_ui->get_cfg_off_s();
        s.deep_sleep_after_s = (int)g_ui->get_cfg_deep_s();
        s.wake_on_touch = g_ui->get_cfg_wake_touch();
        s.boot_btn_standby = g_ui->get_cfg_boot_standby();
        s.onscreen_keyboard_auto = g_ui->get_cfg_osk_auto();
        s.light_sleep = g_ui->get_cfg_light_sleep();
        ESP_LOGI(TAG, "cfg-save UI: br=%f dim=%f off=%f deep=%f",
                 (double)g_ui->get_cfg_brightness(), (double)g_ui->get_cfg_dim_s(),
                 (double)g_ui->get_cfg_off_s(), (double)g_ui->get_cfg_deep_s());
        pda_settings_update(&s);
        pda_config_save();
        /* M5.0b (A2): clamp_all pode ajustar (deep >= off+10 etc.);
         * devolve os valores REAIS aos sliders p/ UI não mentir. */
        {
            const pda_settings_t *cs = pda_settings();
            g_ui->set_cfg_brightness((float)cs->brightness);
            g_ui->set_cfg_dim_s((float)cs->dim_after_s);
            g_ui->set_cfg_off_s((float)cs->screen_off_after_s);
            g_ui->set_cfg_deep_s((float)cs->deep_sleep_after_s);
        }
        s_qs_dirty = false;   /* o save do painel de Config já persistiu o brilho */
        apply_brightness_from_settings();
        ed_push_ui(false);   /* osk_auto pode ter mudado */
        log_line("[config] salva em system.lua", NULL);
    });
    ui.on_cfg_reset([]() {
        activity();
        pda_config_reset_defaults();
        push_settings_to_ui();
    });
    ui.on_cfg_test_speaker([]() {
        activity();
        spawn_thread("spk_test", 6144, []() { audio_i2s_beep(1200); });
    });
    ui.on_cfg_sleep_now([]() {
        activity();
        power_mgmt_request_standby();
    });
    ui.on_cfg_set_cursor([](slint::SharedString st) {
        activity();
        pda_settings_set("ui.cursor", 0, false, false, std::string(st.data()).c_str());
        /* M4.11b: persiste NA HORA. Antes só ia para a RAM (s_dirty) e
         * dependia do próximo "Salvar"; se o boot seguinte caísse na cura
         * espelho/NVS, o cursor voltava ao default. */
        pda_config_save();
        s_qs_dirty = false;   /* save completo: brilho pendente do pulldown foi junto */
        ESP_LOGI(TAG, "[config] cursor salvo: %s", pda_settings()->cursor_style);
        ui_post([]() {
            if (g_ui->get_active_app() == AppState::Editor) ed_push_ui(false);
        });
    });
    ui.on_cfg_set_accent([](slint::SharedString a) {
        activity();
        pda_settings_set("ui.accent", 0, false, false, std::string(a.data()).c_str());
        pda_config_save();
        g_ui->set_cfg_accent(slint::SharedString(pda_settings()->accent));
        ESP_LOGI(TAG, "[config] acento salvo: %s", pda_settings()->accent);
    });
    ui.on_cfg_set_wifi_on([](bool on) {
        activity();
        pda_settings_set("net.wifi_enabled", 0, true, on, NULL);
        pda_config_save();
        wifi_net_set_enabled(on);
        ESP_LOGI(TAG, "[config] wifi %s pela chave", on ? "ligado" : "desligado");
    });
    ui.on_cfg_open_networks([]() {
        activity();
        networks_open();
    });
    ui.on_cfg_hibernate([]() {
        power_mgmt_hibernate();
    });

    /* ---------- quick settings pulldown (M4.12) ---------- */
    ui.on_qs_brightness([](float v) {
        activity();
        pda_settings_t s = *pda_settings();
        if (s.brightness == (int)v) return;
        s.brightness = (int)v;
        pda_settings_update(&s);          /* RAM apenas — I/O só no fechamento */
        apply_brightness_from_settings(); /* backlight ao vivo */
        s_qs_dirty = true;
    });
    ui.on_qs_state([](bool open) {
        activity();
        if (!open && s_qs_dirty) {
            s_qs_dirty = false;
            pda_config_save();
            ESP_LOGI(TAG, "[quick] brilho persistido: %d", pda_settings()->brightness);
        }
    });
}
