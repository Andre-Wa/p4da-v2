/* stub de freertos/task.h p/ syntaxcheck (base: hosttest v1 +
 * xTaskCreateWithCaps, usado pela camada app p/ pilhas em PSRAM). */
#pragma once
#include "FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

static inline BaseType_t xTaskCreate(TaskFunction_t fn, const char *name,
                                     uint32_t stack, void *arg,
                                     UBaseType_t prio, TaskHandle_t *out)
{
    (void)fn; (void)name; (void)stack; (void)arg; (void)prio;
    if (out) *out = NULL;
    return pdPASS;
}

static inline BaseType_t xTaskCreateWithCaps(TaskFunction_t fn, const char *name,
                                             uint32_t stack, void *arg,
                                             UBaseType_t prio, TaskHandle_t *out,
                                             uint32_t caps)
{
    (void)caps;
    return xTaskCreate(fn, name, stack, arg, prio, out);
}

static inline void vTaskDelay(TickType_t t) { (void)t; }
static inline void vTaskDelete(TaskHandle_t h) { (void)h; }
static inline TaskHandle_t xTaskGetCurrentTaskHandle(void) { return NULL; }
