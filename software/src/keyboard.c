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

#include "bsp/board.h"
#include "keyboard.h"
#include "keyboard_usb.h"
#include "keyboard_ps2.h"
#include "config.h"
#include "flash.h"
#include "sound.h"
#include "pico/util/queue.h"
#include "pico/time.h"
#include <ctype.h>
#include <stdlib.h>

// defined in main.c
void wait(uint32_t milliseconds);

//#define DEBUG
#define INFLASHFUN __in_flash(".kbdfun") 

#ifdef DEBUG
#include "terminal.h"
#include <stdarg.h>
static void INFLASHFUN print(const char *format, ...)
{
  char buffer[101];
  va_list argptr;
  va_start(argptr, format);
  vsnprintf(buffer, 100, format, argptr);
  terminal_receive_string(buffer);
  va_end(argptr);
}
#else
#define print(...)
#endif


static queue_t keyboard_queue;
static uint8_t keyboard_led_status = 0;
static uint8_t keyboard_modifiers  = 0;

const char *INFLASHFUN keyboard_get_keyname(uint16_t key)
{
  static char namebuf[100];
  static const char __in_flash(".configmenus") keynames[156][13] =
    {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", 
     "ENTER", "ESCAPE", "BACKSPACE", "TAB", "SPACE", "-", "=", "{", "}", "BACKSLASH", "EUROPE_1", ";", "'", "`", ",", ".", 
     "/", "CAPS_LOCK", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "PRINT_SCREEN", "SCROLL_LOCK", "PAUSE", "INSERT", "HOME", "PAGE_UP", "DELETE", 
     "END", "PAGE_DOWN", "RIGHT", "LEFT", "DOWN", "UP", "NUMLOCK", "KP_DIV", "KP_MULT", "KP_MINUS", "KP_PLUS", "KP_ENTER", 
     "KP_1", "KP_2", "KP_3", "KP_4", "KP_5", "KP_6", "KP_7", "KP_8", "KP_9", "KP_0", "KP_DECIMAL", "EUROPE_2", "APP", "POWER", 
     "KP_EQUAL", "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24", "EXECUTE", "HELP", "MENU", "SELECT", "STOP", "AGAIN", "UNDO", "CUT", "COPY", 
     "PASTE", "FIND", "MUTE", "VOL_UP", "VOL_DOWN", "CAPS_LOCK", "NUM_LOCK", "SCROLL_LOCK", "KP_COMMA", "KP_EQUAL", "KANJI1", "KANJI2", 
     "KANJI3", "KANJI4", "KANJI5", "KANJI6", "KANJI7", "KANJI8", "KANJI9", "LANG1", "LANG2", "LANG3", "LANG4", "LANG5", "LANG6", "LANG7", "LANG8", "LANG9", "ERASE", "SYSREQ", "CANCEL", 
     "CLEAR", "PRIOR", "RETURN", "SEPARATOR"};

  static const char __in_flash(".configmenus") modkeynames[8][10] =
    {"LeftCtrl", "LeftShift", "LeftAlt", "LeftGUI", "RightCtrl", "RightShift", "RightAlt", "RightGUI"};


  uint8_t buflen = 29;
  namebuf[0] = 0;

  uint8_t mod = key >> 8;
  if( (mod & KEYBOARD_MODIFIER_LEFTCTRL)!=0 )   
    { strncat(namebuf, "LeftCtrl-", buflen);  buflen -= 9; }
  if( (mod & KEYBOARD_MODIFIER_RIGHTCTRL)!=0 )
    { strncat(namebuf, "RightCtrl-", buflen);  buflen -= 10; }
  if( (mod & KEYBOARD_MODIFIER_LEFTALT)!=0 )
    { strncat(namebuf, "LeftAlt-", buflen);   buflen -= 8; }
  if( (mod & KEYBOARD_MODIFIER_RIGHTALT)!=0 )
    { strncat(namebuf, "RightAlt-", buflen);   buflen -= 9; }
  if( (mod & KEYBOARD_MODIFIER_LEFTGUI)!=0 )
    { strncat(namebuf, "LeftGUI-", buflen);   buflen -= 8; }
  if( (mod & KEYBOARD_MODIFIER_RIGHTGUI)!=0 )
    { strncat(namebuf, "RightGUI-", buflen);   buflen -= 9; }
  if( (mod & (KEYBOARD_MODIFIER_LEFTSHIFT|KEYBOARD_MODIFIER_RIGHTSHIFT))!=0 ) 
    { strncat(namebuf, "Shift-", buflen); buflen -= 6; }

  key &= 0xFF;
  if( key>=4 && key<4+156 )
    {
      if( strlen(keynames[key-4])==1 )
        snprintf(namebuf+strlen(namebuf), buflen, "%c", keynames[key-4][0]);
      else
        snprintf(namebuf+strlen(namebuf), buflen, "{%s}", keynames[key-4]);
    }
  else if( key>=0xE0 && key <= 0xE7 )
    snprintf(namebuf+strlen(namebuf), buflen, "{%s}", modkeynames[key-0xE0]);
  else
    snprintf(namebuf+strlen(namebuf), buflen, "[%02X]", key);
  
  return namebuf;
}


// -------------------------------------------  keyboard layouts (languages)  -------------------------------------------


struct IntlMapStruct
{
  uint8_t mapNormal[71];
  uint8_t mapShift[71];
  struct { int code; bool shift; int character; } mapOther[10];
  struct { int code; int character; } mapAltGr[12];
};


const struct IntlMapStruct __in_flash(".keymaps") intlMaps[7] = {
  { // US English
    {0    ,0    ,0    ,0    ,'a'  ,'b'  ,'c'  ,'d'  ,
     'e'  ,'f'  ,'g'  ,'h'  ,'i'  ,'j'  ,'k'  ,'l'  ,
     'm'  ,'n'  ,'o'  ,'p'  ,'q'  ,'r'  ,'s'  ,'t'  ,
     'u'  ,'v'  ,'w'  ,'x'  ,'y'  ,'z'  ,'1'  ,'2'  ,
     '3'  ,'4'  ,'5'  ,'6'  ,'7'  ,'8'  ,'9'  ,'0'  ,
     KEY_ENTER, KEY_ESC, KEY_BACKSPACE, KEY_TAB, ' ', '-', '=', '[',
     ']'  ,'\\' ,0    ,';'  ,'\'' ,'`'  ,','  ,'.'  ,
     '/'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6   ,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {0    ,0    ,0    ,0    ,'A'  ,'B'  ,'C'  ,'D'  ,
     'E'  ,'F'  ,'G'  ,'H'  ,'I'  ,'J'  ,'K'  ,'L'  ,
     'M'  ,'N'  ,'O'  ,'P'  ,'Q'  ,'R'  ,'S'  ,'T'  ,
     'U'  ,'V'  ,'W'  ,'X'  ,'Y'  ,'Z'  ,'!'  ,'@'  ,
     '#'  ,'$'  ,'%'  ,'^'  ,'&'  ,'*'  ,'('  ,')'  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,'_'  ,'+'  ,'{'  ,
     '}'  ,'|'  ,0    ,':'  ,'"'  ,'~'  ,'<'  ,'>'  ,
     '?'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6   ,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {{0x64, 0, '\\'}, {0x64, 1, '|'}, {-1,-1}},
    {{-1,-1}}
  },{ // UK English
    {0    ,0    ,0    ,0    ,'a'  ,'b'  ,'c'  ,'d'  ,
     'e'  ,'f'  ,'g'  ,'h'  ,'i'  ,'j'  ,'k'  ,'l'  ,
     'm'  ,'n'  ,'o'  ,'p'  ,'q'  ,'r'  ,'s'  ,'t'  ,
     'u'  ,'v'  ,'w'  ,'x'  ,'y'  ,'z'  ,'1'  ,'2'  ,
     '3'  ,'4'  ,'5'  ,'6'  ,'7'  ,'8'  ,'9'  ,'0'  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,'-'  ,'='  ,'['  ,
     ']'  ,'#'  ,0    ,';'  ,'\'' ,'`'  ,','  ,'.'  ,
     '/'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6   ,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {0    ,0    ,0    ,0    ,'A'  ,'B'  ,'C'  ,'D'  ,
     'E'  ,'F'  ,'G'  ,'H'  ,'I'  ,'J'  ,'K'  ,'L'  ,
     'M'  ,'N'  ,'O'  ,'P'  ,'Q'  ,'R'  ,'S'  ,'T'  ,
     'U'  ,'V'  ,'W'  ,'X'  ,'Y'  ,'Z'  ,'!'  ,'"'  ,
     '#'  ,'$'  ,'%'  ,'^'  ,'&'  ,'*'  ,'('  ,')'  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,'_'  ,'+'  ,'{'  ,
     '}'  ,'~'  ,0    ,':'  ,'@'  ,'~'  ,'<'  ,'>'  ,
     '?'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6   ,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {{0x64, 0, '\\'}, {0x64, 1, '|'}, {-1,-1}},
    {{-1,-1}}
  },{ // French
    {0    ,0    ,0    ,0    ,'q'  ,'b'  ,'c'  ,'d'  ,
     'e'  ,'f'  ,'g'  ,'h'  ,'i'  ,'j'  ,'k'  ,'l'  ,
     ','  ,'n'  ,'o'  ,'p'  ,'a'  ,'r'  ,'s'  ,'t'  ,
     'u'  ,'v'  ,'z'  ,'x'  ,'y'  ,'w'  ,'&'  ,0    ,
     '"'  ,'\'' ,'('  ,'-'  ,0    ,'_'  ,0    ,0    ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,')'  ,'='  ,'^'  ,
     '$'  ,'*'  ,0    ,'m'  ,0    ,0    ,';'  ,':'  ,
     '!'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6   ,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {0    ,0    ,0    ,0    ,'Q'  ,'B'  ,'C'  ,'D'  ,
     'E'  ,'F'  ,'G'  ,'H'  ,'I'  ,'J'  ,'K'  ,'L'  ,
     '?'  ,'N'  ,'O'  ,'P'  ,'A'  ,'R'  ,'S'  ,'T'  ,
     'U'  ,'V'  ,'Z'  ,'X'  ,'Y'  ,'W'  ,'1'  ,'2'  ,
     '3'  ,'4'  ,'5'  ,'6'  ,'7'  ,'8'  ,'9'  ,'0'  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,0    ,'+'  ,0    ,
     0    ,0    ,0    ,'M'  ,'%'  ,0    ,'.'  ,'/'  ,
     0    ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6   ,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {{0x64, 0, '<'}, {0x64, 1, '>'}, {-1,-1}},
    {{0x1f, '~'  }, {0x21, '{'  }, {0x20, '#'  }, {0x22, '['  }, 
     {0x23, '|'  }, {0x25, '\\' }, {0x27, '@'  }, {0x2d, ']'  }, 
     {0x2e, '}'  }, {0x64, '\\'  }, {-1,-1}}
  },{ // German
    {0    ,0    ,0    ,0    ,'a'  ,'b'  ,'c'  ,'d'  ,
     'e'  ,'f'  ,'g'  ,'h'  ,'i'  ,'j'  ,'k'  ,'l'  ,
     'm'  ,'n'  ,'o'  ,'p'  ,'q'  ,'r'  ,'s'  ,'t'  ,
     'u'  ,'v'  ,'w'  ,'x'  ,'z'  ,'y'  ,'1'  ,'2'  ,
     '3'  ,'4'  ,'5'  ,'6'  ,'7'  ,'8'  ,'9'  ,'0'  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,0    ,'\'' ,0    ,
     '+'  ,'#'  ,0    ,0    ,0    ,'^'  ,','  ,'.'  ,
     '-'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6   ,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {0    ,0    ,0    ,0    ,'A'  ,'B'  ,'C'  ,'D'  ,
     'E'  ,'F'  ,'G'  ,'H'  ,'I'  ,'J'  ,'K'  ,'L'  ,
     'M'  ,'N'  ,'O'  ,'P'  ,'Q'  ,'R'  ,'S'  ,'T'  ,
     'U'  ,'V'  ,'W'  ,'X'  ,'Z'  ,'Y'  ,'!'  ,'"'  ,
     0    ,'$'  ,'%'  ,'&'  ,'/'  ,'('  ,')'  ,'='  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,'?'  ,'`'  ,0    ,
     '*'  ,'\'' ,0    ,0    ,0    ,0    ,';'  ,':'  ,
     '_'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6   ,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {{0x64, 0, '<'}, {0x64, 1, '>'}, {-1,-1}},
    {{0x14, '@'  }, {0x24, '{'  }, {0x25, '['  }, {0x27, '}'  }, 
     {0x26, ']'  }, {0x2d, '\\' }, {0x30, '~'  }, {0x64, '|'  }, 
     {-1,-1}}
  },{ // Italian
    {0    ,0    ,0    ,0    ,'a'  ,'b'  ,'c'  ,'d'  ,
     'e'  ,'f'  ,'g'  ,'h'  ,'i'  ,'j'  ,'k'  ,'l'  ,
     'm'  ,'n'  ,'o'  ,'p'  ,'q'  ,'r'  ,'s'  ,'t'  ,
     'u'  ,'v'  ,'w'  ,'x'  ,'y'  ,'z'  ,'1'  ,'2'  ,
     '3'  ,'4'  ,'5'  ,'6'  ,'7'  ,'8'  ,'9'  ,'0'  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,'\'' ,0    ,0    ,
     '+'  ,0    ,0    ,0    ,0    ,'\\' ,','  ,'.'  ,
     '-'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6   ,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {0    ,0    ,0    ,0    ,'A'  ,'B'  ,'C'  ,'D'  ,
     'E'  ,'F'  ,'G'  ,'H'  ,'I'  ,'J'  ,'K'  ,'L'  ,
     'M'  ,'N'  ,'O'  ,'P'  ,'Q'  ,'R'  ,'S'  ,'T'  ,
     'U'  ,'V'  ,'W'  ,'X'  ,'Y'  ,'Z'  ,'!'  ,'"'  ,
     0    ,'$'  ,'%'  ,'&'  ,'/'  ,'('  ,')'  ,'='  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,'?'  ,'^'  ,0    ,
     '*'  ,0    ,0    ,0    ,0    ,'|'  ,';'  ,':'  ,
     '_'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6   ,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {{0x64, 0, '<'}, {0x64, 1, '>'}, {-1,-1}},
    {{0x33, '@'  }, {0x34, '#'  }, {0x2f, '['  }, {0x30, ']'  }, 
     {-1,-1}}
  },{ // Belgian
    {0    ,0    ,0    ,0    ,'q'  ,'b'  ,'c'  ,'d'  ,
     'e'  ,'f'  ,'g'  ,'h'  ,'i'  ,'j'  ,'k'  ,'l'  ,
     ','  ,'n'  ,'o'  ,'p'  ,'a'  ,'r'  ,'s'  ,'t'  ,
     'u'  ,'v'  ,'z'  ,'x'  ,'y'  ,'w'  ,'&'  ,0    ,
     '"'  ,'\'' ,'('  ,0    ,0    ,'!'  ,0    ,0    ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,')'  ,'-'  ,'^'  ,
     '$'  ,0    ,0    ,'m'  ,0    ,0    ,';'  ,':'  ,
     '='  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6   ,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {0    ,0    ,0    ,0    ,'Q'  ,'B'  ,'C'  ,'D'  ,
     'E'  ,'F'  ,'G'  ,'H'  ,'I'  ,'J'  ,'K'  ,'L'  ,
     '?'  ,'N'  ,'O'  ,'P'  ,'A'  ,'R'  ,'S'  ,'T'  ,
     'U'  ,'V'  ,'Z'  ,'X'  ,'Y'  ,'W'  ,'1'  ,'2'  ,
     '3'  ,'4'  ,'5'  ,'6'  ,'7'  ,'8'  ,'9'  ,'0'  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,0    ,'_'  ,0    ,
     '*'  ,0    ,0    ,'M'  ,'%'  ,0    ,'.'  ,'/'  ,
     '+'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6   ,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {{0x64, 0, '<'}, {0x64, 1, '>'}, {-1,-1}},
    {{0x1e, '|'  }, {0x64, '\\' }, {0x1f, '@'  }, {0x20, '#'  }, 
     {0x27, '}'  }, {0x26, '{'  }, {0x38, '~'  }, {0x2f, '['  }, 
     {0x30, ']'  }, {-1,-1}}
  }, { // Spanish
    {0    ,0    ,0    ,0    ,'a'  ,'b'  ,'c'  ,'d'  ,
     'e'  ,'f'  ,'g'  ,'h'  ,'i'  ,'j'  ,'k'  ,'l'  ,
     'm'  ,'n'  ,'o'  ,'p'  ,'q'  ,'r'  ,'s'  ,'t'  ,
     'u'  ,'v'  ,'w'  ,'x'  ,'y'  ,'z'  ,'1'  ,'2'  ,
     '3'  ,'4'  ,'5'  ,'6'  ,'7'  ,'8'  ,'9'  ,'0'  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,'\'' ,0    ,'`'  ,
     '+'  ,0    ,0    ,0    ,'\'' ,'\\' ,','  ,'.'  ,
     '-'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6   ,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {0    ,0    ,0    ,0    ,'A'  ,'B'  ,'C'  ,'D'  ,
     'E'  ,'F'  ,'G'  ,'H'  ,'I'  ,'J'  ,'K'  ,'L'  ,
     'M'  ,'N'  ,'O'  ,'P'  ,'Q'  ,'R'  ,'S'  ,'T'  ,
     'U'  ,'V'  ,'W'  ,'X'  ,'Y'  ,'Z'  ,'!'  ,'"'  ,
     0    ,'$'  ,'%'  ,'&'  ,'/'  ,'('  ,')'  ,'='  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,'?'  ,0    ,'^'  ,
     '*'  ,0    ,0    ,0    ,0    ,'\\' ,';'  ,':'  ,
     '_'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6   ,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {{0x64, 0, '<'}, {0x64, 1, '>'}, {-1,-1}},
    {{0x35, '\\' }, {0x1e, '|'  }, {0x1f, '@'  }, {0x21, '~'  }, 
     {0x20, '#'  }, {0x34, '{'  }, {0x2f, '['  }, {0x30, ']'  }, 
     {0x31, '}'  }, {-1,-1}}
  }
};

static const int keyMapKeypad[]    = {'\\', '*', '-', '+', KEY_ENTER, KEY_END, KEY_DOWN, KEY_PDOWN, KEY_LEFT, 0, KEY_RIGHT, KEY_HOME, KEY_UP, KEY_PUP, KEY_INSERT, KEY_DELETE};
static const int keyMapKeypadNum[] = {'\\', '*', '-', '+', KEY_ENTER , '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '.'};
static const int keyMapSpecial[]   = {KEY_PAUSE, KEY_INSERT, KEY_HOME, KEY_PUP, KEY_DELETE, KEY_END, KEY_PDOWN, KEY_RIGHT, KEY_LEFT, KEY_DOWN, KEY_UP};

static uint8_t keyboardLanguage = 0;


uint8_t INFLASHFUN keyboard_map_key_ascii(uint16_t k, bool *isaltcode)
{
  const struct IntlMapStruct *map = &(intlMaps[keyboardLanguage]);
  uint8_t i, ascii = 0;
  
  uint8_t key = k & 0xFF;
  uint8_t modifier = k >> 8;

  // left-alt plus numeric keypad allows emitting any ASCII code
  static uint8_t altcodecount = 0;
  static uint8_t altcode = 0;
  if( isaltcode!=NULL ) *isaltcode = false;
  if( (modifier & KEYBOARD_MODIFIER_LEFTALT)!=0 && key>=HID_KEY_KEYPAD_1 && key<=HID_KEY_KEYPAD_0 /*&& (keyboard_led_status&KEYBOARD_LED_NUMLOCK)!=0*/ )
    {
      altcode *= 10;
      altcode += (key==HID_KEY_KEYPAD_0) ? 0 : (key-HID_KEY_KEYPAD_1+1);
      altcodecount++;
      if( altcodecount==3 )
        {
          if( isaltcode!=NULL ) *isaltcode = true;
          ascii = altcode;
          altcode=0; 
          altcodecount=0;
        }

      return ascii;
    }
  else
    {
      altcode = 0;
      altcodecount = 0;
    }

  if( modifier & KEYBOARD_MODIFIER_RIGHTALT )
    {
      // AltGr pressed (international keyboard)
      for(i=0; map->mapAltGr[i].code>=0; i++)
        if( map->mapAltGr[i].code == key )
          { ascii = map->mapAltGr[i].character; break; }
    }
  else if( key <= HID_KEY_PRINT_SCREEN )
    {
      bool ctrl  = (modifier & (KEYBOARD_MODIFIER_LEFTCTRL|KEYBOARD_MODIFIER_RIGHTCTRL))!=0;
      bool shift = (modifier & (KEYBOARD_MODIFIER_LEFTSHIFT|KEYBOARD_MODIFIER_RIGHTSHIFT))!=0;
      bool caps  = (key >= HID_KEY_A) && (key <= HID_KEY_Z) && (keyboard_led_status & KEYBOARD_LED_CAPSLOCK)!=0;

      if( shift ^ caps )
        ascii = map->mapShift[key];
      else
        ascii = map->mapNormal[key];
      
      if( ctrl && ascii>=0x40 && ascii<0x7f )
        ascii &= 0x1f;
    }
  else if( (key >= HID_KEY_PAUSE) && (key <= HID_KEY_ARROW_UP) )
    {
      ascii = keyMapSpecial[key-HID_KEY_PAUSE];
    }
  else if( (key >= HID_KEY_KEYPAD_DIVIDE) && (key <= HID_KEY_KEYPAD_DECIMAL) )
    {
      if( (keyboard_led_status & KEYBOARD_LED_NUMLOCK)!=0 )
        ascii = keyMapKeypadNum[key-HID_KEY_KEYPAD_DIVIDE];
      else
        ascii = keyMapKeypad[key-HID_KEY_KEYPAD_DIVIDE];
    }
  else
    {
      bool shift = (modifier & (KEYBOARD_MODIFIER_LEFTSHIFT|KEYBOARD_MODIFIER_RIGHTSHIFT))!=0;

      for(i=0; map->mapOther[i].code>=0; i++)
        if( map->mapOther[i].code == key && map->mapOther[i].shift == shift )
          ascii = map->mapOther[i].character;
    }
  
  return ascii;
}



// ---------------------------------------------  keymap handling  ---------------------------------------------


static bool    keymap_mapping = false;
static uint8_t keymap_key_from;


void INFLASHFUN keyboard_keymap_map_start()
{
  keymap_mapping = true;
  keymap_key_from = HID_KEY_NONE;
}


bool keyboard_keymap_mapping(uint8_t *fromKey)
{
  if( fromKey!=NULL ) *fromKey = keymap_key_from;
  return keymap_mapping;
}


static bool INFLASHFUN keyboard_keymap_map_end(uint8_t key_from, uint8_t key_to)
{
  bool res = false;

  if( keymap_mapping )
    {
      if( key_from!=HID_KEY_NONE && key_to!=HID_KEY_NONE )
        {
          config_get_keyboard_user_mapping()[key_from] = key_to;
          res = true;
        }
      
      keymap_mapping = false;
    }
  
  return res;
}


// ----------------------------------------------  macro handling  ----------------------------------------------


#define MACRO_NONE     0
#define MACRO_PLAYBACK 1
#define MACRO_RECORD   2
#define MACRO_RECORD_START 3

uint8_t  macro_modifier_keys = 0;
uint8_t  macro_len = 0, macro_ptr = 0;
uint16_t macro_key, macro_data[256];
static uint8_t macro_status = MACRO_NONE;


#define MACRO_KEY(p)          p[0]
#define MACRO_DATA_LEN(p)     p[1]
#define MACRO_DATA(p)         (p+2)
#define MACRO_FIRST()         ((uint16_t *) config_get_keyboard_macros_start())
#define MACRO_NEXT(p)         (p+2+MACRO_DATA_LEN(p))
#define MACRO_EXTKEY(key,mod) ((key) | (mod) << 8)


static bool INFLASHFUN keyboard_find_macro(uint16_t key)
{
  for(uint16_t *p = MACRO_FIRST(); MACRO_KEY(p)!=0; p=MACRO_NEXT(p))
    if( MACRO_KEY(p)==key )
      {
        macro_len = MACRO_DATA_LEN(p);
        memcpy(macro_data, MACRO_DATA(p), macro_len*2);
        return true;
      }

  return false;
}


static bool INFLASHFUN keyboard_save_macro(uint16_t key, uint8_t data_len, uint16_t *data)
{
  // find macro (if it exists)
  uint16_t *p;
  for(p = MACRO_FIRST(); MACRO_KEY(p)!=key && MACRO_KEY(p)!=0; p=MACRO_NEXT(p));

  // find end of macro list
  uint16_t *pe;
  for(pe = p; MACRO_KEY(pe)!=0; pe=MACRO_NEXT(pe));

  // make space for new macro (replace current one if it exists)
  uint16_t *pn = p==pe ? p : MACRO_NEXT(p);
  int len = data_len==0 ? 0 : 2+data_len;
  memmove(p+len, pn, 2*(pe-pn+1));

  // copy in new macro (if not empty)
  if( len>0 )
    {
      MACRO_KEY(p)      = key;
      MACRO_DATA_LEN(p) = data_len;
      memcpy(MACRO_DATA(p), data, data_len*2);
    }

  //for(uint8_t *pp=MACRO_FIRST(); pp<p+4+strlen(name)+data_len*2+2; pp++) print("%02X", *pp);
  return true;
}


void keyboard_macro_clearall()
{
  MACRO_KEY(MACRO_FIRST()) = 0;
}

bool INFLASHFUN keyboard_set_macro_name(uint16_t key, const char *name)
{
  return false;
}


static void INFLASHFUN process_led_keys(uint8_t key, uint8_t modifier)
{
  uint8_t status = keyboard_led_status;

  if( key==HID_KEY_CAPS_LOCK )
    keyboard_led_status ^= KEYBOARD_LED_CAPSLOCK;
  else if( key==HID_KEY_NUM_LOCK )
    keyboard_led_status ^= KEYBOARD_LED_NUMLOCK;
  else if( key==HID_KEY_SCROLL_LOCK )
    keyboard_led_status ^= KEYBOARD_LED_SCROLLLOCK;
  
  if( keyboard_led_status != status )
    {
      keyboard_usb_set_led_status(keyboard_led_status);
      keyboard_ps2_set_led_status(keyboard_led_status);
    }
}


static void INFLASHFUN keyboard_add_keypress(uint8_t key, uint8_t modifier)
{
  //print("(%02X%02X-%s)", modifier, key, keyboard_get_keyname(modifier<<8 | key));

  if( macro_status==MACRO_RECORD_START )
    {
      macro_key    = MACRO_EXTKEY(key, modifier);
      macro_status = MACRO_RECORD;
    }
  else if( macro_status==MACRO_RECORD )
    {
      if( macro_len<255 )
        macro_data[macro_len++] = MACRO_EXTKEY(key, modifier);
      else
        sound_play_tone(880, 50, config_get_audible_bell_volume(), false);

      process_led_keys(key,modifier);
      queue_try_add(&keyboard_queue, &key); 
      queue_try_add(&keyboard_queue, &modifier); 
    }
  else if( macro_status==MACRO_NONE && !config_menu_active() && keyboard_find_macro(MACRO_EXTKEY(key, modifier)) )
    {
      macro_status = MACRO_PLAYBACK;
      macro_ptr=0; 
    }
  else
    {
      process_led_keys(key,modifier);
      queue_try_add(&keyboard_queue, &key); 
      queue_try_add(&keyboard_queue, &modifier); 
    }
}


void keyboard_key_change(uint8_t key, bool make)
{
  //print("[%02X:%i]", key, make ? 1 : 0);

  if( keymap_mapping )
    {
      if( make )
        {
          if( keymap_key_from==HID_KEY_NONE )
            keymap_key_from = key;
          else
            keyboard_keymap_map_end(keymap_key_from, key);
        }
    }
  else
    {
      // no keyboard mapping while config menu is active
      if( !config_menu_active() ) 
        key = config_get_keyboard_user_mapping()[key];

      uint8_t mod = 0;
      switch( key )
        {
        case HID_KEY_SHIFT_LEFT:    mod = KEYBOARD_MODIFIER_LEFTSHIFT;  break;
        case HID_KEY_SHIFT_RIGHT:   mod = KEYBOARD_MODIFIER_RIGHTSHIFT; break;
        case HID_KEY_CONTROL_LEFT:  mod = KEYBOARD_MODIFIER_LEFTCTRL;   break;
        case HID_KEY_CONTROL_RIGHT: mod = KEYBOARD_MODIFIER_RIGHTCTRL;  break;
        case HID_KEY_ALT_LEFT:      mod = KEYBOARD_MODIFIER_LEFTALT;    break;
        case HID_KEY_ALT_RIGHT:     mod = KEYBOARD_MODIFIER_RIGHTALT;   break;
        case HID_KEY_GUI_LEFT:      mod = KEYBOARD_MODIFIER_LEFTGUI;    break;
        case HID_KEY_GUI_RIGHT:     mod = KEYBOARD_MODIFIER_RIGHTGUI;   break;
        }

      if( mod!=0 )
        {
          if( make )
            keyboard_modifiers |= mod;
          else 
            keyboard_modifiers &= ~mod;
        }
      else if( make ) 
        keyboard_add_keypress(key, keyboard_modifiers);
    }
}


bool INFLASHFUN keyboard_macro_getfirst(KeyboardMacroInfo *info)
{
  info->next = MACRO_FIRST();
  return keyboard_macro_getnext(info);
}


bool INFLASHFUN keyboard_macro_getnext(KeyboardMacroInfo *info)
{
  info->key = (info->next-MACRO_FIRST()) < 2000 ? MACRO_KEY(info->next) : 0;
  if( info->key!=0 )
    {
      info->data_len = MACRO_DATA_LEN(info->next);
      info->data     = MACRO_DATA(info->next);
      info->next     = MACRO_NEXT(info->next);
      return true;
    }
  else
    return false;
}


bool keyboard_macro_recording()
{
  return macro_status==MACRO_RECORD_START || macro_status==MACRO_RECORD;
}


void INFLASHFUN keyboard_macro_record_start()
{
  if( macro_status==MACRO_NONE )
    {
      macro_len = 0;
      macro_status = MACRO_RECORD_START;
    }
}


bool INFLASHFUN keyboard_macro_record_stop()
{
  bool res = true;

  if( keyboard_macro_recording() )
    {
      // last keypress was for ending the macro recording => ignore it
      res = keyboard_save_macro(macro_key, macro_len-1, macro_data);
      macro_status = MACRO_NONE;
    }
  
  return res;
}


bool keyboard_macro_delete(uint16_t key)
{
  return keyboard_save_macro(key, 0, NULL);
}


void keyboard_macro_record_startstop()
{
  if( keyboard_macro_recording() )
    {
      if( !keyboard_macro_record_stop() )
        {
          // saving macro failed => triple beep
          sound_play_tone(880, 50, config_get_audible_bell_volume(), true); 
          wait(50);
        }
      
      // macro recording end => double beep
      sound_play_tone(880, 50, config_get_audible_bell_volume(), true); 
      wait(50);
    }
  else
    keyboard_macro_record_start();
  
  // macro record operation => beep
  sound_play_tone(880, 50, config_get_audible_bell_volume(), false);
}


// ----------------------------------------------  main functions  ----------------------------------------------


size_t INFLASHFUN keyboard_num_keypress()
{
  if( macro_status==MACRO_PLAYBACK )
    {
      if( macro_ptr==macro_len ) macro_status = MACRO_NONE; 
      return macro_len-macro_ptr;
    }
  else
    return queue_get_level(&keyboard_queue)/2;
}


uint16_t INFLASHFUN keyboard_read_keypress()
{
  uint16_t key = 0;

  if( macro_status==MACRO_PLAYBACK )
    {
      if( macro_ptr==macro_len ) macro_status = MACRO_NONE;
      if( macro_status!=MACRO_NONE )
        {
          key = macro_data[macro_ptr];
          macro_ptr++;
          process_led_keys(key&0xFF,key>>8);
        }
    }
  else
    {
      uint8_t b = 0;
      if( queue_try_remove(&keyboard_queue, &b) ) key  = b;
      if( queue_try_remove(&keyboard_queue, &b) ) key |= b<<8;
    }

  return key;
}


uint8_t keyboard_get_current_modifiers()
{
  return keyboard_modifiers;
}


bool keyboard_ctrl_pressed(uint16_t key)
{
  return (key & ((KEYBOARD_MODIFIER_LEFTCTRL|KEYBOARD_MODIFIER_RIGHTCTRL)<<8))!=0;
}


bool keyboard_alt_pressed(uint16_t key)
{
  return (key & ((KEYBOARD_MODIFIER_LEFTALT|KEYBOARD_MODIFIER_RIGHTALT)<<8))!=0;
}


bool keyboard_shift_pressed(uint16_t key)
{
  return (key & ((KEYBOARD_MODIFIER_LEFTSHIFT|KEYBOARD_MODIFIER_RIGHTSHIFT)<<8))!=0;
}


uint8_t INFLASHFUN keyboard_get_led_status()
{
  return keyboard_led_status;
}


void INFLASHFUN keyboard_task()
{
  keyboard_usb_task();
  keyboard_ps2_task();
}


void INFLASHFUN keyboard_apply_settings()
{
  keyboard_modifiers = 0;

  keyboardLanguage = config_get_keyboard_layout();
  keyboard_usb_apply_settings();
  keyboard_ps2_apply_settings();
}


void INFLASHFUN keyboard_init()
{
  keyboard_apply_settings();
  queue_init(&keyboard_queue, 1, 32);
  keyboard_usb_init();
  keyboard_ps2_init();
}


