/* stub de freertos/queue.h p/ syntaxcheck. */
#pragma once
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
static inline QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t sz)
{ (void)len; (void)sz; return (void *)1; }
static inline BaseType_t xQueueSend(QueueHandle_t q, const void *it, TickType_t t)
{ (void)q; (void)it; (void)t; return pdTRUE; }
static inline BaseType_t xQueueReceive(QueueHandle_t q, void *it, TickType_t t)
{ (void)q; (void)it; (void)t; return pdFALSE; }
static inline BaseType_t xQueueReset(QueueHandle_t q) { (void)q; return pdTRUE; }
