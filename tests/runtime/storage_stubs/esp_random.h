#pragma once
#include <stddef.h>
#include <stdint.h>
void esp_fill_random(void *buffer, size_t size);
uint32_t esp_random(void);
