/* stub de esp_lcd_touch.h p/ syntaxcheck (tipos + sleep do GT911). */
#pragma once
#include "esp_err.h"

typedef struct esp_lcd_touch_s *esp_lcd_touch_handle_t;

esp_err_t esp_lcd_touch_enter_sleep(esp_lcd_touch_handle_t tp);
esp_err_t esp_lcd_touch_exit_sleep(esp_lcd_touch_handle_t tp);
