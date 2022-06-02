#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include "hardware/flash.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"

#include "pins.h"
#include "terminal.h"
#include "font.h"
#include "flash.h"
#include "config.h"
#include "xmodem.h"
#include "framebuf.h"
#include "serial.h"

#ifndef _IMG_ASSET_SECTION
#define _IMG_ASSET_SECTION ".data"
#endif

#include "font_cga.h"
#include "font_ega.h"
#include "font_vga.h"
#include "font_terminus.h"
#include "font_terminus_bold.h"
#include "font_petscii.h"

#define INFLASHFUN __in_flash(".configfun") 


static const uint8_t builtin_font_graphics_char_mapping[31] =
  {0x04, // diamond/caret
   0xB1, // scatter
   0x0B, // HT
   0x0C, // FF
   0x0D, // CR
   0x0E, // LF
   0xF8, // degree symbol
   0xF1, // plusminus
   0x0F, // NL
   0x10, // VT
   0xD9, // left-top corner
   0xBF, // left-bottom corner
   0xDA, // right-bottom corner
   0xC0, // right-top corner
   0xC5, // cross
   0x11, // horizontal line 1
   0x12, // horizontal line 2
   0xC4, // horizontal line 3
   0x13, // horizontal line 4
   0x5F, // horizontal line 5
   0xC3, // right "T"
   0xB4, // left "T"
   0xC1, // top "T"
   0xC2, // bottom "T"
   0xB3, // vertical line
   0xF3, // less-equal
   0xF2, // greater-equal
   0xE3, // pi
   0x1C, // not equal
   0x9C, // pound sterling
   0xFA  // center dot
  };


struct FontInfoStruct
{
  uint32_t magic;
  uint32_t bitmapWidth;
  uint32_t bitmapHeight;
  uint8_t  charHeight;
  uint8_t  underlineRow;
  uint8_t  graphicsCharMapping[31];
  char     name[32];
  uint8_t  reserved[48];
} userFontInfo[4];


static uint8_t cur_font_normal = 0, cur_font_bold = 0, font_char_height = 16;
static uint8_t __attribute__((aligned(4), section(_IMG_ASSET_SECTION ".font"))) font_blinkoff[8*256*16];
static uint8_t __attribute__((aligned(4), section(_IMG_ASSET_SECTION ".font"))) font_blinkon[8*256*16];


bool font_have_boldfont()
{
  return cur_font_normal != cur_font_bold;
}


const uint8_t *font_get_data_blinkon()  
{ 
  return font_blinkon;  
}


const uint8_t *font_get_data_blinkoff() 
{ 
  return font_blinkoff; 
}


uint8_t font_get_char_height()
{
  return font_char_height;
}


const uint8_t *font_get_bmpdata(uint8_t fontNum)
{
  switch( fontNum )
    {
    case FONT_ID_NONE:     return NULL;
    case FONT_ID_CGA:      return font_cga_bmp;
    case FONT_ID_EGA:      return font_ega_bmp;
    case FONT_ID_VGA:      return font_vga_bmp;
    case FONT_ID_TERM:     return font_terminus_bmp;
    case FONT_ID_TERMBOLD: return font_terminus_bold_bmp;
    case FONT_ID_PETSCII:  return font_petscii_bmp;
    case FONT_ID_USER1:
    case FONT_ID_USER2:
    case FONT_ID_USER3:
    case FONT_ID_USER4: return flash_get_read_ptr(fontNum-FONT_ID_USER1+12);
    }

  return font_cga_bmp;
}


bool INFLASHFUN font_get_font_info(uint8_t fontNum, uint32_t *bitmapWidth, uint32_t *bitmapHeight, uint8_t *charHeight, uint8_t *underlineRow)
{
  bool ok = true;
  uint8_t ch=0, ur=0;
  uint32_t bh=0, bw=0;

  switch( fontNum )
    {
    case FONT_ID_CGA:
      bh=64; bw=256; ch=8; ur=7;
      break;

    case FONT_ID_EGA:
      bh=56; bw=512; ch=14; ur=13;
      break;

    case FONT_ID_VGA:
    case FONT_ID_TERM:
    case FONT_ID_TERMBOLD:
      bh=64; bw=512; ch=16;ur=14; 
      break;

    case FONT_ID_PETSCII:
      bh=128; bw=128; ch=8; ur=7;
      break;
      
    case FONT_ID_USER1:
    case FONT_ID_USER2:
    case FONT_ID_USER3:
    case FONT_ID_USER4: 
      bh=userFontInfo[fontNum-FONT_ID_USER1].bitmapHeight; 
      bw=userFontInfo[fontNum-FONT_ID_USER1].bitmapWidth; 
      ch=userFontInfo[fontNum-FONT_ID_USER1].charHeight;
      ur=userFontInfo[fontNum-FONT_ID_USER1].underlineRow;
      break;

    default:
      ok = false;
      break;
    }

  if( bitmapWidth!=NULL )  *bitmapWidth=bw;
  if( bitmapHeight!=NULL ) *bitmapHeight=bh;
  if( charHeight!=NULL )   *charHeight=ch;
  if( underlineRow!=NULL ) *underlineRow=ur;
  
  return ok;
}


const INFLASHFUN char *font_get_name(uint8_t fontNum)
{
  switch( fontNum )
    {
    case FONT_ID_CGA:      return "CGA";
    case FONT_ID_EGA:      return "EGA";
    case FONT_ID_VGA:      return "VGA";
    case FONT_ID_TERM:     return "Terminus";
    case FONT_ID_TERMBOLD: return "Terminus bold";
    case FONT_ID_PETSCII:  return "PETSCII";
    case FONT_ID_USER1: 
    case FONT_ID_USER2:
    case FONT_ID_USER3:
    case FONT_ID_USER4: return userFontInfo[fontNum-FONT_ID_USER1].name; 
    }

  return NULL;
}


void INFLASHFUN font_set_name(uint8_t fontNum, const char *name)
{
  if( fontNum>=FONT_ID_USER1 && fontNum<=FONT_ID_USER4 )
    {
      strncpy(userFontInfo[fontNum-FONT_ID_USER1].name, name, 32);
      userFontInfo[fontNum-FONT_ID_USER1].name[31] = 0;
      flash_write(11, userFontInfo, sizeof(userFontInfo));
    }
}


const INFLASHFUN uint8_t font_map_graphics_char(uint8_t c, bool boldFont)
{
  if( c>=96 && c<=126 )
    {
      uint8_t fontNum = (boldFont && font_have_boldfont()) ? cur_font_bold : cur_font_normal;

      if( fontNum<FONT_ID_USER1 )
        c = builtin_font_graphics_char_mapping[c-96];
      else if( fontNum<=FONT_ID_USER4 )
        c = userFontInfo[fontNum-FONT_ID_USER1].graphicsCharMapping[c-96];
    }
  
  return c;
}


const INFLASHFUN uint8_t *font_get_graphics_char_mapping(uint8_t fontNum)
{
  if( fontNum<FONT_ID_USER1 )
    return builtin_font_graphics_char_mapping;
  else if( fontNum<=FONT_ID_USER4 )
    return userFontInfo[fontNum-FONT_ID_USER1].graphicsCharMapping;
  else 
    return NULL;
}


bool INFLASHFUN font_set_graphics_char_mapping(uint8_t fontNum, const uint8_t *mapping)
{
  if( fontNum>=FONT_ID_USER1 && fontNum<=FONT_ID_USER4 && memcmp(userFontInfo[fontNum-FONT_ID_USER1].graphicsCharMapping, mapping, 31)!=0 )
    {
      memcpy(userFontInfo[fontNum-FONT_ID_USER1].graphicsCharMapping, mapping, 31);
      flash_write(11, userFontInfo, sizeof(userFontInfo));
      return true;
    }

  return false;
}


void INFLASHFUN font_set_underline_row(uint8_t fontNum, uint8_t underlineRow)
{
  if( fontNum>=FONT_ID_USER1 && fontNum<=FONT_ID_USER4 && userFontInfo[fontNum-FONT_ID_USER1].underlineRow!=underlineRow )
    {
      userFontInfo[fontNum-FONT_ID_USER1].underlineRow = underlineRow;
      flash_write(11, userFontInfo, sizeof(userFontInfo));
    }
}


// -----------------------------------------------------------------------------------------------------------------


static uint8_t state;
static uint32_t byteCounter, bitmapWidth, bitmapHeight, fontBaseAddr, fontCharHeight;
static const char *error = NULL;


bool INFLASHFUN receiveFontDataPacket(unsigned long no, char* charData, int size)
{
  static uint8_t dataPage[256];
  uint8_t *data = (uint8_t *) charData;

  if( error==NULL )
    {
      if( state==0 )
        {
          // beginning of file (header)
          bitmapWidth  = data[0x12]+(data[0x13]<<8)+(data[0x14]<<16)+(data[0x15]<<24);
          bitmapHeight = data[0x16]+(data[0x17]<<8)+(data[0x18]<<16)+(data[0x19]<<24);

          if( data[0]!='B' || data[1]!='M' )
            error = "Invalid or unhandled BMP file format (expected BM signature)";
          else if( (bitmapWidth % 32) != 0 )
            error = "Bitmap width must be a multiple of 32 (multiple of 4 characters wide)";
          else if( ((bitmapHeight*bitmapWidth) % 2048)!=0 )
            error = "Product of bitmap height and width must be a multiple of 2048 (256 characters * 8 pixel width)";
          else 
            { 
              fontCharHeight = (bitmapHeight*bitmapWidth) / 2048;

              if( fontCharHeight<8 || fontCharHeight>16 )
                error = "Character height must be between 8 and 16 pixels (inclusive)";
              else if( (bitmapHeight % fontCharHeight) != 0 )
                error = "Bitmap height must be a multiple of the character height";
              else if( (data[0x1c]+(data[0x1d]<<8)) != 1 )
                error = "Bitmap must be monochrome (2 colors)";
              else if( data[0x1e]!=0 || data[0x1f]!=0 || data[0x20]!=0 || data[0x21]!=0 )
                error = "Bitmap file must contain uncompressed data";
              else
                {
                  state = 1; 
                  byteCounter = data[0x0a]+(data[0x0b]<<8)+(data[0x0c]<<16)+(data[0x0d]<<24); 
                  uint32_t ints = save_and_disable_interrupts();
                  flash_range_erase(fontBaseAddr, FLASH_SECTOR_SIZE);
                  restore_interrupts(ints);
                }
            }
        }
      
      if( state==1 )
        {
          // waiting for bitmap data start
          if( byteCounter > size )
            byteCounter -= size;
          else
            {
              memcpy(dataPage, data+byteCounter, size-byteCounter);
              state = 2;
              byteCounter = size-byteCounter;
            }
        }
      else if( state==2 )
        {
          // receiving data
          uint8_t pagePos = byteCounter%256;
          if( pagePos+size >= 256 )
            {
              memcpy(dataPage+pagePos, data, 256-pagePos);
              
              uint32_t ints = save_and_disable_interrupts();
              flash_range_program(fontBaseAddr + (byteCounter&~255), dataPage, FLASH_PAGE_SIZE);
              restore_interrupts(ints);

              size -= 256-pagePos;
              data += 256-pagePos;
              byteCounter += 256-pagePos;
              pagePos = 0;
            }

          memcpy(dataPage+pagePos, data, size);
          byteCounter += size;
          if( byteCounter>=4096 ) state = 3;
        }
      else if( state==3 )
        {
          // done receiving => ignore additional data
        }
    }

  return true; //error==NULL;
}


const char *INFLASHFUN font_receive_fontdata(uint8_t userFontNum)
{
  state = 0;
  error = NULL;
  if( userFontNum<4 )
    {
      fontBaseAddr = flash_get_write_offset(userFontNum+12);

      while( serial_xmodem_receive_char(10)!=-1 );
      if( !xmodem_receive(serial_xmodem_receive_char, serial_xmodem_send_data, receiveFontDataPacket) )
        error = "Transmission failed or canceled";
      else if( error==NULL )
        {
          userFontInfo[userFontNum].bitmapWidth = bitmapWidth;
          userFontInfo[userFontNum].bitmapHeight = bitmapHeight;
          userFontInfo[userFontNum].charHeight = fontCharHeight;
          userFontInfo[userFontNum].underlineRow = (fontCharHeight<13) ? fontCharHeight-1 : fontCharHeight-2;
          flash_write(11, userFontInfo, sizeof(userFontInfo));
        }
    }
  else
    error = "Invalid font number";
  
  return error;
}


// -----------------------------------------------------------------------------------------------------------------


static uint8_t INFLASHFUN reverse_bits(uint8_t b)
{
  uint8_t res = 0;
  for(int i=0; i<8; i++)
    {
      res <<= 1;
      if( b&1 ) res |= 1;
      b >>= 1;
    }

  return res;
}


static bool INFLASHFUN set_font_data(uint32_t font_offset, uint32_t bitmapWidth, uint32_t bitmapHeight, uint8_t charHeight, uint8_t underlineRow, const uint8_t *bitmapData)
{
  if( (bitmapWidth * bitmapHeight) == (2048*charHeight) && bitmapData!=NULL && charHeight>0 )
    {
      for(int br=0; br<bitmapHeight; br++)
        for(int bc=0; bc<bitmapWidth/8; bc++)
          {
            int cr = (bitmapHeight-br-1) % charHeight;
            int cn = ((bitmapHeight-br-1)/charHeight)*(bitmapWidth/8) + bc;

            uint32_t offset = cr*256*8+cn;
            uint8_t d   = bitmapData[br*bitmapWidth/8+bc];
            if( framebuf_is_dvi() ) d = reverse_bits(d);
            uint8_t du  = cr==underlineRow ? 255 : d;
            font_blinkoff[font_offset+offset+256*0] = d;
            font_blinkoff[font_offset+offset+256*1] = du;
            font_blinkoff[font_offset+offset+256*2] = d;
            font_blinkoff[font_offset+offset+256*3] = du;
            font_blinkon[font_offset+offset+256*0]  = d;
            font_blinkon[font_offset+offset+256*1]  = du;
            font_blinkon[font_offset+offset+256*2]  = ~d;
            font_blinkon[font_offset+offset+256*3]  = ~du;
          }
      
      return true;
    }

  return false;
}


bool INFLASHFUN font_apply_font(uint8_t font, bool bold)
{
  bool res = false;
  uint8_t charHeight, underlineRow;
  uint32_t bitmapHeight, bitmapWidth;
  
  if( font<=FONT_ID_USER4 )
    {
      if( bold && font==0 ) font = cur_font_normal;
      if( font!=(bold ? cur_font_bold : cur_font_normal) )
        {
          if( font_get_font_info(font, &bitmapWidth, &bitmapHeight, &charHeight, &underlineRow) )
            if( set_font_data(bold ? 4*256 : 0, bitmapWidth, bitmapHeight, charHeight, underlineRow, font_get_bmpdata(font)) )
              {
                if( bold )
                  cur_font_bold = font;
                else
                  cur_font_normal = font;
                
                font_char_height = charHeight;
                res = true;
              }
        }
      else
        res = true;
    }
  
  return res;
}


void INFLASHFUN font_apply_settings()
{
  uint8_t fn = config_get_screen_font_normal();
  uint8_t fb = config_get_screen_font_bold();
  uint8_t chn, chb;

  font_apply_font(fn, false);
  if( !font_get_font_info(fn, NULL, NULL, &chn, NULL) || !font_get_font_info(fb, NULL, NULL, &chb, NULL) || chn!=chb ) fb=0;
  font_apply_font(fb, true);
}


void INFLASHFUN font_init()
{
  flash_read(11, userFontInfo, sizeof(userFontInfo));
  bool modified = false;
  for(int i=0; i<sizeof(userFontInfo)/sizeof(struct FontInfoStruct); i++)
    {
      if( userFontInfo[i].magic != 0xA5968778 )
        {
          memset(&(userFontInfo[i]), 0, sizeof(struct FontInfoStruct));
          userFontInfo[i].magic  = 0xA5968778;
          snprintf(userFontInfo[i].name, 32, "User %i", i+1);
          memset(userFontInfo[i].graphicsCharMapping, ' ', 31);
          modified = true;
        }
      else
        userFontInfo[i].name[31] = 0;
    }
  
  if( modified ) flash_write(11, userFontInfo, sizeof(userFontInfo)); 

  font_apply_settings();
}
