#pragma once

#include "esp_lcd_touch.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Inicializa o barramento I2C compartilhado e o controlador de touch
 * GT911. Devolve o handle "cru" (esp_lcd_touch_handle_t) que o
 * slint_esp_init() espera.
 */
esp_err_t board_touch_init(esp_lcd_touch_handle_t *out_touch);

/**
 * v2 (inversão de dependência): o proxy de "toque cego" do standby não
 * chama mais power_mgmt direto (na v1 o BSP dependia do serviço de
 * energia — ciclo board→power). O composition root (app_main) registra
 * aqui o predicado do serviço; sem provedor, o touch nunca é cegado.
 */
typedef bool (*board_touch_blind_fn)(void);
void board_touch_set_blind_provider(board_touch_blind_fn fn);

/** M5c.1: o barramento I2C compartilhado (touch + ES8311). */
i2c_master_bus_handle_t board_i2c_bus(void);

#ifdef __cplusplus
}
#endif
