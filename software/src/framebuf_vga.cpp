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

#include "global.h"
#include "framebuf_vga.h"

extern "C"
{
#include "framebuf.h"
#include "font.h"
#include "config.h"
}


static uint8_t *charbuf = NULL;
static sSegm*   textSeg = NULL;

// defined in framebuf.c
extern int16_t framebuf_flash_counter;
extern uint8_t framebuf_flash_color;


void framebuf_vga_charmemset(uint32_t idx, uint8_t c, uint8_t a, uint8_t fg, uint8_t bg, size_t n)
{
  uint32_t w = c + (a<<8) + (bg << 16) + (fg << 24);
  uint32_t *buf = (uint32_t *) (charbuf + idx*4);
  for(size_t i=0; i<n; i++) buf[i] = w;
}


void framebuf_vga_charmemmove(uint32_t toidx, uint32_t fromidx, size_t n)
{
  memmove(charbuf+toidx*4, charbuf+fromidx*4, n*4);
}


void framebuf_vga_set_char(uint32_t idx, uint8_t c)
{
  charbuf[idx*4] = c;
}


uint8_t framebuf_vga_get_char(uint32_t idx)
{
  return charbuf[idx*4];
}


void framebuf_vga_set_attr(uint32_t idx, uint8_t a)
{
  charbuf[idx*4+1] = a;
}


uint8_t framebuf_vga_get_attr(uint32_t idx)
{
  return charbuf[idx*4 + 1];
}


void framebuf_vga_invert()
{
  uint32_t s = MAX_COLS*MAX_ROWS*4;
  for(uint32_t i=0; i<s; i+=4)
    {
      uint8_t c    = charbuf[i+2];
      charbuf[i+2] = charbuf[i+3];
      charbuf[i+3] = c;
    }
}


void framebuf_vga_set_color(uint32_t idx, uint8_t fg, uint8_t bg)
{
  charbuf[idx*4 + 2] = bg;
  charbuf[idx*4 + 3] = fg;
}


void framebuf_vga_get_color(uint32_t idx, uint8_t *fg, uint8_t *bg)
{
  *bg = charbuf[idx*4 + 2];
  *fg = charbuf[idx*4 + 3];
}


void framebuf_vga_set_char_and_attr(uint32_t idx, uint32_t c)
{
  ((uint32_t *) charbuf)[idx] = c;
}


uint32_t framebuf_vga_get_char_and_attr(uint32_t idx)
{
  return ((uint32_t *) charbuf)[idx];
}


static void framebuf_vga_new_frame()
{
  static uint32_t par, par2;
  static int frameCtr = 0;

  if( framebuf_flash_counter<0 )
    {
      par  = textSeg->par;
      par2 = textSeg->par2;
      textSeg->form = GF_COLOR;
      textSeg->par  = framebuf_flash_color | (framebuf_flash_color<<8) | (framebuf_flash_color<<16) | (framebuf_flash_color<<24);
      textSeg->par2 = framebuf_flash_color | (framebuf_flash_color<<8) | (framebuf_flash_color<<16) | (framebuf_flash_color<<24);
      framebuf_flash_counter = -framebuf_flash_counter;
    }
  else if( framebuf_flash_counter>0 )
    {
      if( --framebuf_flash_counter == 0 )
        {          
          textSeg->form = GF_CTEXT;
          textSeg->par  = par;
          textSeg->par2 = par2;
        }
    }
  else if( ++frameCtr>=config_get_screen_blink_period()/2 )
    {
      if( textSeg->par == (uint32_t) font_get_data_blinkon() )
        textSeg->par = (uint32_t) font_get_data_blinkoff();
      else
        textSeg->par  = (uint32_t) font_get_data_blinkon();
      
      frameCtr = 0;
    }
  
  textSeg->par3 = font_get_char_height();
}


void framebuf_vga_init(uint8_t *databuf, uint8_t *rowattr)
{
  charbuf = databuf;
  
  // run VGA core
  multicore_launch_core1(VgaCore);
  
  // setup videomode
  VgaCfgDef(&Cfg);           // get default configuration
  Cfg.video = &VideoVGA;     // video timings
  Cfg.width = FRAME_WIDTH;   // screen width
  Cfg.height = FRAME_HEIGHT; // screen height
  VgaCfg(&Cfg, &Vmode);      // calculate videomode setup
  
  // initialize base layer 0
  ScreenClear(pScreen);
  sStrip* t = ScreenAddStrip(pScreen, FRAME_HEIGHT);
  textSeg = ScreenAddSegm(t, FRAME_WIDTH);
  ScreenSegmCText(textSeg, charbuf, font_get_data_blinkon(), font_get_char_height(), MAX_COLS*4);
  textSeg->par2 = (uint32_t) rowattr;
  VgaSetNewFrameCallback(framebuf_vga_new_frame);
  
  // initialize system clock
  set_sys_clock_pll(Vmode.vco*1000, Vmode.pd1, Vmode.pd2);
  
  // initialize videomode
  VgaInitReq(&Vmode);
}
