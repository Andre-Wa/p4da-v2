/**
 * audio_i2s.c — saída local ES8311 + NS4150: beep de UI (v1 M5c.1) e
 * player de WAV do cartão (SOM.1, v2.2.0).
 *
 * Topologia (board_config.h / esquemático GUITION folha 02):
 *   I2S0: MCLK 13, BCLK 12, LRCK 10, DOUT 9 (DIN 48 p/ mic, futuro)
 *   I2C0 compartilhado com o GT911 (bus_handle do touch_init)
 *   PA_EN 11 → NS4150 (o es8311_codec liga/desliga no open/close)
 *
 * Um canal I2S, duas fontes (beep × player): o handshake de suspensão
 * faz o player soltar o codec ENTRE chunks quando um beep pede passagem
 * (latência ≤ ~23 ms p/ o clique de UI), toca, e o player reabre na taxa
 * dele. Com o player PAUSADO o codec já está fechado — beep direto.
 *
 * Taxas do player: 44100/48000 (round 1 do SOM.1, docs/AUDIO.md); outras
 * taxas = recusa honesta no estado ("erro: taxa ..."). O esp_codec_dev
 * reconfigura o clock do I2S no open() com o sample_info da taxa do
 * arquivo (beep sempre 44k1).
 */

#include "audio_i2s.h"
#include "audio_vol.h"
#include "board_config.h"
#include "touch_init.h"
#include "wav_parse.h"

#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "audio_i2s";

static i2s_chan_handle_t s_tx = NULL;
static esp_codec_dev_handle_t s_dev = NULL;
static bool s_ready = false;
/* v5.13.0 (v1): serializa beeps — clique duplo não empilha senoides. */
static SemaphoreHandle_t s_beep_mtx = NULL;
/* Espelho do estado enable/disable do canal I2S TX (v1 M5c.1.2): o
 * componente desabilita no close(); sem o espelho o disable interno do
 * set_fmt() bate em canal já desabilitado e loga E a cada beep. */
static bool s_chan_on = false;

#define BEEP_RATE 44100

/* ---- player local (SOM.1) ---- */
typedef struct { int op; char path[144]; } cmd_t;   /* op 1=play */
static QueueHandle_t s_q = NULL;
static volatile bool s_playing = false;
static volatile bool s_paused = false;
static volatile bool s_stop_req = false;
/* handshake beep×música: beep pede, player solta o codec entre chunks */
static volatile bool s_suspend_req = false;
static volatile bool s_suspended = false;
static char s_track[64] = "";
static char s_state[48] = "sem init";
static audio_i2s_event_cb s_cb = NULL;
static void *s_ctx = NULL;
/* volume/mute locais (SOM.3) */
static int s_vol = BOARD_SPK_VOL_DEFAULT;
static bool s_muted = false;
static int s_prev_vol = BOARD_SPK_VOL_DEFAULT;

static void spk_play_task(void *arg);   /* fwd: criada no init, definida abaixo */

static void notify(void) { if (s_cb) s_cb(s_ctx); }
static void set_state(const char *s)
{
    snprintf(s_state, sizeof(s_state), "%s", s);
    notify();
}

esp_err_t audio_i2s_init(void)
{
    if (s_ready) return ESP_OK;

    /* ---- I2S0 TX (playback) ---- */
    i2s_chan_config_t ch_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&ch_cfg, &s_tx, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel falhou");
        return ESP_FAIL;
    }
    /* Macros OFICIAIS do IDF p/ clk/slot (v5.11.4/v1): adaptam-se à
     * divergência de campos entre targets. '.invert_flags' é STRUCT de
     * bitfields no IDF 5.5.1 — '= {0}', NUNCA '= false' (causa raiz do
     * -Werror=missing-braces da v5.11.x, ver tools/idfinit_check_i2s.c
     * da v1). */
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(BEEP_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK_GPIO,
            .bclk = BOARD_I2S_BCLK_GPIO,
            .ws   = BOARD_I2S_LRCK_GPIO,
            .dout = BOARD_I2S_DOUT_GPIO,
            .din  = GPIO_NUM_NC,
            .invert_flags = {0},
        },
    };
    if (i2s_channel_init_std_mode(s_tx, &std_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "init do canal I2S falhou");
        return ESP_FAIL;
    }
    /* NÃO habilitar aqui: esp_codec_dev_open() faz disable→reconfig→
     * enable internamente (comentário da v1). */

    /* ---- interfaces do codec ---- */
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BOARD_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = board_i2c_bus(),   /* MESMO bus do GT911 */
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl_if) { ESP_LOGE(TAG, "i2c ctrl if falhou"); return ESP_FAIL; }
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (!gpio_if) { ESP_LOGE(TAG, "gpio if falhou"); return ESP_FAIL; }

    es8311_codec_cfg_t es_cfg = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .pa_pin = BOARD_AMP_PA_EN_GPIO,
        .use_mclk = true,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
    if (!codec_if) { ESP_LOGE(TAG, "es8311_codec_new falhou (I2C?)"); return ESP_FAIL; }

    audio_codec_i2s_cfg_t i2s_cfg = { .tx_handle = s_tx, .rx_handle = NULL };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!data_if) { ESP_LOGE(TAG, "i2s data if falhou"); return ESP_FAIL; }

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec_if,
        .data_if = data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    s_dev = esp_codec_dev_new(&dev_cfg);
    if (!s_dev) { ESP_LOGE(TAG, "esp_codec_dev_new falhou"); return ESP_FAIL; }

    s_vol = audio_vol_get(AUDIO_VOL_INTERNAL);   /* dono do valor: audio_vol */
    esp_codec_dev_set_out_vol(s_dev, (float)s_vol);
    s_beep_mtx = xSemaphoreCreateMutex();
    if (!s_beep_mtx) { ESP_LOGE(TAG, "sem RAM p/ mutex do beep"); return ESP_ERR_NO_MEM; }

    s_q = xQueueCreate(2, sizeof(cmd_t));
    if (!s_q) return ESP_ERR_NO_MEM;
    if (xTaskCreateWithCaps(spk_play_task, "spk_play", 8192, NULL, 4, NULL,
                            MALLOC_CAP_SPIRAM) != pdPASS)
        return ESP_FAIL;

    s_ready = true;
    set_state("pronto");
    ESP_LOGI(TAG, "ES8311 + NS4150 prontos (I2S0, I2C condiv., PA=GPIO%d, vol=%d)",
             (int)BOARD_AMP_PA_EN_GPIO, s_vol);
    return ESP_OK;
}

bool audio_i2s_ready(void) { return s_ready; }

/* ---------------- volume/mute (SOM.3) ---------------- */
void audio_i2s_set_vol(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_vol = pct;
    s_muted = false;
    if (s_dev) esp_codec_dev_set_out_vol(s_dev, (float)pct);
    ESP_LOGI(TAG, "volume local %d%%", pct);
}
int audio_i2s_get_vol(void) { return s_vol; }
void audio_i2s_mute_toggle(void)
{
    if (s_muted) {
        audio_i2s_set_vol(s_prev_vol);
    } else {
        s_prev_vol = s_vol;
        s_muted = true;
        if (s_dev) esp_codec_dev_set_out_vol(s_dev, 0.f);
        ESP_LOGI(TAG, "mute local on (vol anterior %d)", s_prev_vol);
    }
}

/* ---------------- helpers de canal ---------------- */
static void chan_open(uint32_t rate)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = rate,
        .channel = 2,
        .bits_per_sample = 16,
    };
    if (!s_chan_on) {
        if (i2s_channel_enable(s_tx) == ESP_OK) s_chan_on = true;
    }
    if (esp_codec_dev_open(s_dev, &fs) == ESP_OK) s_chan_on = true;
}
static void chan_close(void)
{
    esp_codec_dev_close(s_dev);   /* desabilita o canal internamente */
    s_chan_on = false;
}

/* ---------------- beep (v1 M5c.1/M5c.1b + handshake SOM.1) ---------------- */
esp_err_t audio_i2s_beep(int ms)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_beep_mtx, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "beep descartado: outro beep em andamento (clique duplo?)");
        return ESP_ERR_INVALID_STATE;
    }
    /* Música tocando no falante? Pede o codec e espera o player soltar
     * (ele fecha entre chunks; ≤ ~23 ms). Pausado/idle: canal já livre. */
    const bool need_suspend = s_playing && !s_paused;
    if (need_suspend) {
        s_suspend_req = true;
        for (int i = 0; i < 50 && !s_suspended; i++)
            vTaskDelay(pdMS_TO_TICKS(10));
    }
    chan_open(BEEP_RATE);
    /* v5.14.0 (v1): log no INÍCIO do som — a ordem do log bate c/ ouvido. */
    ESP_LOGI(TAG, "beep 440 Hz %d ms no falante", ms);
    const int total = BEEP_RATE * ms / 1000;
    int16_t buf[512];           /* 256 amostras estéreo */
    int pos = 0;
    while (pos < total) {
        int n = total - pos < 256 ? total - pos : 256;
        for (int i = 0; i < n; i++) {
            /* envelope simples p/ não clicar: 5 ms de rampa nas pontas */
            int g = BOARD_BEEP_GAIN;
            if (pos + i < BEEP_RATE / 200) g = (pos + i) * BOARD_BEEP_GAIN / (BEEP_RATE / 200);
            int rem = total - (pos + i);
            if (rem < BEEP_RATE / 200) g = rem * BOARD_BEEP_GAIN / (BEEP_RATE / 200);
            int16_t v = (int16_t)(sinf(2.0f * (float)M_PI * 440.0f *
                                       (float)(pos + i) / (float)BEEP_RATE) * (float)g);
            buf[i * 2] = v;
            buf[i * 2 + 1] = v;
        }
        if (esp_codec_dev_write(s_dev, buf, (uint32_t)n * 4) != ESP_OK) break;
        pos += n;
    }
    chan_close();
    if (need_suspend) s_suspend_req = false;   /* player reabre e segue */
    xSemaphoreGive(s_beep_mtx);
    return ESP_OK;
}

/* ---------------- player local (SOM.1) ---------------- */
static void do_play_local(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { set_state("erro: arquivo não abre"); return; }
    wav_info_t w;
    if (!wav_parse(f, &w)) {
        fclose(f); set_state("erro: só WAV PCM16"); return;
    }
    if (w.bits != 16 || (w.channels != 1 && w.channels != 2)) {
        fclose(f); set_state("erro: WAV 16 bits 1-2 canais"); return;
    }
    if (w.sample_rate != 44100 && w.sample_rate != 48000) {
        fclose(f);
        set_state("erro: taxa não suportada (44k1/48k no round 1)");
        ESP_LOGW(TAG, "WAV %lu Hz — recusado no round 1 do SOM.1",
                 (unsigned long)w.sample_rate);
        return;
    }
    s_playing = true;
    s_paused = false;
    s_stop_req = false;
    set_state("tocando");
    ESP_LOGI(TAG, "falante: \"%s\" (%lu Hz, %u ch)", s_track,
             (unsigned long)w.sample_rate, (unsigned)w.channels);
    fseek(f, w.data_off, SEEK_SET);
    uint32_t left = w.data_len;
    uint8_t raw[4096];
    static int16_t out[4096];   /* estático: mono expande p/ 2x na pilha não */
    bool open_now = false;

    while (!s_stop_req && left > 0) {
        /* pause: solta o codec (beep passa livre) e espera */
        while (s_paused && !s_stop_req) {
            if (open_now) { chan_close(); open_now = false; }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        /* beep pedindo o codec? solta entre chunks e avisa */
        if (s_suspend_req) {
            if (open_now) { chan_close(); open_now = false; }
            s_suspended = true;
            while (s_suspend_req && !s_stop_req)
                vTaskDelay(pdMS_TO_TICKS(10));
            s_suspended = false;
            if (s_stop_req) break;
        }
        if (s_stop_req) break;
        if (!open_now) { chan_open(w.sample_rate); open_now = true; }

        uint32_t n = left < sizeof(raw) ? left : (uint32_t)sizeof(raw);
        if (w.channels == 1) n = (n / 4) * 2;      /* lê meio p/ expandir */
        if (fread(raw, 1, n, f) != n) break;
        uint32_t wbytes = n;
        const void *wptr = raw;
        if (w.channels == 1) {
            for (uint32_t i = 0; i < n / 2; i++) {
                int16_t v = (int16_t)((uint16_t)raw[2 * i] |
                                      ((uint16_t)raw[2 * i + 1] << 8));
                out[2 * i] = v;
                out[2 * i + 1] = v;
            }
            wptr = out;
            wbytes = n * 2;
        }
        int wfail = 0;
        for (;;) {
            esp_err_t e = esp_codec_dev_write(s_dev, (void *)wptr, (int)wbytes);
            if (e == ESP_OK) break;
            if (s_stop_req) goto play_end;
            if (!s_paused && !s_suspend_req && ++wfail >= 5) {
                ESP_LOGW(TAG, "write local: %s (%d retries) — fim",
                         esp_err_to_name(e), wfail);
                goto play_end;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        left -= n;
    }
play_end:
    if (open_now) chan_close();
    s_playing = false;
    s_paused = false;
    fclose(f);
    const bool ended = !s_stop_req && left == 0;
    s_stop_req = false;
    set_state(ended ? "fim da faixa" : "parado");
}

static void spk_play_task(void *arg)
{
    (void)arg;
    cmd_t c;
    for (;;) {
        if (xQueueReceive(s_q, &c, portMAX_DELAY) != pdTRUE) continue;
        if (c.op == 1) do_play_local(c.path);
    }
}

esp_err_t audio_i2s_play(const char *path)
{
    if (!s_q) return ESP_ERR_INVALID_STATE;
    if (s_playing) s_stop_req = true;   /* troca direta para a atual agora */
    const char *base = strrchr(path, '/');
    snprintf(s_track, sizeof(s_track), "%s", base ? base + 1 : path);
    notify();
    cmd_t c = { .op = 1 };
    snprintf(c.path, sizeof(c.path), "%s", path);
    xQueueReset(s_q);
    return xQueueSend(s_q, &c, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}

void audio_i2s_stop(void)
{
    if (!s_q) return;
    if (!s_playing) {
        xQueueReset(s_q);
        set_state("parado");
        return;
    }
    s_stop_req = true;
}

void audio_i2s_pause(void)
{
    if (!s_playing || s_paused) return;
    s_paused = true;
    set_state("pausado");
}

void audio_i2s_resume(void)
{
    if (!s_playing || !s_paused) return;
    s_paused = false;
    set_state("tocando");
}

bool audio_i2s_playing(void) { return s_playing; }
bool audio_i2s_paused(void) { return s_paused; }
const char *audio_i2s_track(void) { return s_track; }
const char *audio_i2s_state(void) { return s_state; }
void audio_i2s_set_event_cb(audio_i2s_event_cb cb, void *ctx)
{
    s_cb = cb; s_ctx = ctx;
}
