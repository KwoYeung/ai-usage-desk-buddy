#pragma once
#include <stdbool.h>
#include <stdint.h>

void buddy_sound_set_allowed(bool allowed);
void buddy_sound_set_test_allowed(bool allowed);
void buddy_sound_set_tone(uint8_t tone);
void buddy_sound_set_volume_level(uint8_t level);
void buddy_sound_notify_test(void);
void buddy_sound_notify(void);
void buddy_sound_notify_connected(void);
uint32_t buddy_sound_stage(void);
uint32_t buddy_sound_play_count(void);
