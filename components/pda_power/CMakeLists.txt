# pda_power — degraus de energia (DIM -> STANDBY -> HIBERNATE).
#
# Herdado 1:1 da v1 (M4.14/M4.15 validados em HW): kill-switch do light
# sleep ativo (power_mgmt_light_sleep_active() == false até o rework
# M-power), standby robusto com CPU idle, hibernate = deep sleep com
# sessão salva no SD (volta = RESET; flag NVS). docs/POWER.md é o dono
# das decisões.
idf_component_register(
    SRCS
        "power_mgmt.c"
    INCLUDE_DIRS "include"
    REQUIRES
        pda_board           # board_config.h, display (backlight/blank), storage (shutdown/remount)
        pda_script          # pda_config.h (tempos dos degraus, wake_on_touch...)
        nvs_flash           # flag "woke_from_hibernate"
        esp_driver_gpio     # botão BOOT (GPIO35) como wake/entrada
        esp_timer
)
