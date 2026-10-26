# pda_audio — saída de som: falante local (ES8311+NS4150) e USB (UAC1).
#
# Camada L3 do p4da-v2. Conteúdo: wav_parse (parser PCM16 puro, testado
# no host), audio_i2s (beep da v1 M5c.1 + player local SOM.1 c/ handshake
# beep×música + volume/mute SOM.3), audio_uac (player UAC da v1 M5a,
# 1:1), audio_route (SOM.2 auto/interno/USB, RAM-only).
#
# Decisões em docs/AUDIO.md (dono do subsistema); quirks de device
# (H3S: 100%=mute, 0% audível, GET range timeouta) vivem nos comentários
# do audio_uac.c desde a v1 — não "consertar" sem medição.
idf_component_register(
    SRCS
        "wav_parse.c"
        "audio_i2s.c"
        "audio_uac.c"
        "audio_route.c"
        "audio_vol.c"
    INCLUDE_DIRS "include"
    REQUIRES
        nvs_flash               # audio_vol: volumes por fonte persistidos
        esp_timer               # debounce de gravação do volume
        pda_board               # board_config.h (pinos I2S/PA) + board_i2c_bus()
        pda_usb                 # usb_host_state.h (host lib instalada p/ UAC)
        usb                     # usb/usb_host.h (client UAC)
        esp_driver_i2s          # driver/i2s_std.h
        espressif__esp_codec_dev
        espressif__usb_host_uac
)
