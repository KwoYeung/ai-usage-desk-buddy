#include "buddy_deepseek.h"

#include <stdio.h>

void buddy_deepseek_amount(char *destination, size_t size, uint64_t cents)
{
    if (destination == NULL || size == 0U) {
        return;
    }
    snprintf(destination, size, "%llu.%02llu",
             (unsigned long long)(cents / 100U),
             (unsigned long long)(cents % 100U));
}

void buddy_deepseek_age(char *destination, size_t size, uint64_t updated_at,
                        int64_t epoch_seconds, uint64_t received_ms, uint64_t now_ms)
{
    uint64_t age;
    uint64_t now;

    if (destination == NULL || size == 0U) {
        return;
    }
    if (updated_at == 0U || epoch_seconds <= 0) {
        snprintf(destination, size, "更新：--");
        return;
    }
    now = (uint64_t)epoch_seconds +
          (now_ms >= received_ms ? (now_ms - received_ms) / 1000U : 0U);
    age = now > updated_at ? now - updated_at : 0U;
    if (age < 60U) {
        snprintf(destination, size, "更新：<1 分钟");
    } else if (age < 3600U) {
        snprintf(destination, size, "更新：%llu 分钟前",
                 (unsigned long long)(age / 60U));
    } else if (age < 86400U) {
        snprintf(destination, size, "更新：%llu 小时前",
                 (unsigned long long)(age / 3600U));
    } else {
        snprintf(destination, size, "更新：%llu 天前",
                 (unsigned long long)(age / 86400U));
    }
}
