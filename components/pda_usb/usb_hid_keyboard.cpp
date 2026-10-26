/**
 * usb_hid_keyboard.cpp — teclado USB HID via USB Host nativo do ESP32-P4.
 *
 * Refatorado do protótipo (ver docs/USB.md). Três correções estruturais:
 *
 *  1) HOT-PLUG: a task da biblioteca host não se mata mais em ALL_FREE.
 *     No protótipo, a janela entre usb_host_install() e hid_host_install()
 *     (zero clients, zero devices) fazia a task dar break+vTaskDelete no
 *     boot sem teclado — e nenhum hot-plug era visto depois.
 *
 *  2) LIGHT SLEEP: o periférico DWC2 não sobrevive ao sleep (como o
 *     SDMMC). prepare_sleep() faz teardown ordenado e resume() reinstala.
 *
 *  3) DEDUP: com set_idle(0) o teclado manda relatório só em mudança, mas
 *     dispositivos que ignoram idle mandariam o report inteiro repetido —
 *     comparamos com o relatório anterior e só emitimos NOVAS imprensa.
 */

#include "usb_hid_keyboard.h"
#include "usb_host_state.h"

#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"   /* v2.2.2: fila do watcher */

#include <cstring>
#include <atomic>

static const char *TAG = "usb_hid_kbd";

static KeyPressCallback s_callback = nullptr;
static UsbKbdEventCallback s_event_cb = nullptr;

static TaskHandle_t s_lib_task = NULL;
static volatile bool s_installed = false;
static volatile bool s_host_ready = false;  /* v5.12.0: usb_host_install() OK (usb_host_state.h) */

/* v2.2.2 (rework do watcher v2.2.1, contra usb_host.h REAL do IDF 5.5):
 * DEV_GONE só é entregue a client que SEGURA o device ABERTO ("A device
 * opened by the client is now gone"). Então o watcher segura o handle de
 * TODO device enumerado (open device-level, sem claim de interface —
 * coexiste com HID/UAC): unplug gera DEV_GONE p/ nós ANTES de qualquer
 * open stale (crash uac_host.c:2171 da rodada 2026-10-25). Opens/closes
 * NUNCA no callback de evento (mesma razão do UAC M5.4.4): callback
 * enfileira, esta task executa. */
#define USB_WATCH_MAX 8
typedef struct { uint8_t op; uint8_t addr; usb_device_handle_t hdl; } wevt_t;
static QueueHandle_t s_watch_q = NULL;
static TaskHandle_t s_watch_task = NULL;
static usb_host_client_handle_t s_watch_cli = NULL;
static struct { uint8_t addr; usb_device_handle_t hdl; } s_held[USB_WATCH_MAX];
static volatile bool s_watch_close_all = false;

static usb_host_dev_gone_cb s_gone_cb = NULL;

void usb_host_set_dev_gone_cb(usb_host_dev_gone_cb cb) { s_gone_cb = cb; }

static void usb_watch_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    (void)arg;
    wevt_t e = { 0, 0, NULL };
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        e.op = 1;
        e.addr = msg->new_dev.address;
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        e.op = 2;
        e.hdl = msg->dev_gone.dev_hdl;
    } else {
        return;
    }
    xQueueSend(s_watch_q, &e, 0);
}

static void usb_watch_task(void *arg)
{
    (void)arg;
    usb_host_client_config_t ccfg = {};
    ccfg.is_synchronous = false;
    ccfg.max_num_event_msg = 4;
    ccfg.async.client_event_callback = usb_watch_cb;
    ccfg.async.callback_arg = NULL;
    if (usb_host_client_register(&ccfg, &s_watch_cli) != ESP_OK) {
        ESP_LOGW(TAG, "watcher: client_register falhou — sem aviso de gone");
        s_watch_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        /* callbacks rodam NESTE contexto (docs do IDF) e só enfileiram */
        const bool alive =
            (usb_host_client_handle_events(s_watch_cli,
                                           pdMS_TO_TICKS(50)) == ESP_OK);
        wevt_t e;
        while (xQueueReceive(s_watch_q, &e, 0) == pdTRUE) {
            if (e.op == 1) {
                usb_device_handle_t h = NULL;
                if (usb_host_device_open(s_watch_cli, e.addr, &h) == ESP_OK) {
                    for (auto &sl : s_held)
                        if (!sl.hdl) { sl.hdl = h; sl.addr = e.addr; break; }
                } else {
                    ESP_LOGW(TAG, "watcher: open addr %u falhou (não segurado)",
                             (unsigned)e.addr);
                }
            } else {
                uint8_t addr = 0;
                for (auto &sl : s_held)
                    if (sl.hdl == e.hdl) { addr = sl.addr; sl.hdl = NULL; sl.addr = 0; break; }
                usb_host_device_close(s_watch_cli, e.hdl);  /* cleanup do gone */
                if (addr && s_gone_cb) {
                    ESP_LOGI(TAG, "watcher: device addr %u desconectou",
                             (unsigned)addr);
                    s_gone_cb(addr);
                }
            }
        }
        if (s_watch_close_all) {
            s_watch_close_all = false;
            for (auto &sl : s_held)
                if (sl.hdl) {
                    usb_host_device_close(s_watch_cli, sl.hdl);
                    sl.hdl = NULL; sl.addr = 0;
                }
        }
        if (!alive) break;   /* teardown da lib: sai e libera o slot */
    }
    usb_host_client_deregister(s_watch_cli);
    s_watch_cli = NULL;
    s_watch_task = NULL;
    vTaskDelete(NULL);
}

void usb_host_watcher_close_all(void)
{
    s_watch_close_all = true;
    for (int i = 0; i < 30; i++) {   /* espera o loop soltar tudo (≤300 ms) */
        bool any = false;
        for (auto &sl : s_held) if (sl.hdl) { any = true; break; }
        if (!any) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static volatile bool s_shutdown = false;      /* pede saída da lib task */
static SemaphoreHandle_t s_lib_done_sem = NULL; /* lib task avisou que saiu */

#define USB_KBD_MAX_IFACES 4
static hid_host_device_handle_t s_devs[USB_KBD_MAX_IFACES] = { nullptr };
static std::atomic<int> s_conn_count{0};

static uint8_t s_prev_keys[6] = { 0 };

/* ------------------------------------------------------------------ */
/* Mapa HID keycode -> ASCII (US-QWERTY, com shift na linha de números) */
static const char s_shifted_numbers[] = "!@#$%^&*()";

static uint8_t hid_keycode_to_ascii(uint8_t keycode, bool shift)
{
    if (keycode >= 0x04 && keycode <= 0x1D) { // a-z
        char c = 'a' + (keycode - 0x04);
        return shift ? (char)(c - 'a' + 'A') : (uint8_t)c;
    }
    if (keycode >= 0x1E && keycode <= 0x27) { // 1-9, 0
        if (shift) return (uint8_t)s_shifted_numbers[keycode - 0x1E];
        return (keycode == 0x27) ? '0' : (uint8_t)('1' + (keycode - 0x1E));
    }
    switch (keycode) {
        case 0x28: return '\n';   // Enter
        case 0x2A: return '\b';   // Backspace
        case 0x2B: return '\t';   // Tab
        case 0x2C: return ' ';    // Space
        case 0x2D: return shift ? '_' : '-';
        case 0x2E: return shift ? '+' : '=';
        case 0x2F: return shift ? '{' : '[';
        case 0x30: return shift ? '}' : ']';
        case 0x31: return shift ? '|' : '\\';
        case 0x33: return shift ? ':' : ';';
        case 0x34: return shift ? '"' : '\'';
        case 0x36: return shift ? '<' : ',';
        case 0x37: return shift ? '>' : '.';
        case 0x38: return shift ? '?' : '/';
        default:   return 0;      // setas, F1-F12 etc: use o keycode no app
    }
}

/* ------------------------------------------------------------------ */
static void track_add(hid_host_device_handle_t h)
{
    for (int i = 0; i < USB_KBD_MAX_IFACES; i++) {
        if (s_devs[i] == nullptr) { s_devs[i] = h; break; }
    }
    s_conn_count.fetch_add(1);
    if (s_event_cb) s_event_cb(true);
}

static void track_remove(hid_host_device_handle_t h)
{
    bool found = false;
    for (int i = 0; i < USB_KBD_MAX_IFACES; i++) {
        if (s_devs[i] == h) { s_devs[i] = nullptr; found = true; }
    }
    if (!found) return;
    if (s_conn_count.load() > 0) s_conn_count.fetch_sub(1);
    if (s_event_cb) s_event_cb(s_conn_count.load() > 0);
}

bool usb_hid_keyboard_connected(void) { return s_conn_count.load() > 0; }

/* ------------------------------------------------------------------ */
static MediaKeyCallback s_media_cb;

void usb_hid_keyboard_set_media_cb(MediaKeyCallback cb) { s_media_cb = cb; }

static void hid_keyboard_report_callback(const uint8_t *const data, int length)
{
    const uint8_t *report = data;
    int report_len = length;

    /* CircuitPython/KMK podem prefixar 1 byte de Report ID antes dos 8
     * bytes do boot protocol (tamanho 9 = 1 + 8). */
    if (length == 9) {
        report = data + 1;
        report_len = 8;
    }
    /* M5a.2: consumer controls (media keys) chegam como reports curtos
     * (2 bytes de usage HUT 0x0C, ou 3 com Report ID — mesmo padrão do
     * prefixo de 1 byte que o boot protocol vê no caso de 9 bytes).
     * Teclados boot seguem o caminho de 8 bytes abaixo, intocados. */
    if (report_len >= 1 && report_len <= 3 && s_media_cb) {
        uint16_t usage = (report_len == 3)
            ? (uint16_t)(report[1] | (report[2] << 8))
            : (uint16_t)(report[0] | (report_len == 2 ? (report[1] << 8) : 0));
        int act = -1;
        switch (usage) {
        case 0x00B5: act = USB_MEDIA_NEXT; break;
        case 0x00B6: act = USB_MEDIA_PREV; break;
        case 0x00B7: act = USB_MEDIA_STOP; break;
        case 0x00CD: act = USB_MEDIA_PLAYPAUSE; break;
        case 0x00E9: act = USB_MEDIA_VOL_UP; break;
        case 0x00EA: act = USB_MEDIA_VOL_DOWN; break;
        case 0x00E2: act = USB_MEDIA_MUTE; break;
        default: break;
        }
        if (usage == 0) return;          /* release: nada a fazer */
        if (act >= 0) { s_media_cb(act); return; }
        /* M5a.2.2 (v5.8): mapa vendor Jieli/QCY H3S — codes de 1 byte
         * capturados no log da 16ª rodada: 08=play/pause, 01=vol+,
         * 02=vol-. Reports de 2 B com byte alto 0 e estes codes não
         * colidem com usages reais do Consumer HUT (B5+ / E9+ / etc.).
         * Long-press next/prev é processado DENTRO do fone (sem report). */
        if (report_len == 2 && report[1] == 0) {
            switch (report[0]) {
            case 0x08: act = USB_MEDIA_PLAYPAUSE; break;
            case 0x01: act = USB_MEDIA_VOL_UP; break;
            case 0x02: act = USB_MEDIA_VOL_DOWN; break;
            default: break;
            }
            if (act >= 0) { s_media_cb(act); return; }
        }
    }
    if (report_len < 8) {
        /* M5a.2.1 (v5.7): diagnóstico p/ media keys do H3S — o formato
         * real dos reports ainda é desconhecido; 1 linha/s com len+bytes
         * dos reports que não são teclado nem consumer conhecido. */
        static int64_t s_last_dbg = 0;
        const int64_t now = esp_timer_get_time();
        if (length == 0) return;   /* report vazio de unplug: ruído */
        if (now - s_last_dbg > 1000000) {
            s_last_dbg = now;
            ESP_LOGI(TAG, "report descartado: len=%d bytes=%02x %02x %02x %02x",
                     length,
                     length > 0 ? data[0] : 0, length > 1 ? data[1] : 0,
                     length > 2 ? data[2] : 0, length > 3 ? data[3] : 0);
        }
        return;
    }

    const uint8_t modifiers = report[0];
    const bool shift = (modifiers & 0x22) != 0;   // left|right shift

    /* dedup: só emite tecla que não estava no relatório anterior */
    uint8_t cur[6];
    memcpy(cur, &report[2], 6);
    for (int i = 0; i < 6; i++) {
        uint8_t keycode = cur[i];
        if (keycode == 0) continue;
        bool was_down = false;
        for (int j = 0; j < 6; j++) {
            if (s_prev_keys[j] == keycode) { was_down = true; break; }
        }
        if (was_down) continue;

        uint8_t ascii = hid_keycode_to_ascii(keycode, shift);
        ESP_LOGD(TAG, "tecla kc=0x%02X ascii=0x%02X mod=0x%02X", keycode, ascii, modifiers);
        if (s_callback) s_callback(ascii, keycode, modifiers);
    }
    memcpy(s_prev_keys, cur, 6);
}

static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                        const hid_host_interface_event_t event,
                                        void *arg)
{
    (void)arg;
    uint8_t data[64] = { 0 };
    size_t data_length = 0;

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
        if (hid_host_device_get_raw_input_report_data(hid_device_handle, data,
                                                        sizeof(data), &data_length) == ESP_OK) {
            hid_keyboard_report_callback(data, (int)data_length);
        }
        break;
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "interface HID desconectada");
        hid_host_device_close(hid_device_handle);
        track_remove(hid_device_handle);
        break;
    default:
        break;
    }
}

static void hid_host_device_event(hid_host_device_handle_t hid_device_handle,
                                  const hid_host_driver_event_t event,
                                  void *arg)
{
    (void)arg;
    hid_host_dev_params_t dev_params;
    if (hid_host_device_get_params(hid_device_handle, &dev_params) != ESP_OK) return;

    if (event != HID_HOST_DRIVER_EVENT_CONNECTED) return;

    /* Aceita boot-protocol keyboard E HID genérico (subclass=0/proto=0,
     * o caso do KMK/CircuitPython); rejeita mouse boot (proto=2) etc. */
    const bool parece_teclado =
        (dev_params.sub_class == HID_SUBCLASS_BOOT_INTERFACE && dev_params.proto == HID_PROTOCOL_KEYBOARD) ||
        (dev_params.sub_class == 0 && dev_params.proto == 0);
    if (!parece_teclado) {
        ESP_LOGW(TAG, "ignorando interface HID não-teclado (subclass=%d proto=%d)",
                 dev_params.sub_class, dev_params.proto);
        return;
    }

    ESP_LOGI(TAG, "teclado conectado (subclass=%d proto=%d)",
             dev_params.sub_class, dev_params.proto);

    const hid_host_device_config_t dev_config = {
        .callback = hid_host_interface_callback,
        .callback_arg = NULL,
    };
    esp_err_t err = hid_host_device_open(hid_device_handle, &dev_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "falha ao abrir interface HID (%s)", esp_err_to_name(err));
        return;
    }
    if (dev_params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
        hid_class_request_set_protocol(hid_device_handle, HID_REPORT_PROTOCOL_BOOT);
        hid_class_request_set_idle(hid_device_handle, 0, 0); /* só reporta em mudança */
    }
    err = hid_host_device_start(hid_device_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "falha ao iniciar interface HID (%s)", esp_err_to_name(err));
        hid_host_device_close(hid_device_handle);
        return;
    }
    memset(s_prev_keys, 0, sizeof(s_prev_keys));
    track_add(hid_device_handle);
}

/* ------------------------------------------------------------------ */
static void usb_host_lib_task(void *arg)
{
    usb_host_config_t host_config = {};
    host_config.skip_phy_setup = false;
    host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;
    if (usb_host_install(&host_config) != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install falhou");
        xTaskNotifyGive((TaskHandle_t)arg);
        vTaskDelete(NULL);
        return;
    }
    s_host_ready = true;   /* v5.12.0: 2º client (UAC) espera por isto */
    /* v2.2.1/2.2.2: watcher de desconexão (client próprio + handles segurados) */
    if (s_watch_task == NULL) {
        if (!s_watch_q) s_watch_q = xQueueCreate(8, sizeof(wevt_t));
        xTaskCreate(usb_watch_task, "usb_watch", 3072, NULL, 2, &s_watch_task);
    }
    xTaskNotifyGive((TaskHandle_t)arg);

    /* NÃO sair em ALL_FREE por conta própria: barramento vazio é o
     * estado NORMAL de um PDA sem teclado (sair aqui matava o hot-plug).
     * A saída só ocorre em dois casos:
     *   - s_shutdown && ALL_FREE  (teardown pedido por prepare_sleep);
     *   - handle_events retornar erro (abort raro).
     * Ordem importa: o usb_host_uninstall() só pode ser chamado por OUTRO
     * task depois que esta task acabou (padrão dos exemplos do IDF). */
    while (true) {
        uint32_t event_flags = 0;
        if (usb_host_lib_handle_events(portMAX_DELAY, &event_flags) != ESP_OK) break;
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
        if (s_shutdown && (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)) break;
    }
    ESP_LOGI(TAG, "task usb_host encerrada");
    if (s_lib_done_sem) xSemaphoreGive(s_lib_done_sem);
    vTaskDelete(NULL);
}

static void start_stack(void)
{
    if (s_installed) return;
    if (!s_lib_done_sem) s_lib_done_sem = xSemaphoreCreateBinary();
    s_shutdown = false;

    xTaskCreate(usb_host_lib_task, "usb_host", 4096,
                xTaskGetCurrentTaskHandle(), 2, &s_lib_task);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    const hid_host_driver_config_t hid_cfg = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_host_device_event,
        .callback_arg = NULL,
    };
    if (hid_host_install(&hid_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "hid_host_install falhou");
        return;
    }
    s_installed = true;
    ESP_LOGI(TAG, "USB Host HID pronto — hot-plug ativo");
}

void usb_hid_keyboard_init(KeyPressCallback on_key_press, UsbKbdEventCallback on_event)
{
    s_callback = on_key_press;
    if (on_event) s_event_cb = on_event;
    start_stack();
}

void usb_hid_keyboard_prepare_sleep(void)
{    usb_host_watcher_close_all();   /* v2.2.2: solta handles antes do uninstall */

    if (!s_installed) return;
    ESP_LOGI(TAG, "teardown USB p/ standby...");
    s_host_ready = false;   /* host vai sumir; UAC não deve assumir o contrário */
    for (int i = 0; i < USB_KBD_MAX_IFACES; i++) {
        if (s_devs[i]) {
            hid_host_device_close(s_devs[i]);
            s_devs[i] = nullptr;
        }
    }
    s_conn_count.store(0);

    /* 1) pede à lib task para sair quando o barramento esvaziar; */
    s_shutdown = true;
    /* 2) sem client, o host libera tudo e a lib task vê ALL_FREE e sai; */
    if (hid_host_uninstall() != ESP_OK) {
        ESP_LOGW(TAG, "hid_host_uninstall reportou erro (seguindo)");
    }
    s_installed = false;
    /* 3) espera a lib task confirmar a saída ANTES de desinstalar o host; */
    if (s_lib_done_sem &&
        xSemaphoreTake(s_lib_done_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "lib task não confirmou saída em 1 s (uninstall mesmo assim)");
    }
    esp_err_t err = usb_host_uninstall();
    ESP_LOGI(TAG, "usb_host_uninstall: %s", esp_err_to_name(err));
    s_lib_task = NULL;
}

void usb_hid_keyboard_resume(void)
{
    if (s_installed) {
        ESP_LOGW(TAG, "resume chamados com USB já instalado (ignorado)");
        return;
    }
    ESP_LOGI(TAG, "reinstall USB apos wake...");
    start_stack();
    ESP_LOGI(TAG, "USB apos wake: instalado=%d", (int)s_installed);
}

/* v5.12.0: ver usb_host_state.h — audio_uac espera por isto antes do
 * 1º uac_host_install() (elimina o E "Unable to register USB Host client"
 * que aparecia em todo boot por corrida de inicialização). */
bool usb_host_state_ready(void) { return s_host_ready; }
