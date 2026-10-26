/**
 * audio_vol.h — volume POR FONTE com persistência (SOM.3+, v2.2.1).
 *
 * Pedido de campo (checklist C7 da v2.2.0): "opção para modificar o
 * volume pela interface sem depender de device externo". Modelo:
 *  - dois valores independentes: falante local e USB (como o pedido:
 *    "volume separado por fonte");
 *  - aplicação AO VIVO no backend correspondente (codec / feature unit);
 *  - persistência em NVS ("pdaaudio": vol_i / vol_u) com DEBOUNCE de
 *    3 s + trailing (slider não martela a flash; o último valor sempre
 *    chega — mesmo padrão do brilho do quick panel, M4.12 da v1).
 * Os backends não donam mais o valor: audio_i2s_set_vol /
 * audio_uac_volume_set são caminhos de APLICAÇÃO chamados daqui.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_VOL_INTERNAL = 0,   /* falante ES8311+NS4150 */
    AUDIO_VOL_UAC,            /* device USB (feature unit) */
} audio_vol_src_t;

/** Carrega do NVS (defaults: BOARD_SPK_VOL_DEFAULT / 70). Antes dos
 *  inits de audio_i2s/audio_uac (app_main). */
void audio_vol_init(void);

int  audio_vol_get(audio_vol_src_t src);
/** Clamp 0-100, aplica no backend agora, persiste com debounce. */
void audio_vol_set(audio_vol_src_t src, int pct);
void audio_vol_step(audio_vol_src_t src, int delta);

#ifdef __cplusplus
}
#endif
