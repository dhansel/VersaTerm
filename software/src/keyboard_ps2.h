#ifndef KEYBOARD_PS2_H
#define	KEYBOARD_PS2_H

void keyboard_ps2_set_led_status(uint8_t leds);
void keyboard_ps2_task();
void keyboard_ps2_init();
void keyboard_ps2_apply_settings();

#endif

