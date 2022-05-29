#include "framebuf.h"
#include "font.h"
#include "terminal.h"
#include "config.h"
#include "pins.h"
#include "serial.h"
#include "sound.h"
#include "keyboard.h"
#include "hardware/uart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// defined in main.c
void wait(uint32_t milliseconds);

#define INFLASHFUN __in_flash(".terminalfun") 

#define TS_NORMAL      0
#define TS_WAITBRACKET 1
#define TS_STARTCHAR   2 
#define TS_READPARAM   3
#define TS_HASH        4
#define TS_READCHAR    5

#define CS_TEXT_US  0
#define CS_TEXT_UK  1
#define CS_GRAPHICS 2

static uint8_t terminal_state = TS_NORMAL;
static uint8_t color_fg, color_bg, attr = 0, cur_attr = 0;
static int cursor_col = 0, cursor_row = 0, saved_col = 0, saved_row = 0;
static int scroll_region_start, scroll_region_end;
static bool cursor_shown = true, origin_mode = false, cursor_eol = false, auto_wrap_mode = true, vt52_mode = false, localecho = false;
static bool saved_eol = false, saved_origin_mode = false, insert_mode = false;
static bool petscii_lower_case_charset = true;
static uint8_t saved_attr, saved_fg, saved_bg, saved_charset, charset, charset_G0, charset_G1, tabs[255];


static uint8_t INFLASHFUN get_charset(char c)
{
  switch( c )
    {
    case 'A' : return CS_TEXT_UK;
    case 'B' : return CS_TEXT_US;
    case '0' : return CS_GRAPHICS;
    case '1' : return CS_TEXT_US;
    case '2' : return CS_GRAPHICS;
    }

  return CS_TEXT_US;
}


static void INFLASHFUN show_cursor(bool show)
{
  uint8_t attr = ATTR_INVERSE;
  switch( config_get_terminal_cursortype() )
    {
    case 1: attr = ATTR_BLINK; break;
    case 2: attr = ATTR_UNDERLINE; break;
    }
  
  framebuf_set_attr(cursor_col, cursor_row, show ? (cur_attr ^ attr) : cur_attr);
}


static void INFLASHFUN move_cursor_wrap(int row, int col)
{
  if( row!=cursor_row || col!=cursor_col )
    {
      int top_limit    = scroll_region_start;
      int bottom_limit = scroll_region_end;
      
      if( cursor_shown && cursor_row>=0 && cursor_col>=0 ) show_cursor(false);
      
      while( col<0 )                        { col += framebuf_get_ncols(row); row--; }
      while( row<top_limit )                { row++; framebuf_scroll_region(top_limit, bottom_limit, -1, color_fg, color_bg); }
      while( col>=framebuf_get_ncols(row) ) { col -= framebuf_get_ncols(row); row++; }
      while( row>bottom_limit )             { row--; framebuf_scroll_region(top_limit, bottom_limit, 1, color_fg, color_bg); }

      cursor_row = row;
      cursor_col = col;
      cursor_eol = false;
      
      cur_attr = framebuf_get_attr(cursor_col, cursor_row);
      if( cursor_shown ) show_cursor(true);
    }
}


static void INFLASHFUN move_cursor_within_region(int row, int col, int top_limit, int bottom_limit)
{
  if( row!=cursor_row || col!=cursor_col )
    {
      if( cursor_shown && cursor_row>=0 && cursor_col>=0 ) show_cursor(false);

      if( col<0 ) 
        col = 0;
      else if( col>=framebuf_get_ncols(row) )
        col = framebuf_get_ncols(row)-1;

      if( row<top_limit ) 
        row = top_limit;
      else if( row>bottom_limit )
        row = bottom_limit;
          
      cursor_row = row;
      cursor_col = col;
      cursor_eol = false;

      cur_attr = framebuf_get_attr(cursor_col, cursor_row);
      if( cursor_shown ) show_cursor(true);
    }
}


static void INFLASHFUN move_cursor_limited(int row, int col)
{
  // only move if cursor is currently within scroll region, do not move
  // outside of scroll region
  if( cursor_row >= scroll_region_start && cursor_row <= scroll_region_end )
    move_cursor_within_region(row, col, scroll_region_start, scroll_region_end);
}



static void INFLASHFUN init_cursor(int row, int col)
{
  cursor_row = -1;
  cursor_col = -1;
  move_cursor_within_region(row, col, 0, framebuf_get_nrows()-1);
}


static void INFLASHFUN print_char_vt(char c)
{
  if( cursor_eol ) 
    { 
      // cursor was already past the end of the line => move it to the next line now
      move_cursor_wrap(cursor_row+1, 0); 
      cursor_eol=false; 
    }

  if( insert_mode )
    {
      show_cursor(false);
      framebuf_insert(cursor_col, cursor_row, 1, color_fg, color_bg);
    }

  if( charset==CS_TEXT_UK && c==35 )
    c=font_map_graphics_char(125, (attr & ATTR_BOLD)!=0); // pound sterling symbol
  else if( charset==CS_GRAPHICS )
    c=font_map_graphics_char(c, (attr & ATTR_BOLD)!=0);
  
  framebuf_set_color(cursor_col, cursor_row, color_fg, color_bg);
  framebuf_set_attr(cursor_col, cursor_row, attr);
  framebuf_set_char(cursor_col, cursor_row, c);

  if( auto_wrap_mode && cursor_col==framebuf_get_ncols(cursor_row)-1 )
    {
      // cursor stays in last column but will wrap if another character is typed
      cur_attr = attr;
      show_cursor(cursor_shown);
      cursor_eol=true;
    }
  else
    init_cursor(cursor_row, cursor_col+1);
}


static void INFLASHFUN print_char_petscii(char c)
{
  framebuf_set_color(cursor_col, cursor_row, color_fg, color_bg);
  framebuf_set_attr(cursor_col, cursor_row, attr);
  framebuf_set_char(cursor_col, cursor_row, c);
  int row = cursor_row, col = cursor_col;
  cursor_row = -1;
  cursor_col = -1;
  move_cursor_wrap(row, col+1);
}


void INFLASHFUN terminal_reset()
{
  saved_col = 0;
  saved_row = 0;
  cursor_shown = true;
  color_fg = config_get_terminal_default_fg();
  color_bg = config_get_terminal_default_bg();
  scroll_region_start = 0;
  scroll_region_end = framebuf_get_nrows()-1;
  origin_mode = false;
  cursor_eol = false;
  auto_wrap_mode = true;
  insert_mode = false;
  vt52_mode = false;
  attr = config_get_terminal_default_attr();
  saved_attr = 0;
  charset_G0 = CS_TEXT_US;
  charset_G1 = CS_GRAPHICS;
  charset = charset_G0;
  memset(tabs, 0, framebuf_get_ncols(-1));
  framebuf_set_scroll_delay(0);
  localecho = config_get_terminal_localecho();
  petscii_lower_case_charset = true;
}


void INFLASHFUN terminal_clear_screen()
{
  framebuf_fill_screen(' ', color_fg, color_bg);
  init_cursor(0, 0);
  scroll_region_start = 0;
  scroll_region_end = framebuf_get_nrows()-1;
  origin_mode = false;
}


static void INFLASHFUN send_char(char c)
{
  serial_send_char(c);
  if( localecho ) terminal_receive_char(c);
}


static void INFLASHFUN send_string(const char *s)
{
  serial_send_string(s);
  if( localecho ) terminal_receive_string(s);
}


static INFLASHFUN void terminal_process_text(char c)
{
  switch( c )
    {
    case 5: // ENQ => send answer-back string
      send_string(config_get_terminal_answerback());
      break;
      
    case 7: // BEL => produce beep
      sound_play_tone(config_get_audible_bell_frequency(), 
                      config_get_audible_bell_duration(), 
                      config_get_audible_bell_volume(), 
                      false);
      framebuf_flash_screen(config_get_visual_bell_color(), config_get_visual_bell_duration());
      break;
      
    case 8:   // backspace
    case 127: // delete
      {
        uint8_t mode = c==8 ? config_get_terminal_bs() : config_get_terminal_del();
        if( mode>0 )
          {
            int top_limit = origin_mode ? scroll_region_start : 0;
            if( cursor_row>top_limit )
              move_cursor_wrap(cursor_row, cursor_col-1);
            else
              move_cursor_limited(cursor_row, cursor_col-1);

            if( mode==2 )
              {
                framebuf_set_char(cursor_col, cursor_row, ' ');
                framebuf_set_attr(cursor_col, cursor_row, 0);
                cur_attr = 0;
                show_cursor(cursor_shown);
              }
          }

        break;
      }

    case '\t': // horizontal tab
      {
        int col = cursor_col+1;
        while( col < framebuf_get_ncols(cursor_row)-1 && !tabs[col] ) col++;
        move_cursor_limited(cursor_row, col); 
        break;
      }
      
    case '\n': // newline
    case 11:   // vertical tab (interpreted as newline)
    case 12:   // form feed (interpreted as newline)
    case '\r': // carriage return
      {
        switch( c=='\r' ? config_get_terminal_cr() : config_get_terminal_lf() )
          {
          case 1: move_cursor_wrap(cursor_row, 0); break;
          case 2: move_cursor_wrap(cursor_row+1, cursor_col); break;
          case 3: move_cursor_wrap(cursor_row+1, 0); break;
          }
        break;
      }

    case 14:  // SO
      charset = charset_G1; 
      break;

    case 15:  // SI
      charset = charset_G0; 
      break;

    default: // regular character
      if( c>=32 ) print_char_vt(c);
      break;
    }
}


static void INFLASHFUN terminal_process_command(char start_char, char final_char, uint8_t num_params, uint8_t *params)
{
  // NOTE: num_params>=1 always holds, if no parameters were received then params[0]=0
  if( final_char=='l' || final_char=='h' )
    {
      bool enabled = final_char=='h';
      if( start_char=='?' )
        {
          switch( params[0] )
            {
            case 2:
              if( !enabled ) { terminal_reset(); vt52_mode = true; }
              break;

            case 3: // switch 80/132 columm mode - 132 columns not supported but we can clear the screen
              terminal_clear_screen();
              break;

            case 4: // smooth scroll
              framebuf_set_scroll_delay(enabled ? (1000/6) : 0);
              break;
              
            case 5: // invert screen
              framebuf_set_screen_inverted(enabled);
              break;
          
            case 6: // origin mode
              origin_mode = enabled; 
              move_cursor_limited(scroll_region_start, 0); 
              break;
              
            case 7: // auto-wrap mode
              auto_wrap_mode = enabled; 
              break;

            case 12: // local echo (send-receive mode)
              localecho = !enabled;
              break;
              
            case 25: // show/hide cursor
              cursor_shown = enabled;
              show_cursor(cursor_shown);
              break;
            }
        }
      else if( start_char==0 )
        {
          switch( params[0] )
            {
            case 4: // insert mode
              insert_mode = enabled;
              break;
            }
        }
    }
  else if( final_char=='J' )
    {
      switch( params[0] )
        {
        case 0:
          for(int i=0; i<cursor_row; i++) framebuf_set_row_attr(i, 0);
          framebuf_fill_region(cursor_col, cursor_row, framebuf_get_ncols(cursor_row)-1, framebuf_get_nrows()-1, ' ', color_fg, color_bg);
          break;
          
        case 1:
          for(int i=cursor_row+1; i<framebuf_get_nrows(); i++) framebuf_set_row_attr(i, 0);
          framebuf_fill_region(0, 0, cursor_col, cursor_row, ' ', color_fg, color_bg);
          break;
          
        case 2:
          for(int i=0; i<framebuf_get_nrows(); i++) framebuf_set_row_attr(i, 0);
          framebuf_fill_region(0, 0, framebuf_get_ncols(cursor_row)-1, framebuf_get_nrows()-1, ' ', color_fg, color_bg);
          break;
        }

      cur_attr = framebuf_get_attr(cursor_col, cursor_row);
      show_cursor(cursor_shown);
    }
  else if( final_char=='K' )
    {
      switch( params[0] )
        {
        case 0:
          framebuf_fill_region(cursor_col, cursor_row, framebuf_get_ncols(cursor_row)-1, cursor_row, ' ', color_fg, color_bg);
          break;
          
        case 1:
          framebuf_fill_region(0, cursor_row, cursor_col, cursor_row, ' ', color_fg, color_bg);
          break;
          
        case 2:
          framebuf_fill_region(0, cursor_row, framebuf_get_ncols(cursor_row)-1, cursor_row, ' ', color_fg, color_bg);
          break;
        }

      cur_attr = framebuf_get_attr(cursor_col, cursor_row);
      show_cursor(cursor_shown);
    }
  else if( final_char=='A' )
    {
      move_cursor_limited(cursor_row-MAX(1, params[0]), cursor_col);
    }
  else if( final_char=='B' )
    {
      move_cursor_limited(cursor_row+MAX(1, params[0]), cursor_col);
    }
  else if( final_char=='C' || final_char=='a' )
    {
      move_cursor_limited(cursor_row, cursor_col+MAX(1, params[0]));
    }
  else if( final_char=='D' || final_char=='j' )
    {
      move_cursor_limited(cursor_row, cursor_col-MAX(1, params[0]));
    }
  else if( final_char=='E' || final_char=='e' )
    {
      move_cursor_limited(cursor_row+MAX(1, params[0]), 0);
    }
  else if( final_char=='F' || final_char=='k' )
    {
      move_cursor_limited(cursor_row-MAX(1, params[0]), 0);
    }
  else if( final_char=='d' )
    {
      move_cursor_limited(MAX(1, params[0]), cursor_col);
    }
  else if( final_char=='G' || final_char=='`' )
    {
      move_cursor_limited(cursor_row, MAX(1, params[0])-1);
    }
  else if( final_char=='H' || final_char=='f' )
    {
      int top_limit    = origin_mode ? scroll_region_start : 0;
      int bottom_limit = origin_mode ? scroll_region_end   : framebuf_get_nrows()-1;
      move_cursor_within_region(top_limit+MAX(params[0],1)-1, num_params<2 ? 0 : MAX(params[1],1)-1, top_limit, bottom_limit);
    }
  else if( final_char=='I' )
    {
      int n = MAX(1, params[0]);
      int col = cursor_col+1;
      while( n>0 && col < framebuf_get_ncols(cursor_row)-1 )
        {
          while( col < framebuf_get_ncols(cursor_row)-1 && !tabs[col] ) col++;
          n--;
        }
      move_cursor_limited(cursor_row, col); 
    }
  else if( final_char=='Z' )
    {
      int n = MAX(1, params[0]);
      int col = cursor_col-1;
      while( n>0 && col>0 )
        {
          while( col>0 && !tabs[col] ) col--;
          n--;
        }
      move_cursor_limited(cursor_row, col); 
    }
  else if( final_char=='L' || final_char=='M' )
    {
      int n = MAX(1, params[0]);
      int bottom_limit = origin_mode ? scroll_region_end : framebuf_get_nrows()-1;
      show_cursor(false);
      framebuf_scroll_region(cursor_row, bottom_limit, final_char=='M' ? n : -n, color_fg, color_bg);
      cur_attr = framebuf_get_attr(cursor_col, cursor_row);
      show_cursor(cursor_shown);
    }
  else if( final_char=='@' )
    {
      int n = MAX(1, params[0]);
      show_cursor(false);
      framebuf_insert(cursor_col, cursor_row, n, color_fg, color_bg);
      cur_attr = framebuf_get_attr(cursor_col, cursor_row);
      show_cursor(cursor_shown);
    }
  else if( final_char=='P' )
    {
      int n = MAX(1, params[0]);
      framebuf_delete(cursor_col, cursor_row, n, color_fg, color_bg);
      cur_attr = framebuf_get_attr(cursor_col, cursor_row);
      show_cursor(cursor_shown);
    }
  else if( final_char=='S' || final_char=='T' )
    {
      int top_limit    = origin_mode ? scroll_region_start : 0;
      int bottom_limit = origin_mode ? scroll_region_end   : framebuf_get_nrows()-1;
      int n = MAX(1, params[0]);
      show_cursor(false);
      while( n-- ) framebuf_scroll_region(top_limit, bottom_limit, final_char=='S' ? n : -n, color_fg, color_bg);
      cur_attr = framebuf_get_attr(cursor_col, cursor_row);
      show_cursor(cursor_shown);
    }
  else if( final_char=='g' )
    {
      int p = params[0];
      if( p==0 )
        tabs[cursor_col] = false;
      else if( p==3 )
        memset(tabs, 0, framebuf_get_ncols(-1));
    }
  else if( final_char=='m' )
    {
      unsigned int i;
      for(i=0; i<num_params; i++)
        {
          int p = params[i];

          if( p==0 )
            {
              color_fg = config_get_terminal_default_fg();
              color_bg = config_get_terminal_default_bg();
              attr     = config_get_terminal_default_attr();
              //cursor_shown = true;
              show_cursor(cursor_shown);
            }
          else if( p==1 )
            attr |= ATTR_BOLD;
          else if( p==4 )
            attr |= ATTR_UNDERLINE;
          else if( p==5 )
            attr |= ATTR_BLINK;
          else if( p==7 )
            attr |= ATTR_INVERSE;
          else if( p==22 )
            attr &= ~ATTR_BOLD;
          else if( p==24 )
            attr &= ~ATTR_UNDERLINE;
          else if( p==25 )
            attr &= ~ATTR_BLINK;
          else if( p==27 )
            attr &= ~ATTR_INVERSE;
          else if( p>=30 && p<=37 )
            color_fg = p-30;
          else if( p==38 && num_params>=i+2 && params[i+1]==5 )
            { color_fg = params[i+2] & 15; i+=2; }
          else if( p==39 )
            color_fg = config_get_terminal_default_fg();
          else if( p>=40 && p<=47 )
            color_bg = p-40;
          else if( p==48 && num_params>=i+2 && params[i+1]==5 )
            { color_bg = params[i+2] & 15; i+=2; }
          else if( p==49 )
            color_bg = config_get_terminal_default_bg();

          show_cursor(cursor_shown);
        }
    }
  else if( final_char=='r' )
    {
      if( num_params==2 && params[1]>params[0] )
        {
          scroll_region_start = MAX(params[0], 1)-1;
          scroll_region_end   = MIN(params[1], framebuf_get_nrows())-1;
        }
      else if( params[0]==0 )
        {
          scroll_region_start = 0;
          scroll_region_end   = framebuf_get_nrows()-1;
        }

      move_cursor_within_region(scroll_region_start, 0, scroll_region_start, scroll_region_end);
    }
  else if( final_char=='s' )
    {
      saved_row = cursor_row;
      saved_col = cursor_col;
      saved_eol = cursor_eol;
      saved_origin_mode = origin_mode;
      saved_fg  = color_fg;
      saved_bg  = color_bg;
      saved_attr = attr;
      saved_charset = charset;
    }
  else if( final_char=='u' )
    {
      move_cursor_limited(saved_row, saved_col);
      origin_mode = saved_origin_mode;      
      cursor_eol = saved_eol;
      color_fg = saved_fg;
      color_bg = saved_bg;
      attr = saved_attr;
      charset = saved_charset;
    }
  else if( final_char=='c' )
    {
      // device attributes resport
      // https://www.vt100.net/docs/vt100-ug/chapter3.html#DA
      // https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h4-Functions-using-CSI-_-ordered-by-the-final-character-lparen-s-rparen:CSI-Ps-c.1CA3
      // "ESC [?1;0c" => base VT100, no options
      // "ESC [?6c"   => VT102
      send_string("\033[?6c");
    }
  else if( final_char=='n' )
    {
      if( params[0] == 5 )
        {
          // terminal status report
          send_string("\033[0n");
        }
      else if( params[0] == 6 )
        {
          // cursor position report
          int top_limit = origin_mode ? scroll_region_start : 0;
          char buf[20];
          snprintf(buf, 20, "\033[%u;%uR", cursor_row-top_limit+1, cursor_col+1);
          send_string(buf);
        }
    }
}


void INFLASHFUN terminal_receive_char_vt102(char c)
{
  static char    start_char = 0;
  static uint8_t num_params = 0;
  static uint8_t params[16];

  if( terminal_state!=TS_NORMAL )
    {
      if( (c==8 || c==13) )
        {
          // processe some cursor control characters within escape sequences
          // (otherwise we fail "vttest" cursor control tests)
          terminal_process_text(c);
          return;
        }
      else if( c==11 )
        {
          // ignore VT character plus the following character
          // (otherwise we fail "vttest" cursor control tests)
          terminal_state = TS_READCHAR;
          return;
        }
    }

  switch( terminal_state )
    {
    case TS_NORMAL:
      {
        if( c==27 )
          terminal_state = TS_WAITBRACKET;
        else
          terminal_process_text(c);

        break;
      }

    case TS_WAITBRACKET:
      {
        terminal_state = TS_NORMAL;

        switch( c )
          {
          case '[':
            start_char = 0;
            num_params = 1;
            params[0] = 0;
            terminal_state = TS_STARTCHAR;
            break;
            
          case '#':
            terminal_state = TS_HASH;
            break;
            
          case  27: print_char_vt(c); break;                           // escaped ESC
          case 'c': terminal_reset(); break;                           // reset
          case '7': terminal_process_command(0, 's', 0, NULL); break;  // save cursor position
          case '8': terminal_process_command(0, 'u', 0, NULL); break;  // restore cursor position
          case 'H': tabs[cursor_col] = true; break;                    // set tab
          case 'J': terminal_process_command(0, 'J', 0, NULL); break;  // clear to end of screen
          case 'K': terminal_process_command(0, 'K', 0, NULL); break;  // clear to end of row
          case 'D': move_cursor_wrap(cursor_row+1, cursor_col); break; // cursor down
          case 'E': move_cursor_wrap(cursor_row+1, 0); break;          // cursor down and to first column
          case 'I': move_cursor_wrap(cursor_row-1, 0); break;          // cursor up and to furst column
          case 'M': move_cursor_wrap(cursor_row-1, cursor_col); break; // cursor up
          case '(': 
          case ')': 
          case '+':
          case '*':
            start_char = c;
            terminal_state = TS_READCHAR;
            break;
          }

        break;
      }

    case TS_STARTCHAR:
    case TS_READPARAM:
      {
        if( c>='0' && c<='9' )
          {
            params[num_params-1] = params[num_params-1]*10 + (c-'0');
            terminal_state = TS_READPARAM;
          }
        else if( c == ';' )
          {
            // next parameter (max 16 parameters)
            num_params++;
            if( num_params>16 )
              terminal_state = TS_NORMAL;
            else
              {
                params[num_params-1]=0;
                terminal_state = TS_READPARAM;
              }
          }
        else if( terminal_state==TS_STARTCHAR && (c=='?' || c=='#') )
          {
            start_char = c;
            terminal_state = TS_READPARAM;
          }
        else
          {
            // not a parameter value or startchar => command is done
            terminal_process_command(start_char, c, num_params, params);
            terminal_state = TS_NORMAL;
          }
        
        break;
      }

    case TS_HASH:
      {
        switch( c )
          {
          case '3':
            {
              framebuf_set_row_attr(cursor_row, ROW_ATTR_DBL_WIDTH | ROW_ATTR_DBL_HEIGHT_TOP);
              break;
            }

          case '4':
            {
              framebuf_set_row_attr(cursor_row, ROW_ATTR_DBL_WIDTH | ROW_ATTR_DBL_HEIGHT_BOT);
              break;
            }
            
          case '5':
            {
              framebuf_set_row_attr(cursor_row, 0);
              break;
            }

          case '6':
            {
              framebuf_set_row_attr(cursor_row, ROW_ATTR_DBL_WIDTH);
              break;
            }

          case '8': 
            {
              // fill screen with 'E' characters (DEC test feature)
              int top_limit    = origin_mode ? scroll_region_start : 0;
              int bottom_limit = origin_mode ? scroll_region_end   : framebuf_get_nrows()-1;
              show_cursor(false);
              framebuf_fill_region(0, top_limit, framebuf_get_ncols(-1)-1, bottom_limit, 'E', color_fg, color_bg);
              cur_attr = framebuf_get_attr(cursor_col, cursor_row);
              show_cursor(cursor_shown);
              break;
            }
          }
        
        terminal_state = TS_NORMAL;
        break;
      }

    case TS_READCHAR:
      {
        if( start_char=='(' )
          charset_G0 = get_charset(c);
        else if( start_char==')' )
          charset_G1 = get_charset(c);

        terminal_state = TS_NORMAL;
        break;
      }
    }
}


void INFLASHFUN terminal_receive_char_vt52(char c)
{
  static char start_char, row;

  switch( terminal_state )
    {
    case TS_NORMAL:
      {
        if( c==27 )
          terminal_state = TS_STARTCHAR;
        else
          terminal_process_text(c);
        
        break;
      }

    case TS_STARTCHAR:
      {
        terminal_state = TS_NORMAL;

        switch( c )
          {
          case 'A': 
            move_cursor_limited(cursor_row-1, cursor_col);
            break;

          case 'B': 
            move_cursor_limited(cursor_row+1, cursor_col);
            break;

          case 'C': 
            move_cursor_limited(cursor_row, cursor_col+1);
            break;

          case 'D': 
            move_cursor_limited(cursor_row, cursor_col-1);
            break;

          case 'E':
            framebuf_fill_screen(' ', color_fg, color_bg);
            // fall through

          case 'H': 
            move_cursor_limited(0, 0);
            break;

          case 'I': 
            move_cursor_wrap(cursor_row-1, cursor_col);
            break;

          case 'J':
            show_cursor(false);
            framebuf_fill_region(cursor_col, cursor_row, framebuf_get_ncols(cursor_row)-1, framebuf_get_nrows()-1, ' ', color_fg, color_bg);
            cur_attr = framebuf_get_attr(cursor_col, cursor_row);
            show_cursor(cursor_shown);
            break;

          case 'K':
            show_cursor(false);
            framebuf_fill_region(cursor_col, cursor_row, framebuf_get_ncols(cursor_row)-1, cursor_row, ' ', color_fg, color_bg);
            cur_attr = framebuf_get_attr(cursor_col, cursor_row);
            show_cursor(cursor_shown);
            break;

          case 'L':
          case 'M':
            show_cursor(false);
            framebuf_scroll_region(cursor_row, framebuf_get_nrows()-1, c=='M' ? 1 : -1, color_fg, color_bg);
            cur_attr = framebuf_get_attr(cursor_col, cursor_row);
            show_cursor(cursor_shown);
            break;

          case 'Y':
            start_char = c;
            row = 0;
            terminal_state = TS_READPARAM;
            break;
            
          case 'Z':
            send_string("\033/K");
            break;

          case 'b':
          case 'c':
            start_char = c;
            terminal_state = TS_READPARAM;
            break;

          case 'd':
            framebuf_fill_region(0, 0, cursor_col, cursor_row, ' ', color_fg, color_bg);
            init_cursor(cursor_col, cursor_row);
            break;
            
          case 'e':
            show_cursor(true);
            break;

          case 'f':
            show_cursor(false);
            break;

          case 'j':
            saved_col = cursor_col;
            saved_row = cursor_row;
            break;

          case 'k':
            move_cursor_limited(saved_row, saved_col);
            break;

          case 'l':
            framebuf_fill_region(0, cursor_row, framebuf_get_ncols(cursor_row)-1, cursor_row, ' ', color_fg, color_bg);
            init_cursor(0, cursor_row);
            break;

          case 'o':
            framebuf_fill_region(0, cursor_row, cursor_col, cursor_row, ' ', color_fg, color_bg);
            show_cursor(cursor_shown);
            break;

          case 'p':
            framebuf_set_screen_inverted(true);
            break;

          case 'q':
            framebuf_set_screen_inverted(false);
            break;

          case 'v':
            auto_wrap_mode = true;
            break;

          case 'w':
            auto_wrap_mode = false;
            break;

          case '<':
            terminal_reset();
            vt52_mode = false;
            break;
          }

        break;
      }

    case TS_READPARAM:
      {
        if( start_char=='Y' )
          {
            if( row==0 )
              row = c;
            else
              {
                if( row>=32 && c>=32 ) move_cursor_limited(row-32, c-32);
                terminal_state = TS_NORMAL;
              }
          }
        else if( start_char=='b' && c>=32 )
          {
            color_fg = (c-32) & 15;
            show_cursor(cursor_shown);
          }
        else if( start_char=='c' && c>=32 )
          {
            color_bg = (c-32) & 15;
            show_cursor(cursor_shown);
          }

        break;
      }
    }
}


static void INFLASHFUN terminal_receive_char_petscii(uint8_t c)
{
  static uint8_t inserted = 0;
  static bool quoteMode = false;

  if( c>=192 )
    {
      if     ( c<=223 ) c -= 96;
      else if( c<=254 ) c -= 64;
      else if( c==255 ) c  = 126;
    }

  if( c==34 ) quoteMode=!quoteMode;

  if( (quoteMode || inserted>0) )
    {
      uint8_t cc = 0;
      if( c<32 && c!=13 && (!quoteMode || c!=20) )
        {
          switch( c )
            {
            case 27: cc = 0x5B; break;
            case 28: cc = 0x9A; break;
            case 29: cc = 0x5D; break;
            case 30: cc = 0x98; break;
            case 31: cc = 0x99; break;
            default: cc = petscii_lower_case_charset ? c+96 : c+64; break;
            }
        }
      else if( c>=128 && c<160 && (quoteMode || c!=148) )
        {
          switch( c )
            {
            case 155: cc = 0x9B; break;
            case 156: cc = 0x9C; break;
            case 157: cc = 0x9D; break;
            case 158: cc = petscii_lower_case_charset ? 0x9E : 0xCE; break;
            case 159: cc = petscii_lower_case_charset ? 0x9F : 0xDF; break;
            default:  cc = petscii_lower_case_charset ? c-64 : c+64; break;
            }
        }
          
      if( cc>0 )
        {
          uint8_t a = attr;
          attr |= ATTR_INVERSE;
          print_char_petscii(cc);
          if( inserted>0 ) inserted--;
          attr = a;
          return;
        }
    }
      
  switch( c )
    {
    case 5: // WHITE
      color_fg = 1;
      break;

    case 10:  // LF
    case 13:  // CR
    case 141: // shift+CR
      {
        switch( c==10 ? config_get_terminal_lf() : config_get_terminal_cr() )
          {
          case 1: move_cursor_wrap(cursor_row, 0); break;
          case 2: move_cursor_wrap(cursor_row+1, cursor_col); break;
          case 3: move_cursor_wrap(cursor_row+1, 0); break;
          }
        if( c!=10 ) { inserted = 0; quoteMode = false; attr &= ~ATTR_INVERSE; }
        break;
      }

    case 14: // Switch to lower case character set
      petscii_lower_case_charset = true;
      for(uint8_t row=0; row<framebuf_get_nrows(); row++)
        for(uint8_t col=0; col<framebuf_get_ncols(col); col++)
          {
            char c = framebuf_get_char(col, row);
            if( c>='A' && c<='Z' )
              c += 32;
            else if( c>='A'+128 && c<='Z'+128 ) 
              c -= 128;
            else if( c==0xDE || c==0xDF || c==0xE9 || c==0xFA )
              c -= 64;
            framebuf_set_char(col, row, c);
          }
      break;

    case 17: // cursor down
      move_cursor_wrap(cursor_row+1, cursor_col);
      break;

    case 18: // enable reverse character mode
      attr |= ATTR_INVERSE;
      break;

    case 19: // cursor home
      move_cursor_wrap(0, 0);
      break;

    case 20: // backspace/delete
      if( cursor_col>0 || cursor_row>0 )
        {
          move_cursor_wrap(cursor_row, cursor_col-1);
          framebuf_delete(cursor_col, cursor_row, 1, color_fg, color_bg);
          cur_attr = framebuf_get_attr(cursor_col, cursor_row);
          show_cursor(cursor_shown);
        }
      break;

    case 28: // red
      color_fg = 2;
      break;

    case 29: // cursor right
      move_cursor_wrap(cursor_row, cursor_col+1);
      break;
      
    case 30: // green
      color_fg = 5;
      break;

    case 31: // blue
      color_fg = 6;
      break;

    case 129: // orange
      color_fg = 8;
      break;

    case 142: // Switch to upper case character set
      petscii_lower_case_charset = false;
      for(uint8_t row=0; row<framebuf_get_nrows(); row++)
        for(uint8_t col=0; col<framebuf_get_ncols(col); col++)
          {
            char c = framebuf_get_char(col, row);
            if( c>='a' && c<='z' ) 
              c -= 32;
            else if( c>='A' && c<='Z' )
              c += 128;
            else if( c==0x9E || c==0x9F || c==0xA9 || c==0xBA )
              c += 64;
            framebuf_set_char(col, row, c);
          }
      break;

    case 144: // black
      color_fg = 0;
      break;

    case 145: // cursor up
      move_cursor_limited(cursor_row-1, cursor_col);
      break;

    case 146: // disable reverse character mode
      attr &= ~ATTR_INVERSE;
      break;

    case 147: // clear screen
      terminal_clear_screen();
      break;

    case 148: // insert
      show_cursor(false);
      framebuf_insert(cursor_col, cursor_row, 1, color_fg, color_bg);
      cur_attr = framebuf_get_attr(cursor_col, cursor_row);
      show_cursor(cursor_shown);
      inserted++;
      break;

    case 149: // brown
      color_fg = 9;
      break;

    case 150: // light red
      color_fg = 10;
      break;

    case 151: // dark grey
      color_fg = 11;
      break;

    case 152: // grey
      color_fg = 12;
      break;

    case 153: // light green
      color_fg = 13;
      break;

    case 154: // light blue
      color_fg = 14;
      break;

    case 155: // light gray
      color_fg = 15;
      break;

    case 156: // purple
      color_fg = 4;
      break;

    case 157: // cursor left
      if( cursor_row>0 )
        move_cursor_wrap(cursor_row, cursor_col-1);
      else
        move_cursor_limited(cursor_row, cursor_col-1);
      break;

    case 158: // yellow
      color_fg = 7;
      break;

    case 159: // cyan
      color_fg = 3;
      break;

    default:
      {
        if( c>=65 && c<=90 && petscii_lower_case_charset )
          c += 32;
        else if( c>=97 && c<=122 )
          c  = petscii_lower_case_charset ? c-32 : c+96;
        else if( c>=149 && c<=191 && c!=169 && c!=186 )
          c += 64; // more PETSCII graphics characters
        else if( c>=133 && c<=140 )
          c = 0; // function keys (no function in terminal and not printable)
        else
          {
            switch( c )
              {
              case  92: c = 0x9A; break; // pound symbol
              case  94: c = 0x98; break; // up arrow
              case  95: c = 0x99; break; // left arrow
              case  96: c = 0xC0; break; // middle horizontal line
              case 123: c = 0x9B; break; // cross
              case 124: c = 0x9C; break; // left checkerboard
              case 125: c = 0x9D; break; // middle vertical line
              case 126: c = petscii_lower_case_charset ? 0x9E : 0xDE ; break; // full checkerboard / pi
              case 127: c = petscii_lower_case_charset ? 0x9F : 0xDF ; break; // down diagonals / top right triangle
              case 169: c = petscii_lower_case_charset ? 0xA9 : 0xE9 ; break; // up diagonals / top left triangle
              case 186: c = petscii_lower_case_charset ? 0xBA : 0xFA ; break; // checkmark / bottom right corner
              }
          }

        if( c>0 ) print_char_petscii(c);
        if( inserted>0 ) inserted--;
        break;
      }
    }
}


void INFLASHFUN terminal_receive_char(char c)
{
  if( config_get_terminal_clearBit7() ) c &= 0x7f;

  switch( config_get_terminal_type() )
    {
    case CFG_TTYPE_VT102:
      if( !vt52_mode ) { terminal_receive_char_vt102(c); break; }

    case CFG_TTYPE_VT52:
      terminal_receive_char_vt52(c);
      break;

    case CFG_TTYPE_PETSCII:
      terminal_receive_char_petscii(c);
      break;
    }
}



void INFLASHFUN terminal_receive_string(const char* str)
{
  while( *str ) { terminal_receive_char(*str); str++; }
}


static void INFLASHFUN send_cursor_sequence(char c)
{
  if( vt52_mode )
    { send_char(27); send_char(c); }
  else
    { send_char(27); send_char('['); send_char(c); }
}


static void INFLASHFUN terminal_process_key_vt(uint16_t key)
{
  uint8_t c = keyboard_map_key_ascii(key, NULL);
  switch( c )
    {
    case KEY_UP:     send_cursor_sequence('A'); break;
    case KEY_DOWN:   send_cursor_sequence('B'); break;
    case KEY_RIGHT:  send_cursor_sequence('C'); break;
    case KEY_LEFT:   send_cursor_sequence('D'); break;

    case KEY_ENTER:
      {
        switch( config_get_keyboard_enter() )
          {
          case 0: send_char(0x0d); break;
          case 1: send_char(0x0a); break;
          case 2: send_char(0x0d); send_char(0x0a); break;
          case 3: send_char(0x0a); send_char(0x0d); break;
          }
        break;
      }

    case KEY_BACKSPACE:
      {
        switch( config_get_keyboard_backspace() )
          {
          case 0: send_char(0x08); break;
          case 1: send_char(0x7f); break;
          }
        break;
      }

    case KEY_DELETE:
      {
        switch( config_get_keyboard_delete() )
          {
          case 0: send_char(0x08); break;
          case 1: send_char(0x7f); break;
          }
        break;
      }

    case KEY_INSERT:
      {
        terminal_receive_string(insert_mode ? "\033[4l" : "\033[4h");
        break;
      }

    case KEY_HOME:
      {
        terminal_receive_string(keyboard_shift_pressed(key) ? "\033[2J\033[H" : "\033[H");
        break;
      }

    default:  
      if( config_get_terminal_uppercase() && isalpha(c) ) c = toupper(c);
      send_char(c);
      break;
    }
}


static void INFLASHFUN terminal_process_key_petscii(uint16_t key)
{
  uint8_t cc = 0;

  // mapping the key to ASCII performs three important functions:
  // - apply mapping according to keyboard layout (language)
  // - map numpad keys to regular keys
  // - provide LeftAlt-NNN for entering specific codes
  bool isaltcode = false;
  uint8_t c = keyboard_map_key_ascii(key, &isaltcode);

  // if c is the result of user pressing LeftAlt-NNN then send without mapping
  if( isaltcode ) { send_char(c); return; }

  switch( c )
    {
    case KEY_UP:        cc = 145; break;
    case KEY_DOWN:      cc = 17;  break;
    case KEY_RIGHT:     cc = 29;  break;
    case KEY_LEFT:      cc = 157; break;
    case KEY_ENTER:     cc = 13;  break;
    case KEY_HOME:      cc = keyboard_shift_pressed(key) ? 147 : 19;  break;
    case KEY_BACKSPACE: cc = 20;  break;
    case KEY_DELETE:    cc = 20;  break;
    case KEY_INSERT:    cc = 148; break;
    case KEY_F1:        cc = 133; break;
    case KEY_F2:        cc = 137; break;
    case KEY_F3:        cc = 134; break;
    case KEY_F4:        cc = 138; break;
    case KEY_F5:        cc = 135; break;
    case KEY_F6:        cc = 139; break;
    case KEY_F7:        cc = 136; break;
    case KEY_F8:        cc = 140; break;
    case '`':           cc =  95; break; // left arrow 
    case '\\':          cc =  94; break; // up arrow
    case '|':           cc = 126; break; // pi
    case '_':           cc = 123; break; // full cross
    case '{':           cc = 186; break; // bottom right corner
    case '}':           cc = 192; break; // middle line
    case '-':           cc = keyboard_alt_pressed(key) ? 126 : c; break; // full checkerboard
    case '[':           cc = keyboard_alt_pressed(key) ? 164 : c; break; // underscore
    case ']':           cc = keyboard_alt_pressed(key) ? 223 : c; break; // top right triangle

    default:  
      {
        if( keyboard_alt_pressed(key) && c>='a' && c <='z' )
          {
            static const uint8_t gfx[26] = {176, 191, 188, 172, 177, 187, 165, 180, 162, 181, 161, 182, 167, 
                                            170, 185, 175, 171, 178, 174, 163, 184, 190, 179, 189, 183, 173};
            cc = gfx[c-'a'];
          }
        else if( keyboard_ctrl_pressed(key) && c>='0' && c<='9' )
          {
            static const uint8_t colors[10] = {146, 144, 5, 28, 159, 156, 30, 31, 158, 18};
            cc = colors[c-'0'];
          }
        else if( keyboard_alt_pressed(key) && c>='1' && c<='8' )
          {
            static const uint8_t colors[8] = {129, 149, 150, 151, 152, 153, 154, 155};
            cc = colors[c-'1'];
          }
        else if( keyboard_ctrl_pressed(key) && keyboard_shift_pressed(key) && (key&0xFF)==HID_KEY_Z )
          cc = petscii_lower_case_charset ? 142 : 14;
        else if( c>='a' && c<='z' )
          cc = c - 32;
        else if( c>='A' && c<='Z' )
          cc = c + 128;
        else //if( c<127 )
          cc = c;
      }
          
      break;
    }

  if( cc>0 ) send_char(cc);
}

void INFLASHFUN terminal_process_key(uint16_t key)
{
  if( (key&0xFF)==HID_KEY_PAUSE )
    {
      if( keyboard_ctrl_pressed(key) )
        {
          // CTRL-Pause/Break sends answerback message
          send_string(config_get_terminal_answerback());
        }
      else
        {
          // Pause/Break key sends BREAK condition on serial port
          serial_set_break(true);
          wait(MAX(1, (12000/config_get_serial_baud())));
          serial_set_break(false);
        }
    }
  else if( key==HID_KEY_F10 )
    {
      sound_play_tone(880, 50, config_get_audible_bell_volume(), false);
      localecho = !localecho;
    }
  else if( config_get_terminal_type()==2 )
    terminal_process_key_petscii(key);
  else
    terminal_process_key_vt(key);
}


void INFLASHFUN terminal_init()
{
  terminal_reset();
  terminal_clear_screen();
}


void INFLASHFUN terminal_apply_settings()
{
  terminal_init();
}
