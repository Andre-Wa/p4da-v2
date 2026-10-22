/* stub de esp_timer.h p/ syntaxcheck (assinaturas do IDF v5.5.1). */
#pragma once
#include <stdint.h>
#include "esp_err.h"

int64_t esp_timer_get_time(void);

typedef struct esp_timer *esp_timer_handle_t;

typedef struct {
    void (*callback)(void *);
    void *arg;
    const char *name;
} esp_timer_create_args_t;

esp_err_t esp_timer_create(const esp_timer_create_args_t *args,
                           esp_timer_handle_t *out_handle);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period_us);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
