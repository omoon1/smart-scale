#include "config.hxx"
#ifndef EXPECT_LCD_SLEEP
#define EXPECT_LCD_SLEEP 0
#endif
static_assert(smart_scale::config::lcd_sleep_enabled == (EXPECT_LCD_SLEEP != 0));
int main() {}
