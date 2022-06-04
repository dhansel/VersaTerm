// -----------------------------------------------------------------------------
// VersaTerm - A versatile serial terminal
// Copyright (C) 2022 David Hansel
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
// -----------------------------------------------------------------------------

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
