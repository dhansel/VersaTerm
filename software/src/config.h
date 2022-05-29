#ifndef CONFIG_H
#define CONFIG_H

#include "pico/stdlib.h"

#define CFG_DISPTYPE_AUTODETECT 0
#define CFG_DISPTYPE_DVI        1
#define CFG_DISPTYPE_VGA        2

#define CFG_USBMODE_OFF         0
#define CFG_USBMODE_DEVICE      1
#define CFG_USBMODE_HOST        2
#define CFG_USBMODE_AUTODETECT  3

#define CFG_TTYPE_VT102   0
#define CFG_TTYPE_VT52    1
#define CFG_TTYPE_PETSCII 2

uint32_t config_get_serial_baud();
uint8_t  config_get_serial_bits();
char     config_get_serial_parity();
uint8_t  config_get_serial_stopbits();
uint8_t  config_get_serial_ctsmode();
uint8_t  config_get_serial_rtsmode();
uint8_t  config_get_serial_xonxoff();
uint16_t config_get_serial_blink();

uint8_t config_get_screen_rows();
uint8_t config_get_screen_cols();
bool    config_get_screen_dblchars();
uint8_t config_get_screen_font_normal();
uint8_t config_get_screen_font_bold();
uint8_t config_get_screen_blink_period();
uint8_t config_get_screen_display();
bool    config_get_screen_monochrome();
uint8_t config_get_screen_monochrome_backgroundcolor(bool dvi);
uint8_t config_get_screen_monochrome_textcolor_normal(bool dvi);
uint8_t config_get_screen_monochrome_textcolor_bold(bool dvi);
uint8_t config_get_screen_color(uint8_t color, bool dvi);

uint8_t config_get_terminal_type();
uint8_t config_get_terminal_localecho();
uint8_t config_get_terminal_cursortype();
uint8_t config_get_terminal_cr();
uint8_t config_get_terminal_lf();
uint8_t config_get_terminal_bs();
uint8_t config_get_terminal_del();
bool    config_get_terminal_clearBit7();
bool    config_get_terminal_uppercase();
uint8_t config_get_terminal_default_fg();
uint8_t config_get_terminal_default_bg();
uint8_t config_get_terminal_default_attr();
const char *config_get_terminal_answerback();

uint8_t config_get_keyboard_layout();
uint8_t config_get_keyboard_enter();
uint8_t config_get_keyboard_backspace();
uint8_t config_get_keyboard_delete();
uint8_t config_get_keyboard_scroll_lock();
uint8_t config_get_keyboard_repeat_delay();
uint8_t config_get_keyboard_repeat_rate();
uint16_t config_get_keyboard_repeat_delay_ms();
uint16_t config_get_keyboard_repeat_rate_mHz();
uint8_t  *config_get_keyboard_user_mapping();
uint8_t  *config_get_keyboard_macros_start();

uint16_t config_get_audible_bell_frequency();
uint16_t config_get_audible_bell_volume();
uint16_t config_get_audible_bell_duration();
uint16_t config_get_visual_bell_color();
uint8_t  config_get_visual_bell_duration();

uint8_t config_get_usb_mode();
uint8_t config_get_usb_cdcmode();

void config_show_splash();
bool config_load(uint8_t n);
bool config_menu_active();
void config_init();
int  config_menu();

#endif
