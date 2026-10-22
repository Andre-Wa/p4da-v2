/* stub de esp_heap_caps.h p/ syntaxcheck. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define MALLOC_CAP_SPIRAM (1 << 22)
#define MALLOC_CAP_INTERNAL (1 << 21)

size_t heap_caps_get_free_size(uint32_t caps);
