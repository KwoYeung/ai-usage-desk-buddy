#include <assert.h>
#include <string.h>

#include "buddy_deepseek.h"

static void test_amounts_keep_two_decimals(void)
{
    char value[32];

    buddy_deepseek_amount(value, sizeof(value), 0);
    assert(strcmp(value, "0.00") == 0);
    buddy_deepseek_amount(value, sizeof(value), 5);
    assert(strcmp(value, "0.05") == 0);
    buddy_deepseek_amount(value, sizeof(value), 100);
    assert(strcmp(value, "1.00") == 0);
    buddy_deepseek_amount(value, sizeof(value), 1234);
    assert(strcmp(value, "12.34") == 0);
    buddy_deepseek_amount(value, sizeof(value), 999999999ULL);
    assert(strcmp(value, "9999999.99") == 0);
}

static void test_age_uses_bounded_units(void)
{
    char value[40];
    const uint64_t received = 10000U;
    const uint64_t now = 10000U;

    buddy_deepseek_age(value, sizeof(value), 0U, 100000, received, now);
    assert(strcmp(value, "更新：--") == 0);
    buddy_deepseek_age(value, sizeof(value), 500U, 0, received, now);
    assert(strcmp(value, "更新：--") == 0);
    buddy_deepseek_age(value, sizeof(value), 100000U, 100000, received, now);
    assert(strcmp(value, "更新：<1 分钟") == 0);
    buddy_deepseek_age(value, sizeof(value), 99941U, 100000, received, now);
    assert(strcmp(value, "更新：<1 分钟") == 0);
    buddy_deepseek_age(value, sizeof(value), 99940U, 100000, received, now);
    assert(strcmp(value, "更新：1 分钟前") == 0);
    buddy_deepseek_age(value, sizeof(value), 100000U - 3600U, 100000, received, now);
    assert(strcmp(value, "更新：1 小时前") == 0);
    buddy_deepseek_age(value, sizeof(value), 100000U - 86400U, 100000, received, now);
    assert(strcmp(value, "更新：1 天前") == 0);
    /* Future timestamps from clock skew must not render a huge age. */
    buddy_deepseek_age(value, sizeof(value), 200000U, 100000, received, now);
    assert(strcmp(value, "更新：<1 分钟") == 0);
    /* Elapsed milliseconds since the last time sync advance the age. */
    buddy_deepseek_age(value, sizeof(value), 100000U, 100000, received, received + 125000U);
    assert(strcmp(value, "更新：2 分钟前") == 0);
}

int main(void)
{
    test_amounts_keep_two_decimals();
    test_age_uses_bounded_units();
    return 0;
}
