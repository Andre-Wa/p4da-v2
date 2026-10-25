/**
 * input_usb.cpp — teclado USB HID -> editor + estado has_keyboard.
 *
 * Herdado da v1 (M2 + M5a.2): os callbacks do driver HID rodam em task do
 * driver — NUNCA tocam a UI direto (regra 5 de ARCHITECTURE.md): tudo
 * passa por ui_post(). Setas/Home/End/Delete pelo keycode HID; ASCII
 * pelo mapa US do driver (backspace/tab incluídos).
 *
 * v2.2.0 (L3): media keys de consumer control registradas — despacham
 * p/ music_on_media_key() (backend que toca: UAC ou falante local).
 *
 * Limpeza v2: a flag g_hid_seen da v1 (set-only desde o M2 — nenhuma
 * leitura) não veio junto.
 */

#include "screens.h"

#include "usb_hid_keyboard.h"

#include "esp_log.h"

static const char *TAG = "input_usb";

void input_usb_init(void)
{
    usb_hid_keyboard_init([](uint8_t ascii, uint8_t keycode, uint8_t /*mod*/) {
        ui_post([ascii, keycode]() {
            activity();
            if (g_ui->get_active_app() != AppState::Editor) return;
            if (g_ed.reading) return;
            switch (keycode) {
            case 0x4F: ed_move(1, 0, false, false); return;    // Right
            case 0x50: ed_move(-1, 0, false, false); return;   // Left
            case 0x51: ed_move(0, 1, false, false); return;    // Down
            case 0x52: ed_move(0, -1, false, false); return;   // Up
            case 0x4A: ed_move(0, 0, true, false); return;     // Home
            case 0x4D: ed_move(0, 0, false, true); return;     // End
            case 0x4C: ed_delete_key(); return;                // Delete
            default: break;
            }
            if (ascii == 0) return;
            if (ascii == '\b') ed_backspace();
            else if (ascii == '\t') ed_insert_str("  ", 2);
            else ed_insert_str((const char *)&ascii, 1);
        });
    },
    [](bool connected) {
        ESP_LOGI(TAG, "teclado USB: %s", connected ? "conectado" : "ausente");
        ui_post([connected]() {
            activity();
            g_ui->set_has_keyboard(connected);
            if (g_ui->get_active_app() == AppState::Editor) ed_push_ui(false);
        });
    });

    /* M5a.2 (v1) / L3 (v2): media keys do consumer control (H3S e
     * teclados completos) -> player. Callback roda em task do HID:
     * hop p/ UI via ui_post (regra 5 de ARCHITECTURE). */
    usb_hid_keyboard_set_media_cb([](int act) {
        ui_post([act]() { music_on_media_key(act); });
    });
}
