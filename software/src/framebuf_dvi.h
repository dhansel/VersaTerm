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

#ifndef FRAMEBUF_DVI_H
#define FRAMEBUF_DVI_H

void framebuf_dvi_init(uint8_t *databuf, uint8_t *rowattr);

void framebuf_dvi_charmemset(uint32_t idx, uint8_t c, uint8_t a, uint8_t fg, uint8_t bg, size_t n);
void framebuf_dvi_charmemmove(uint32_t toidx, uint32_t fromidx, size_t n);

uint8_t framebuf_dvi_get_char(uint32_t idx);
void    framebuf_dvi_set_char(uint32_t idx, uint8_t c);

uint8_t framebuf_dvi_get_attr(uint32_t idx);
void    framebuf_dvi_set_attr(uint32_t idx, uint8_t a);

// Pixel format RGB222
void framebuf_dvi_set_color(uint32_t char_index, uint8_t fg, uint8_t bg);
void framebuf_dvi_get_color(uint32_t char_index, uint8_t *fg, uint8_t *bg);
void framebuf_dvi_invert();

void framebuf_dvi_set_char_and_attr(uint32_t idx, uint32_t c);
uint32_t framebuf_dvi_get_char_and_attr(uint32_t idx);

void framebuf_dvi_flash_screen(uint8_t color, uint8_t nframes);

#endif
