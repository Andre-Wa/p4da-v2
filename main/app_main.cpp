/**
 * app_main.cpp — bring-up + composition root da camada app (p4da-v2).
 *
 * Ordem de bring-up (herdada da v1, testada em campo):
 *   NVS -> storage -> config(Lua) -> VM Lua -> display -> touch ->
 *   Slint -> wiring das telas -> entrada USB -> energia -> sessão ->
 *   event loop (task própria) -> flush do UI-LOOP-GATE.
 *
 * Este arquivo é o ÚNICO lugar que conhece todos os módulos: injeta o
 * provedor de touch-cego no BSP (quebra o ciclo board->power da v1),
 * registra os callbacks de energia e cria as tasks do event loop. Os
 * módulos de tela se conectam entre si apenas via screens.h.
 *
 * Threading (docs/ARCHITECTURE.md, não negociável):
 *  1. UI nunca bloqueia — I/O em spawn_thread, resultado via ui_post;
 *  2. VM Lua single-thread (g_lua_mtx em screen_scripts.cpp);
 *  3. mutação de propriedades Slint só no event loop;
 *  4. power_mgmt_activity() em todo handler (via activity());
 *  5. callback de driver NUNCA invoca direto na UI — sempre ui_post
 *     (UI-LOOP-GATE: antes do loop acumula FIFO, depois encaminha).
 */

#include "slint-esp.h"
#include "app_ui.h"   /* gerado a partir de ui/app_ui.slint */

#include "app_context.h"
#include "screens.h"

#include "board_config.h"
#include "display_init.h"
#include "touch_init.h"
#include "storage_init.h"
#include "pda_config.h"
#include "lua_runtime.h"
#include "power_mgmt.h"
#include "usb_hid_keyboard.h"   /* teardown/resume do HID no standby */
#include "wifi_net.h"           /* L2: STA+NTP via C6 */
#include "audio_i2s.h"          /* L3: falante local (beep + player) */
#include "audio_uac.h"          /* L3: player USB (UAC1) */

#include "nvs_flash.h"

#include "esp_log.h"
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <vector>

static const char *TAG = "app_main";

/* Heap-allocated de propósito: ComponentHandle não tem ctor default, e
 * o handle precisa viver para sempre (dono da referência do component). */
static slint::ComponentHandle<AppWindow> *s_ui_run_handle = nullptr;
static esp_lcd_touch_handle_t s_touch_handle = nullptr;   /* M4.15: GT911 sleep no standby */

/* UI-LOOP-GATE: a ui_loop (prio 5 > main 1) preempta na criação e configura
 * a task estática do Slint na 1ª linha do run_event_loop; +100 ms depois o
 * notify é seguro e o acumulado do bring-up entra no loop (ordem FIFO). */
static void ui_gate_flush_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(100));
    int n = ui_gate_flush();
    if (n > 0)
        ESP_LOGI(TAG, "ui-gate: %d evento(s) do bring-up aplicado(s) no loop", n);
    vTaskDelete(NULL);
}

static void ui_run_task(void *arg)
{
    (void)arg;
    (*s_ui_run_handle)->run();   /* operator-> devolve AppWindow*, que tem run() */
    vTaskDelete(NULL);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== PDA v2.0.0 (L1 fundação) — bring-up ===");

    /* NVS antes de tudo: a sombra de config (pda_config) precisa dele já
     * no boot-heal. */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    wifi_net_seed_clock();   /* M5.0b (A3, v1): relógio plausível sem rede */
    board_storage_init();
    pda_config_init();
    lua_runtime_init();
    lua_runtime_set_log_callback(log_line, NULL);
    lua_runtime_set_toast_callback(toast, NULL);
    lua_runtime_set_settings_changed_cb(settings_changed_by_lua);

    esp_lcd_panel_handle_t panel = nullptr;
    ESP_ERROR_CHECK(board_display_init(&panel));
    board_display_backlight_set((uint8_t)pda_settings()->brightness);

    esp_lcd_touch_handle_t touch = nullptr;
    ESP_ERROR_CHECK(board_touch_init(&touch));
    s_touch_handle = touch;

    static std::vector<slint::platform::Rgb565Pixel> framebuffer(
        BOARD_LCD_H_RES_NATIVE * BOARD_LCD_V_RES_NATIVE);

    slint_esp_init(SlintPlatformConfiguration<slint::platform::Rgb565Pixel>{
        .size = slint::PhysicalSize({BOARD_LCD_V_RES_NATIVE, BOARD_LCD_H_RES_NATIVE}),
        .panel_handle = panel,
        .touch_handle = touch,
        .buffer1 = framebuffer,
        .rotation = slint::platform::SoftwareRenderer::RenderingRotation::Rotate90,
        .byte_swap = false,
    });

    auto ui = AppWindow::create();
    g_ui = ui.operator->();
    g_script_log = std::make_shared<slint::VectorModel<slint::SharedString>>();
    ui->set_script_log(g_script_log);

    push_settings_to_ui();
    ed_push_osk_rows();

    /* ---------- wiring das telas (cada módulo registra os próprios
     * callbacks; ordem irrelevante entre si — o event loop ainda não
     * roda) ---------- */
    status_bar_wire(*ui);
    launcher_wire(*ui);
    networks_wire(*ui);
    music_wire(*ui);
    files_wire(*ui);
    notes_wire(*ui);
    editor_wire(*ui);      /* cria também o timer do cursor piscante */
    scripts_wire(*ui);
    settings_wire(*ui);    /* Config + pulldown de ajustes rápidos */

    ui->on_app_back([]() {
        activity();
        /* saindo da tela Redes sem escolher: retoma reconexão da rede
         * salva (o scan pausa o autoreconnect — v1 M4b). */
        if (g_ui->get_active_app() == AppState::Networks) {
            wifi_net_set_autoreconnect(true);
        }
        nav_back();
    });

    /* ---------- áudio (L3): falante local + UAC ----------
     * v5.4.3 (v1, lição cara): o boot-wire do UAC já se perdeu UMA vez
     * por replace sem assert — sem audio_uac_init() o driver NUNCA
     * instala e o log fica mudo. Eventos dos dois backends refrescam a
     * tela Música via ui_post (regra 5). */
    audio_i2s_init();
    audio_i2s_set_event_cb([](void *) {
        ui_post([]() { push_mus_ui(); });
    }, NULL);
    audio_uac_set_event_cb([](void *) {
        ui_post([]() { push_mus_ui(); });
    }, NULL);
    audio_uac_init();

    /* ---------- entrada USB (teclado HID) ---------- */
    input_usb_init();

    /* ---------- energia ---------- */
    /* v2: o BSP não conhece power_mgmt — o composition root injeta o
     * predicado do "toque cego" (inversão de dependência, F1). */
    board_touch_set_blind_provider(power_mgmt_touch_blind);

    power_mgmt_set_standby_cb([](bool entering, void *) {
        /* Teardown/re-init de periféricos SOMENTE no modo light sleep
         * (hoje atrás do kill-switch). No standby robusto a CPU fica idle
         * e SD/USB permanecem vivos — remontar aqui era o que derrubava
         * USB e SD sem necessidade (log v1 de 2026-09-25). */
        const bool ls = power_mgmt_light_sleep_active();
        if (entering) {
            wifi_net_pause();   /* M4.14 (v1): standby não reconecta nem polla NTP */
            /* M4.15: sem wake por toque armado, o GT911 dorme no standby
             * (~3,5 mA economizados); o proxy do touch já cega read/get_xy
             * então não há I2C nem pontos fantasmas. Com a chave ON o chip
             * fica acordado (é ele que dá o wake). */
            if (s_touch_handle && !power_mgmt_touch_wake_armed()) {
                if (esp_lcd_touch_enter_sleep(s_touch_handle) == ESP_OK) {
                    ESP_LOGI(TAG, "GT911 em sleep (standby sem wake por toque)");
                }
            }
            if (ls) usb_hid_keyboard_prepare_sleep();
            return;
        }
        if (s_touch_handle) esp_lcd_touch_exit_sleep(s_touch_handle);
        wifi_net_resume();      /* M4.14: reconecta/NTP volta ao acordar */
        if (ls) {
            storage_remount_sd();
            usb_hid_keyboard_resume();
        }
        ui_post([]() {
            update_clock();
            char buf[96];
            storage_sd_describe(buf, sizeof(buf));
            g_ui->set_status_store(slint::SharedString(buf));
            g_ui->set_cfg_store_info(slint::SharedString(buf));
            g_ui->set_status_store_icon(slint::SharedString(storage_sd_mounted() ? "sd" : "int"));
            g_ui->set_has_keyboard(usb_hid_keyboard_connected());
        });
    }, NULL);
    power_mgmt_set_hibernate_save_cb(session_save, NULL);
    power_mgmt_init();

    /* Wi-Fi/NTP (L2): sem config/wifi.lua retorna NOT_FOUND e segue
     * offline (relógio de uptime). */
    wifi_net_set_enabled_boot(pda_settings()->wifi_enabled);
    if (wifi_net_init() == ESP_OK) {
        g_ui->set_status_wifi(slint::SharedString("wifi ..."));
    } else {
        g_ui->set_status_wifi(slint::SharedString("wifi off"));
    }

    session_restore_if_needed();
    update_clock();

    /* O event loop do Slint roda em task própria com pilha em PSRAM:
     * o layout precisa de ~32 KiB de pilha (herança v1: a RAM interna
     * ficou curta com o ESP-Hosted; na L1 sem hosted a folga é maior,
     * mas o padrão validado permanece). */
    s_ui_run_handle = new slint::ComponentHandle<AppWindow>(ui);
    BaseType_t ok = xTaskCreateWithCaps(ui_run_task, "ui_loop", 32768, NULL, 5,
                                        NULL, MALLOC_CAP_SPIRAM);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "pilha PSRAM p/ UI falhou — fallback interno");
        ok = xTaskCreate(ui_run_task, "ui_loop", 32768, NULL, 5, NULL);
    }
    ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_FAIL);
    /* M4.15.3 + UI-LOOP-GATE (v5.16.0): o restore NÃO depende mais da
     * corrida de prioridade p/ notify — vai p/ o pending do gate e roda na
     * 1ª iteração do loop, junto com eventos de driver do bring-up. */
    if (s_rest_pending) {
        ui_post([]() { session_restore_apply(); });
    }
    if (xTaskCreate(ui_gate_flush_task, "ui_gate", 4096, NULL, 4, NULL) != pdPASS)
        ESP_LOGE(TAG, "ui_gate: task de flush NÃO criada — eventos do bring-up ficam retidos");
    ESP_LOGI(TAG, "bring-up completo, entrando no loop do Slint");
    /* app_main retorna: a task main se encerra e libera os 8 KiB internos */
}
