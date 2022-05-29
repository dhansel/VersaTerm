#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "tusb.h"

// key values for control keys in ASCII range
#define KEY_BACKSPACE   0x08
#define KEY_TAB         0x09
#define KEY_ENTER       0x0d
#define KEY_ESC         0x1b
#define KEY_DELETE      0x7f

// key values for special (non-ASCII) control keys (returned by keyboard_map_key_ascii)
#define KEY_UP		0x80
#define KEY_DOWN	0x81
#define KEY_LEFT	0x82
#define KEY_RIGHT	0x83
#define KEY_INSERT	0x84
#define KEY_HOME	0x85
#define KEY_END		0x86
#define KEY_PUP		0x87
#define KEY_PDOWN	0x88
#define KEY_PAUSE       0x89
#define KEY_PRSCRN      0x8a
#define KEY_F1      	0x8c
#define KEY_F2      	0x8d
#define KEY_F3      	0x8e
#define KEY_F4      	0x8f
#define KEY_F5      	0x90
#define KEY_F6      	0x91
#define KEY_F7      	0x92
#define KEY_F8      	0x93
#define KEY_F9      	0x94
#define KEY_F10     	0x95
#define KEY_F11     	0x96
#define KEY_F12     	0x97


typedef struct KeyboardMacroInfoStruct 
{
  uint16_t  key, data_len;
  uint16_t *data;
  uint16_t *next;
} KeyboardMacroInfo;


void    keyboard_init();
void    keyboard_apply_settings();
void    keyboard_task();
void    keyboard_key_change(uint8_t key, bool make);
const char *keyboard_get_keyname(uint16_t key);

size_t   keyboard_num_keypress();
uint16_t keyboard_read_keypress();
uint8_t  keyboard_get_led_status();
uint8_t  keyboard_get_current_modifiers();
bool     keyboard_ctrl_pressed(uint16_t key);
bool     keyboard_alt_pressed(uint16_t key);
bool     keyboard_shift_pressed(uint16_t key);
uint8_t  keyboard_map_key_ascii(uint16_t key, bool *isaltcode);

void    keyboard_macro_record_start();
bool    keyboard_macro_record_stop();
void    keyboard_macro_record_startstop();
bool    keyboard_macro_recording();
bool    keyboard_macro_delete(uint16_t key);
bool    keyboard_macro_getfirst(KeyboardMacroInfo *info);
bool    keyboard_macro_getnext(KeyboardMacroInfo *info);
void    keyboard_macro_clearall();

void    keyboard_keymap_map_start();
bool    keyboard_keymap_mapping(uint8_t *fromKey);

#endif
