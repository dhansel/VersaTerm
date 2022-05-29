#ifndef FRAMEBUF_H
#define FRAMEBUF_H

#include "pico/stdlib.h"
#include "font.h"

#define FRAME_WIDTH  640
#define FRAME_HEIGHT 480
#define MAX_COLS     ((uint32_t) (FRAME_WIDTH / FONT_CHAR_WIDTH))
#define MAX_ROWS     ((uint32_t) (FRAME_HEIGHT / font_get_char_height()))

#define ATTR_UNDERLINE 0x01
#define ATTR_BLINK     0x02
#define ATTR_BOLD      0x04
#define ATTR_INVERSE   0x08

#define ROW_ATTR_DBL_WIDTH       0x01
#define ROW_ATTR_DBL_HEIGHT_TOP  0x02
#define ROW_ATTR_DBL_HEIGHT_BOT  0x04

void framebuf_init(bool forceDVI);
void framebuf_apply_settings();
bool framebuf_is_dvi();

void framebuf_set_char(uint8_t column, uint8_t row, uint8_t character);
uint8_t framebuf_get_char(uint8_t column, uint8_t row);

void    framebuf_set_attr(uint8_t column, uint8_t row, uint8_t a);
uint8_t framebuf_get_attr(uint8_t column, uint8_t row);

void    framebuf_set_row_attr(uint8_t row, uint8_t a);
uint8_t framebuf_get_row_attr(uint8_t row);

void framebuf_set_color(uint8_t column, uint8_t row, uint8_t foreground, uint8_t background);
void framebuf_set_fullcolor(uint8_t x, uint8_t y, uint8_t fg, uint8_t bg);

void framebuf_fill_screen(char character, uint8_t fg, uint8_t bg);
void framebuf_fill_region(uint8_t col_start, uint8_t row_start, uint8_t col_end, uint8_t row_end, char character, uint8_t fg, uint8_t bg);

void framebuf_scroll_screen(int8_t n, uint8_t fg, uint8_t bg);
void framebuf_scroll_region(uint8_t row_start, uint8_t row_end, int8_t n, uint8_t fg, uint8_t bg);

void framebuf_insert(uint8_t x, uint8_t y, uint8_t n, uint8_t fg, uint8_t bg);
void framebuf_delete(uint8_t x, uint8_t y, uint8_t n, uint8_t fg, uint8_t bg);

uint8_t framebuf_get_nrows();
uint8_t framebuf_get_ncols(int row);

void framebuf_set_scroll_delay(uint16_t ms);
void framebuf_set_screen_size(uint8_t ncols, uint8_t nrows);
void framebuf_set_screen_inverted(bool invert);
void framebuf_flash_screen(uint8_t color, uint8_t nframes);

#endif
