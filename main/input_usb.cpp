/**
 * input_usb.cpp — teclado USB HID -> editor + estado has_keyboard.
 *
 * Herdado da v1 (M2 + M5a.2): os callbacks do driver HID rodam em task do
 * driver — NUNCA tocam a UI direto (regra 5 de ARCHITECTURE.md): tudo
 * passa por ui_post(). Setas/Home/End/Delete pelo keycode HID; ASCII
 * pelo mapa US do driver (backspace/tab incluídos).
 *
 * v2/L1: o callback de MEDIA KEYS (consumer control) não é registrado —
 * usb_hid_keyboard_set_media_cb() volta a ser chamado na camada L3
 * (player de música). Teclas de mídia de teclados completos são
 * simplesmente ignoradas nesta fundação.
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
}
