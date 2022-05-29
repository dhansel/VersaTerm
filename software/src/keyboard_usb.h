#ifndef KEYBOARD_USB_H
#define KEYBOARD_USB_H

void keyboard_usb_set_led_status(uint8_t leds);
void keyboard_usb_init();
void keyboard_usb_task();
void keyboard_usb_apply_settings();

#endif
