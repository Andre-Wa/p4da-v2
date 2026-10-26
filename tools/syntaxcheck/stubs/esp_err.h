/* stub de esp_err.h p/ syntaxcheck da camada app no host (tools/syntaxcheck).
 * Base: stub do hosttest da v1 + códigos NVS usados no bring-up. */
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NVS_NO_FREE_PAGES 0x1100
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1101
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_NOT_FINISHED 0x109
static inline const char *esp_err_to_name(esp_err_t e) { (void)e; return "ESP_ERR_STUB"; }

/* IDF real: aborta com log em caso de erro. No syntaxcheck: avalia e ignora. */
#define ESP_ERROR_CHECK(x)                                    \
    do {                                                      \
        esp_err_t err_rc_ = (x);                              \
        (void)err_rc_;                                        \
    } while (0)
