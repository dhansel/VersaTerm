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

#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/irq.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "hardware/structs/bus_ctrl.h"
#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "tmds_encode_font_2bpp.h"

#include "pins.h"
#include "framebuf.h"
#include "framebuf_dvi.h"
#include "font.h"
#include "config.h"

#define DVI_TIMING             dvi_timing_640x480p_60hz
#define COLOR_PLANE_SIZE_WORDS (MAX_ROWS * MAX_COLS * 4 / 32)

// defined in framebuf.c
extern int16_t framebuf_flash_counter;
extern uint8_t framebuf_flash_color;

struct dvi_inst dvi0;
static uint16_t *charbuf  = NULL;
static uint32_t *colorbuf = NULL;
static uint8_t  *rowattr  = NULL;


void framebuf_dvi_charmemset(uint32_t idx, uint8_t c, uint8_t a, uint8_t fg, uint8_t bg, size_t n)
{
  uint16_t v = c | (a<<8);
  for(size_t i=0; i<n; i++) charbuf[idx+i] = v;

  if( (idx&1)==1 ) { framebuf_dvi_set_color(idx, fg, bg); idx++; n--; }
  if( (n&1)==1   ) { framebuf_dvi_set_color(idx+n, fg, bg); n--; }
  
  if( n>0 )
    for(int plane=0; plane<3; plane++) 
      {
        uint8_t fg_bg_combined = (fg & 0x3) | ((bg << 2) & 0xc);
        fg_bg_combined |= fg_bg_combined << 4;
        memset((uint8_t *) colorbuf+plane*COLOR_PLANE_SIZE_WORDS*sizeof(uint32_t)+idx/2, fg_bg_combined, n/2);
        fg >>= 2;
        bg >>= 2;
      }
}


void framebuf_dvi_charmemmove(uint32_t toidx, uint32_t fromidx, size_t n)
{
  memmove(charbuf+toidx, charbuf+fromidx, n*2);
  if( (fromidx&1)==0 && (toidx&1)==0 && (n&1)==0 )
    {
      for(int plane = 0; plane < 3; plane++)
        memmove((uint8_t *) colorbuf+plane*COLOR_PLANE_SIZE_WORDS*sizeof(uint32_t)+toidx/2,
                (uint8_t *) colorbuf+plane*COLOR_PLANE_SIZE_WORDS*sizeof(uint32_t)+fromidx/2,
                n/2);
    }
  else if( fromidx>toidx )
    {
      uint8_t fg, bg;
      for(int i=0; i<n; i++)
        {
          framebuf_dvi_get_color(fromidx+i, &fg, &bg);
          framebuf_dvi_set_color(toidx+i, fg, bg);
        }
    }
  else if( fromidx<toidx )
    {
      uint8_t fg, bg;
      for(int i=n-1; i>=0; i--)
        {
          framebuf_dvi_get_color(fromidx+i, &fg, &bg);
          framebuf_dvi_set_color(toidx+i, fg, bg);
        }
    }
}


uint8_t framebuf_dvi_get_char(uint32_t idx)
{
  return charbuf[idx] & 255;
}


void framebuf_dvi_set_char(uint32_t idx, uint8_t c)
{
  charbuf[idx] = (charbuf[idx] & 0xFF00) | c;
}


uint8_t framebuf_dvi_get_attr(uint32_t idx)
{
  return charbuf[idx] / 256;
}

void framebuf_dvi_set_attr(uint32_t idx, uint8_t a)
{
  charbuf[idx] = (charbuf[idx] & 0x00FF) | (a<<8);
}


void framebuf_dvi_invert()
{
  uint32_t s = COLOR_PLANE_SIZE_WORDS*3;
  for(uint32_t i=0; i<s; i++)
    {
      uint32_t c = colorbuf[i];
      colorbuf[i] = ((c & 0x33333333)<<2) | ((c & 0xCCCCCCCC)>>2);
    }
}


void framebuf_dvi_set_color(uint32_t char_index, uint8_t fg, uint8_t bg)
{
  uint32_t bit_index  = (char_index % 8) * 4;
  uint32_t word_index = (char_index / 8);
  uint32_t cpw = COLOR_PLANE_SIZE_WORDS;
  uint32_t msk = ~(0x0f << bit_index);
  uint32_t *c  = colorbuf+word_index;
  uint32_t fg_bg_combined;

  fg_bg_combined = (fg & 0x03) | ((bg & 0x03) << 2);
  *c = (*c & msk) | (fg_bg_combined << bit_index);
  c += cpw;

  fg_bg_combined = ((fg & 0x0C) >> 2) | (bg & 0x0C);
  *c = (*c & msk) | (fg_bg_combined << bit_index);
  c += cpw;

  fg_bg_combined = ((fg & 0x30) >> 4) | ((bg & 0x30) >> 2);
  *c = (*c & msk) | (fg_bg_combined << bit_index);
}


void framebuf_dvi_get_color(uint32_t char_index, uint8_t *fg, uint8_t *bg)
{
  uint32_t bit_index  = (char_index % 8) * 4;
  uint32_t word_index = (char_index / 8);
  uint32_t cpw = COLOR_PLANE_SIZE_WORDS;
  uint8_t c1 = colorbuf[word_index      ] >> bit_index;
  uint8_t c2 = colorbuf[word_index+  cpw] >> bit_index;
  uint8_t c3 = colorbuf[word_index+2*cpw] >> bit_index;

  *fg = ((c1 & 0x03))      | ((c2 & 0x03) << 2) | ((c3 & 0x03) << 4);
  *bg = ((c1 & 0x0C) >> 2) | ((c2 & 0x0C))      | ((c3 & 0x0C) << 2);
}


void framebuf_dvi_set_char_and_attr(uint32_t idx, uint32_t c)
{
  charbuf[idx] = c & 0xFFFF;
  framebuf_dvi_set_color(idx, c >> 24, c >> 16);
}


uint32_t framebuf_dvi_get_char_and_attr(uint32_t idx)
{
  uint8_t fg, bg;
  framebuf_dvi_get_color(idx, &fg, &bg);
  return charbuf[idx] | (bg << 16) | (fg << 24);
}


void __not_in_flash_func(core1_main)() 
{
  uint32_t *tmdsbuf;
  dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
  dvi_start(&dvi0);

  uint8_t frameCtr = 0;
  const uint8_t* font = font_get_data_blinkon();
  static uint32_t solidcolor[MAX_COLS * 4 / 32];
  memset(solidcolor, 0, sizeof(solidcolor));

  while( true )
    {
      if( framebuf_flash_counter<0 )
        {
          framebuf_flash_counter = -framebuf_flash_counter;
          memset(solidcolor, framebuf_flash_color | (framebuf_flash_color<<2), sizeof(solidcolor));
        }
      else if( framebuf_flash_counter>0 )
        {
          if( --framebuf_flash_counter == 0 )
            memset(solidcolor, 0, sizeof(solidcolor));
        }
      else if( ++frameCtr>=config_get_screen_blink_period()/2 )
        {
          if( font == font_get_data_blinkon() )
            font = font_get_data_blinkoff();
          else
            font = font_get_data_blinkon();
          
          frameCtr = 0;
        }
      
      uint8_t  char_height               = font_get_char_height();
      uint32_t color_plane_size_words    = COLOR_PLANE_SIZE_WORDS;
      uint32_t color_plane_words_per_row = COLOR_PLANE_SIZE_WORDS / MAX_ROWS;
      uint32_t num_y                     = font_get_char_height()*MAX_ROWS;
        
      for(uint y = 0; y < FRAME_HEIGHT; ++y)
        {
          queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);

          uint row = y / char_height;
          
          void (*tmds_encode_font_2bpp)(const uint16_t *, const uint32_t *, uint32_t *, uint, const uint8_t *) = 
            (rowattr[row] & ROW_ATTR_DBL_WIDTH) ? tmds_encode_font_2bpp_dw : tmds_encode_font_2bpp_sw;

          if( rowattr[row] & ROW_ATTR_DBL_HEIGHT_TOP )
            {
              for(int plane = 0; plane < 3; ++plane) 
                tmds_encode_font_2bpp((const uint16_t*)&charbuf[row * MAX_COLS],
                                      (y<num_y&&framebuf_flash_counter==0) ? &colorbuf[row * color_plane_words_per_row + plane * color_plane_size_words] : solidcolor,
                                      tmdsbuf + plane * (FRAME_WIDTH / DVI_SYMBOLS_PER_WORD),
                                      FRAME_WIDTH,
                                      (const uint8_t*)&font[(y % char_height)/2 * 256 * 8]);
            }
          else if( rowattr[row] & ROW_ATTR_DBL_HEIGHT_BOT )
            {
              for(int plane = 0; plane < 3; ++plane) 
                tmds_encode_font_2bpp((const uint16_t*)&charbuf[row * MAX_COLS],
                                      (y<num_y&&framebuf_flash_counter==0) ? &colorbuf[row * color_plane_words_per_row + plane * color_plane_size_words] : solidcolor,
                                      tmdsbuf + plane * (FRAME_WIDTH / DVI_SYMBOLS_PER_WORD),
                                      FRAME_WIDTH,
                                      (const uint8_t*)&font[(((y % char_height)+char_height))/2 * 256 * 8]);
            }
          else
            {
              for(int plane = 0; plane < 3; ++plane) 
                tmds_encode_font_2bpp((const uint16_t*)&charbuf[row * MAX_COLS],
                                      (y<num_y&&framebuf_flash_counter==0) ? &colorbuf[row * color_plane_words_per_row + plane * color_plane_size_words] : solidcolor,
                                      tmdsbuf + plane * (FRAME_WIDTH / DVI_SYMBOLS_PER_WORD),
                                      FRAME_WIDTH,
                                      (const uint8_t*)&font[(y % char_height) * 256 * 8]);
            }
          
          queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
        }
    }
}


void framebuf_dvi_init(uint8_t *databuf, uint8_t *ra)
{
  vreg_set_voltage(VREG_VOLTAGE_1_20);
  sleep_ms(10);
  // Run system at TMDS bit clock
  set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

  charbuf  = (uint16_t *) databuf;
  colorbuf = (uint32_t *) (databuf + 60 * 80 * 2);
  rowattr  = ra;

  dvi0.timing  = &DVI_TIMING;
  dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
  dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

  hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
  multicore_launch_core1(core1_main);
}
