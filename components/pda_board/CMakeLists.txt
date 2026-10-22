# pda_board — BSP da GUITION JC4880P443C_I_W (ESP32-P4).
#
# Camada mais baixa do firmware: pinagem reconciliada (board_config.h),
# painel MIPI-DSI ST7701S + backlight PWM, touch GT911 (I2C compartilhado)
# e armazenamento (SD 4-bit FatFS + LittleFS interno + raiz lógica /pda).
#
# Regra de camada (docs/ARCHITECTURE.md): pda_board NÃO conhece nenhum
# serviço acima (power/script/usb/app). Onde a v1 chamava power_mgmt
# direto (touch cego no standby), a v2 usa inversão de dependência:
# board_touch_set_blind_provider(), registrado pelo app_main.
idf_component_register(
    SRCS
        "display_init.c"
        "touch_init.c"
        "storage_init.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_lcd
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_ledc
        fatfs
        sdmmc
        # dependências gerenciadas (idf_component.yml) explícitas como
        # REQUIRES públicos: headers visíveis p/ quem depende de pda_board
        # (main inclui esp_lcd_touch.h via touch_init.h p/ sleep do GT911).
        espressif__esp_lcd_st7701
        espressif__esp_lcd_touch
        espressif__esp_lcd_touch_gt911
        joltwallet__littlefs
)
