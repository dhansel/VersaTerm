#ifndef FONT_H
#define FONT_H

#define FONT_CHAR_WIDTH  8

#define FONT_ID_NONE     0
#define FONT_ID_CGA      1
#define FONT_ID_EGA      2
#define FONT_ID_VGA      3
#define FONT_ID_TERM     4
#define FONT_ID_TERMBOLD 5
#define FONT_ID_PETSCII  6
#define FONT_ID_USER1    7
#define FONT_ID_USER2    8
#define FONT_ID_USER3    9
#define FONT_ID_USER4    10

bool           font_have_boldfont();
const uint8_t *font_get_data_blinkon();
const uint8_t *font_get_data_blinkoff();

uint8_t        font_get_char_height();
bool           font_get_font_info(uint8_t fontNum, uint32_t *bitmapWidth, uint32_t *bitmapHeight, uint8_t *charHeight, uint8_t *underlineRow);
const char    *font_get_name(uint8_t fontNum);
const uint8_t *font_get_bmpdata(uint8_t fontNum);
const uint8_t  font_map_graphics_char(uint8_t c, bool boldFont);

const uint8_t *font_get_graphics_char_mapping(uint8_t fontNum);
bool font_set_graphics_char_mapping(uint8_t fontNum, const uint8_t *mapping);
void font_set_underline_row(uint8_t fontNum, uint8_t underlineRow);
void font_set_name(uint8_t fontNum, const char *name);

const char *font_receive_fontdata(uint8_t fontNum);

bool font_apply_font(uint8_t font, bool bold);

void font_apply_settings();
void font_init();

#endif
