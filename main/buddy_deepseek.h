#pragma once

#include <stddef.h>
#include <stdint.h>

/* Format an integer number of cents as a fixed two-decimal amount. */
void buddy_deepseek_amount(char *destination, size_t size, uint64_t cents);

/* Render the age of the last successful DeepSeek balance refresh. */
void buddy_deepseek_age(char *destination, size_t size, uint64_t updated_at,
                        int64_t epoch_seconds, uint64_t received_ms, uint64_t now_ms);
