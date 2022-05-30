#include "framebuf.h"
#include "terminal.h"
#include "keyboard.h"
#include "serial.h"
#include "config.h"
#include "font.h"
#include "flash.h"
#include "pins.h"
#include "sound.h"
#include "xmodem.h"
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "tusb.h"

#define CONFIG_MAGIC   0x0F1E2D3C
#define CONFIG_VERSION 0

// defined in main.c
void run_tasks(bool processInput);

#define INFLASHFUN __in_flash(".configfun") 

bool menuActive = false;
uint8_t currentConfig = 0;

static const uint8_t __in_flash(".configmenus") default_colors_ansi_dvi[16] =
  {0b000000, 0b100000, 0b001000, 0b101000, 0b000010, 0b100010, 0b001010, 0b101010, 
   0b010101, 0b110000, 0b001100, 0b111100, 0b000011, 0b110011, 0b001111, 0b111111};

static const uint8_t __in_flash(".configmenus") default_colors_ansi_vga[16] =
  {0b00000000, 0b10000000, 0b00010000, 0b10010000, 0b00000010, 0b10000010, 0b00010010, 0b10010010, 
   0b01001001, 0b11100000, 0b00011100, 0b11111100, 0b00000011, 0b11100011, 0b00011111, 0b11111111};

static const uint8_t __in_flash(".configmenus") default_colors_petscii_dvi[16] =
  {0b000000, 0b111111, 0b100000, 0b001010, 0b100010, 0b001000, 0b000010, 0b111100, 
   0b111000, 0b100100, 0b110000, 0b010101, 0b101010, 0b001100, 0b000011, 0b101010};

static const uint8_t __in_flash(".configmenus") default_colors_petscii_vga[16] =
  {0b00000000, 0b11111111, 0b10000000, 0b00010010, 0b10000010, 0b00010000, 0b00000010, 0b11111100, 
   0b10001000, 0b01000100, 0b11100000, 0b01001001, 0b10010010, 0b00011100, 0b00000011, 0b10110110};


static void INFLASHFUN print(const char *format, ...)
{
  char buffer[101];
  va_list argptr;
  va_start(argptr, format);
  vsnprintf(buffer, 100, format, argptr);
  terminal_receive_string(buffer);
  va_end(argptr);
}


static void INFLASHFUN printLines(uint8_t row, uint8_t col, int numlines, const char lines[][80])
{
  for(int i=0; i<numlines; i++)
    print("\033[%i;%iH%s", row+i, col, lines[i]);
}


static uint8_t waitkey(bool showCursor)
{
  if( showCursor ) print("\033[?25h");
  while( keyboard_num_keypress()==0 ) 
    run_tasks(false);
  if( showCursor ) print("\033[?25l");

  uint16_t key = keyboard_read_keypress();
  if( key==HID_KEY_F11 ) keyboard_macro_record_startstop();

  return keyboard_map_key_ascii(key, NULL);
}


int INFLASHFUN getstring(char *buf, size_t maxlen, bool clear, bool digitsOnly, bool allowCursorKeys)
{
  char bak[101];
  strncpy(bak, buf, 100);
  if( clear ) buf[0] = 0;
  print("%s", buf);
  size_t len = strlen(buf);
  while( 1 )
    {
      uint8_t c = waitkey(false);
      if( c==13 || (allowCursorKeys && (c==KEY_UP || c==KEY_DOWN || c==KEY_LEFT || c==KEY_RIGHT || c==KEY_PUP || c==KEY_PDOWN || c==KEY_HOME || c==KEY_END)) )
        { buf[len]=0; return c; }
      else if( c==27 )
        { strncpy(buf, bak, maxlen); return c; }
      else if( (c==8||c==127) && len>0 )
        { print("\b"); len--; }
      else if( (digitsOnly ? isdigit(c) : isprint(c)) && len<maxlen )
        { print("%c", c); buf[len++]=c; }
    }
}


static void INFLASHFUN printMenuFrame()
{
  print("\016\033[Hlqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqk\n");
  print("x\033[78Cx");
  print("tqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqu\n");
  for(int i=0; i<26; i++) print("x\033[78Cx");
  print("mqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqj\017");
}


static void INFLASHFUN clearToEndOfLine(int row, int col)
{
  print("\033[%i;%iH\033[K\033[%i;80H\016x\017", row, col, row);
}


struct SettingsHeaderStruct
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t startupConfig; // only used in config 0, specifies which config to load after boot
  char     name[64];
};


struct SettingsStruct
{
  struct SettingsHeaderStruct Header;
  
  struct SerialStruct
  {
    uint32_t baud; 
    uint16_t bits;
    uint16_t parity;
    uint16_t stopbits;
    uint16_t ctsmode;
    uint16_t rtsmode;
    uint16_t xonxoff;
    uint16_t blink;
    uint16_t reserved[16];
  } Serial;
  
  struct TerminalStruct
  {
    uint16_t ttype;
    uint16_t recvCR;
    uint16_t recvLF;
    uint16_t recvBS;
    uint16_t recvDEL;
    uint16_t echo;
    uint16_t cursor;
    uint16_t clearBit7;
    uint16_t uppercase;
    uint16_t fgcolor;
    uint16_t bgcolor;
    uint16_t attr;
    char     answerback[50];
    uint16_t reserved[32];
  } Terminal;

  struct KeyboardStruct
  {
    uint16_t layout;
    uint16_t enter;
    uint16_t backspace;
    uint16_t delete;
    uint16_t repdelay;
    uint16_t reprate;
    uint16_t scrolllock;
    uint8_t  user_mapping[256];
    uint16_t reserved[16];
  } Keyboard;

  struct ScreenStruct
  {
    uint16_t rows;
    uint16_t cols;
    uint16_t dblchars;
    uint16_t font;
    uint16_t bfont;
    uint16_t display;
    uint16_t mono;
    uint16_t blink;
    uint16_t splash;

    struct ColorStruct
    {
      uint8_t monobg;
      uint8_t mononormal;
      uint8_t monobold;
      uint8_t colors[16];
    } AnsiColorVGA, AnsiColorDVI, PetsciiColorVGA, PetsciiColorDVI;

    uint16_t reserved[16];
  } Screen;

  struct BellStruct
  {
    uint16_t sound_frequency;
    uint16_t sound_volume;
    uint16_t sound_duration;
    uint8_t  visual_color_vga;
    uint8_t  visual_color_dvi;
    uint16_t visual_duration;
    uint16_t reserved[8];
  } Bell;
  
  struct USBStruct
  {
    uint16_t mode;
    uint16_t cdcmode;
    uint16_t reserved[8];
  } USB;

  // must be last (macro data is added starting here)
  uint8_t keyboard_macros_start;
};


// flash sector size is 4096 bytes. We use one sector per configuration
// storing settings, key mapping and macros
union
{
  uint8_t data[4096];
  struct SettingsStruct settings;
} config;

#define settings config.settings


// -----------------------------------------------------------------------------------------------------------------

#define MI_USERFONT1 11
#define MI_USERFONT2 12
#define MI_USERFONT3 13
#define MI_USERFONT4 14

#define MI_FLASHCOLOR      21
#define MI_COLOR_MONO_BG   22
#define MI_COLOR_MONO_NORM 23
#define MI_COLOR_MONO_BOLD 24
#define MI_COLOR0          25
#define MI_COLOR1          26
#define MI_COLOR2          27
#define MI_COLOR3          28
#define MI_COLOR4          29
#define MI_COLOR5          30
#define MI_COLOR6          31
#define MI_COLOR7          32
#define MI_COLOR8          33
#define MI_COLOR9          34 
#define MI_COLOR10         35
#define MI_COLOR11         36
#define MI_COLOR12         37
#define MI_COLOR13         38
#define MI_COLOR14         39
#define MI_COLOR15         40

#define MI_PCOLOR_MONO_BG   47
#define MI_PCOLOR_MONO_NORM 48
#define MI_PCOLOR_MONO_BOLD 49
#define MI_PCOLOR0          50
#define MI_PCOLOR1          51
#define MI_PCOLOR2          52
#define MI_PCOLOR3          53
#define MI_PCOLOR4          54
#define MI_PCOLOR5          55
#define MI_PCOLOR6          56
#define MI_PCOLOR7          57
#define MI_PCOLOR8          58
#define MI_PCOLOR9          59 
#define MI_PCOLOR10         60
#define MI_PCOLOR11         61
#define MI_PCOLOR12         62
#define MI_PCOLOR13         63
#define MI_PCOLOR14         64
#define MI_PCOLOR15         65

#define IFT_QUERY           0
#define IFT_PRINT           1
#define IFT_EDIT            2
#define IFT_DEFAULT         4

#define HAVE_IFT(item, ift) ((item)->itemfun!=NULL && ((item)->itemfun(item, IFT_QUERY,0,0) & ift)!=0)

static int8_t defaults_force_dvi = -1;
static uint16_t menuIdPathLen = 0, menuIdPath[10];


int find_menu_item_id(uint16_t range_start, uint16_t range_end)
{
  for(int i=0; i<menuIdPathLen; i++)
    if( menuIdPath[i]>=range_start && menuIdPath[i]<=range_end )
      return menuIdPath[i];
  
  return -1;
}


struct MenuItemStruct
{
  char key;
  const char label[40];
  uint16_t itemId;
  const struct MenuItemStruct *submenuItems;
  uint8_t submenuItemsNum;
  int (*itemfun)(const struct MenuItemStruct *, int, int, int);
  uint16_t *value;
  
  uint16_t min, max, step, def;
  const char valueLabels[11][40];
};

#define NUM_MENU_ITEMS(menu) (sizeof(menu)/sizeof(struct MenuItemStruct))


static int configs_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int answerback_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int baud_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int color_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int ttype_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int color16_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int attr_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int keyboard_reprate_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int keyboard_key_mapping_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int keyboard_macro_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int user_font_menulabel_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int user_font_upload_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int user_font_height_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int user_font_underline_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int user_font_view_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int user_font_graphics_mapping_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int user_font_name_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int bell_test_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int displaytype_fn(const struct MenuItemStruct *item, int callType, int row, int col);
static int usbtype_fn(const struct MenuItemStruct *item, int callType, int row, int col);


static const struct MenuItemStruct __in_flash(".configmenus") serialMenu[] =
    {{'1', "Baud rate",           0, NULL, 0, baud_fn},
     {'2', "Bits",                0, NULL, 0, NULL, &settings.Serial.bits,     7,  8, 1, 8},
     {'3', "Parity",              0, NULL, 0, NULL, &settings.Serial.parity,   0,  4, 1, 0, {"none", "even", "odd", "mark", "space"}},
     {'4', "Stop bits",           0, NULL, 0, NULL, &settings.Serial.stopbits, 1,  2, 1, 1},
     {'5', "RTS control line",    0, NULL, 0, NULL, &settings.Serial.rtsmode,   0,  2, 1, 0, {"Always assert (always low)", "Never assert (always high)", "Assert when ready to receive"}},
     {'6', "CTS control line",    0, NULL, 0, NULL, &settings.Serial.ctsmode,   0,  1, 1, 0, {"Ignore", "Only send data if asserted"}},
     {'7', "XOn/XOff control",    0, NULL, 0, NULL, &settings.Serial.xonxoff,   0,  2, 1, 0, {"Disabled", "Enabled", "Enabled and FIFOs disabled"}},
     {'8', "LED blink time (ms)", 0, NULL, 0, NULL, &settings.Serial.blink,    0,  1000, 25, 50}};


static const struct MenuItemStruct __in_flash(".configmenus") bellMenu[] =
    {{'1', "Beep Frequency (Hz)",     0, NULL, 0, NULL, &settings.Bell.sound_frequency,  200, 20000, 10, 440},
     {'2', "Beep Volume",             0, NULL, 0, NULL, &settings.Bell.sound_volume,       0,    10,  1,   5},
     {'3', "Beep Duration (ms)",      0, NULL, 0, NULL, &settings.Bell.sound_duration,     0,  2000, 10, 100},
     {'4', "Flash Color",             MI_FLASHCOLOR, NULL, 0, color_fn},
     {'5', "Flash Duration (frames)", 0, NULL, 0, NULL, &settings.Bell.visual_duration, 0,    60,  1,   0},
     {'6', "Test bell",               0, NULL, 0, bell_test_fn}};


static const struct MenuItemStruct __in_flash(".configmenus") terminalMenu[] =
    {{'1', "Type",                     0, NULL, 0, ttype_fn, &settings.Terminal.ttype,      0, 2, 1, 0, {"VT102/Ansi", "VT52", "PETSCII"}},
     {'2', "Receiving CR  (0x0d)",     0, NULL, 0, NULL, &settings.Terminal.recvCR,     0, 3, 1, 1, {"ignore", "CR", "LF", "CR+LF"}},
     {'3', "Receiving LF  (0x0a)",     0, NULL, 0, NULL, &settings.Terminal.recvLF,     0, 3, 1, 2, {"ignore", "CR", "LF", "CR+LF"}},
     {'4', "Receiving BS  (0x08)",     0, NULL, 0, NULL, &settings.Terminal.recvBS,     0, 2, 1, 1, {"ignore", "backspace", "backspace+space+backspace"}},
     {'5', "Receiving DEL (0x7f)",     0, NULL, 0, NULL, &settings.Terminal.recvDEL,    0, 2, 1, 2, {"ignore", "backspace", "backspace+space+backspace"}},
     {'6', "Clear received bit 7",     0, NULL, 0, NULL, &settings.Terminal.clearBit7,  0, 1, 1, 0, {"off", "on"}},
     {'7', "Send all uppercase",       0, NULL, 0, NULL, &settings.Terminal.uppercase,  0, 1, 1, 0, {"off", "on"}},
     {'8', "Local echo",               0, NULL, 0, NULL, &settings.Terminal.echo,       0, 1, 1, 0, {"off", "on"}},
     {'9', "Cursor shape",             0, NULL, 0, NULL, &settings.Terminal.cursor,     0, 2, 1, 0, {"static box", "blinking box", "underline"}},
     {'a', "Default background color", 0, NULL, 0, color16_fn, &settings.Terminal.bgcolor, 0, 15, 1,  0},
     {'b', "Default text color",       0, NULL, 0, color16_fn, &settings.Terminal.fgcolor, 0, 15, 1,  7},
     {'c', "Default text attributes",  0, NULL, 0, attr_fn,    &settings.Terminal.attr,    0, 15, 1,  0},
     {'d', "Answerback message",       0, NULL, 0, answerback_fn}};



static const struct MenuItemStruct __in_flash(".configmenus") keyboardMenu[] =
    {{'1', "Layout",              0, NULL, 0, NULL, &settings.Keyboard.layout,     0,  6, 1, 0, {"English (US)", "English (UK)", "French", "German", "Italian", "Belgian", "Spanish"}},
     {'2', "Enter key sends",     0, NULL, 0, NULL, &settings.Keyboard.enter,      0,  4, 1, 0, {"CR", "LF", "CR+LF", "LF+CR", "nothing"}},
     {'3', "Backspace key sends", 0, NULL, 0, NULL, &settings.Keyboard.backspace,  0,  2, 1, 0, {"backspace (0x08)", "delete (0x7f)", "nothing"}},
     {'4', "Delete key sends",    0, NULL, 0, NULL, &settings.Keyboard.delete,     0,  2, 1, 1, {"backspace (0x08)", "delete (0x7f)", "nothing"}},
     {'5', "Scroll Lock key",     0, NULL, 0, NULL, &settings.Keyboard.scrolllock, 0,  1, 1, 1, {"ignored", "prevents scrolling"}},
     {'6', "Key repeat delay",    0, NULL, 0, NULL, &settings.Keyboard.repdelay,   0,  3, 1, 3, {"1000ms", "750ms", "500ms", "250ms"}},
     {'7', "Key repeat rate",     0, NULL, 0, keyboard_reprate_fn, &settings.Keyboard.reprate, 0, 31, 1, 25},
     {'8', "Key mapping",         0, NULL, 0, keyboard_key_mapping_fn},
     {'9', "Keyboard macros",     0, NULL, 0, keyboard_macro_fn}};


static const struct MenuItemStruct __in_flash(".configmenus") screenAnsiColorMenu[] =
    {{'0', "Monochrome background",      MI_COLOR_MONO_BG,   NULL, 0, color_fn},
     {'1', "Monochrome normal text",     MI_COLOR_MONO_NORM, NULL, 0, color_fn},
     {'2', "Monochrome bold text",       MI_COLOR_MONO_BOLD, NULL, 0, color_fn},
     {'a', "Color  0 (black)",           MI_COLOR0,          NULL, 0, color_fn},
     {'b', "Color  1 (red)",             MI_COLOR1,          NULL, 0, color_fn},
     {'c', "Color  2 (green)",           MI_COLOR2,          NULL, 0, color_fn},
     {'d', "Color  3 (yellow)",          MI_COLOR3,          NULL, 0, color_fn},
     {'e', "Color  4 (blue)",            MI_COLOR4,          NULL, 0, color_fn},
     {'f', "Color  5 (magenta)",         MI_COLOR5,          NULL, 0, color_fn},
     {'g', "Color  6 (cyan)",            MI_COLOR6,          NULL, 0, color_fn},
     {'h', "Color  7 (light gray)",      MI_COLOR7,          NULL, 0, color_fn},
     {'i', "Color  8 (dark gray)",       MI_COLOR8,          NULL, 0, color_fn},
     {'j', "Color  9 (bright red)",      MI_COLOR9,          NULL, 0, color_fn},
     {'k', "Color 10 (bright green)",    MI_COLOR10,         NULL, 0, color_fn},
     {'l', "Color 11 (bright yellow)",   MI_COLOR11,         NULL, 0, color_fn},
     {'m', "Color 12 (bright blue)",     MI_COLOR12,         NULL, 0, color_fn},
     {'n', "Color 13 (bright magenta)",  MI_COLOR13,         NULL, 0, color_fn},
     {'o', "Color 14 (bright cyan)",     MI_COLOR14,         NULL, 0, color_fn},
     {'p', "Color 15 (white)",           MI_COLOR15,         NULL, 0, color_fn}};


static const struct MenuItemStruct __in_flash(".configmenus") screenPetsciiColorMenu[] =
    {{'0', "Monochrome background",      MI_PCOLOR_MONO_BG,  NULL, 0, color_fn},
     {'1', "Monochrome normal text",     MI_PCOLOR_MONO_NORM,NULL, 0, color_fn},
     {'2', "Monochrome bold text",       MI_PCOLOR_MONO_BOLD,NULL, 0, color_fn},
     {'a', "Color  0 (black)",           MI_PCOLOR0,         NULL, 0, color_fn},
     {'b', "Color  1 (white)",           MI_PCOLOR1,         NULL, 0, color_fn},
     {'c', "Color  2 (red)",             MI_PCOLOR2,         NULL, 0, color_fn},
     {'d', "Color  3 (cyan)",            MI_PCOLOR3,         NULL, 0, color_fn},
     {'e', "Color  4 (purple)",          MI_PCOLOR4,         NULL, 0, color_fn},
     {'f', "Color  5 (green)",           MI_PCOLOR5,         NULL, 0, color_fn},
     {'g', "Color  6 (blue)",            MI_PCOLOR6,         NULL, 0, color_fn},
     {'h', "Color  7 (yellow)",          MI_PCOLOR7,         NULL, 0, color_fn},
     {'i', "Color  8 (orange)",          MI_PCOLOR8,         NULL, 0, color_fn},
     {'j', "Color  9 (brown)",           MI_PCOLOR9,         NULL, 0, color_fn},
     {'k', "Color 10 (light red)",       MI_PCOLOR10,        NULL, 0, color_fn},
     {'l', "Color 11 (dark grey)",       MI_PCOLOR11,        NULL, 0, color_fn},
     {'m', "Color 12 (grey)",            MI_PCOLOR12,        NULL, 0, color_fn},
     {'n', "Color 13 (light green)",     MI_PCOLOR13,        NULL, 0, color_fn},
     {'o', "Color 14 (light blue)",      MI_PCOLOR14,        NULL, 0, color_fn},
     {'p', "Color 15 (light grey)",      MI_PCOLOR15,        NULL, 0, color_fn}};


static const struct MenuItemStruct __in_flash(".configmenus") screenMenu[] =
    {{'1', "Display type",               0, NULL, 0, displaytype_fn, &settings.Screen.display,  0,   2, 1,  0, {"Auto-detect", "DVI/HDMI", "VGA"}},
     {'2', "Rows",                       0, NULL, 0, NULL, &settings.Screen.rows,    10,  60, 1, 30},
     {'3', "Columns",                    0, NULL, 0, NULL, &settings.Screen.cols,    20,  80, 2, 80},
     {'4', "Double size characters",     0, NULL, 0, NULL, &settings.Screen.dblchars, 0,   1, 1,  1, {"never", "if screen space allows"}},
     {'5', "Show splash screen",         0, NULL, 0, NULL, &settings.Screen.splash,   0,   1, 1,  1, {"no", "yes"}},
     {'6', "Blink period (frames)",      0, NULL, 0, NULL, &settings.Screen.blink,    2, 120, 2, 60},
     {'7', "Color/Monochrome",           0, NULL, 0, NULL, &settings.Screen.mono,     0,   1, 1,  0, {"Color", "Monochrome"}},
     {'8', "Ansi Colors",                0, screenAnsiColorMenu,    NUM_MENU_ITEMS(screenAnsiColorMenu)},
     {'9', "PETSCII Colors",             0, screenPetsciiColorMenu, NUM_MENU_ITEMS(screenPetsciiColorMenu)}};


static const struct MenuItemStruct __in_flash(".configmenus") userFontMenu[] =
    {{'1', "Upload font bitmap",           0, NULL, 0, user_font_upload_fn},
     {'2', "Font name",                    0, NULL, 0, user_font_name_fn},
     {'3', "Character height (read-only)", 0, NULL, 0, user_font_height_fn},
     {'4', "Underline row",                0, NULL, 0, user_font_underline_fn},
     {'5', "View font",                    0, NULL, 0, user_font_view_fn},
     {'6', "VT100 graphics characters",    0, NULL, 0, user_font_graphics_mapping_fn}};


static const struct MenuItemStruct __in_flash(".configmenus") fontMenu[] =
    {{'1', "Normal font",      0, NULL, 0, NULL, &settings.Screen.font,  1, 10,  1, 3,   {"None", "CGA (8x8)", "EGA (8x14)", "VGA", "Terminus", "Terminus bold", "PETSCII", "User 1", "User 2", "User 3", "User 4"}},
     {'2', "Bold font",        0, NULL, 0, NULL, &settings.Screen.bfont, 0, 10,  1, 0,   {"None", "CGA (8x8)", "EGA (8x14)", "VGA", "Terminus", "Terminus bold", "PETSCII", "User 1", "User 2", "User 3", "User 4"}},
     {'3', "Edit user font 1", MI_USERFONT1, userFontMenu, NUM_MENU_ITEMS(userFontMenu), user_font_menulabel_fn},
     {'4', "Edit user font 2", MI_USERFONT2, userFontMenu, NUM_MENU_ITEMS(userFontMenu), user_font_menulabel_fn},
     {'5', "Edit user font 3", MI_USERFONT3, userFontMenu, NUM_MENU_ITEMS(userFontMenu), user_font_menulabel_fn},
     {'6', "Edit user font 4", MI_USERFONT4, userFontMenu, NUM_MENU_ITEMS(userFontMenu), user_font_menulabel_fn}};


static const struct MenuItemStruct __in_flash(".configmenus") usbMenu[] =
    {{'1', "USB port mode",  0, NULL, 0, usbtype_fn, &settings.USB.mode,    0, 3, 1, 3, {"Disabled", "Device", "Host", "Auto-detect"}},
     {'2', "USB CDC device mode", 0, NULL, 0, NULL, &settings.USB.cdcmode, 0, 3, 1, 1, {"Disabled", "Serial", "Pass-through", "Pass-through (terminal disabled)"}}};


static const struct MenuItemStruct __in_flash(".configmenus") mainMenu[] =
    {{'1', "Serial settings",    0, serialMenu, NUM_MENU_ITEMS(serialMenu)},
     {'2', "Terminal settings",  0, terminalMenu, NUM_MENU_ITEMS(terminalMenu)},
     {'3', "Keyboard settings",  0, keyboardMenu, NUM_MENU_ITEMS(keyboardMenu)},
     {'4', "Screen settings",    0, screenMenu, NUM_MENU_ITEMS(screenMenu)},
     {'5', "Font settings",      0, fontMenu, NUM_MENU_ITEMS(fontMenu)},
     {'6', "Bell settings",      0, bellMenu, NUM_MENU_ITEMS(bellMenu)},
     {'7', "USB settings" ,      0, usbMenu, NUM_MENU_ITEMS(usbMenu)},
     {'8', "Manage configurations", 0, NULL, 0, configs_fn}};


// -----------------------------------------------------------------------------------------------------------------


static uint16_t get_current_usbmode()
{
  if( settings.USB.mode==CFG_USBMODE_AUTODETECT )
    {
      if( tuh_inited() )
        return CFG_USBMODE_HOST;
      else if( tud_inited() )
        return CFG_USBMODE_DEVICE;
      else
        return CFG_USBMODE_OFF;
    }
  else
    return settings.USB.mode;
}


static uint16_t get_current_displaytype()
{
  if( settings.Screen.display==CFG_DISPTYPE_AUTODETECT )
    return framebuf_is_dvi() ? CFG_DISPTYPE_DVI : CFG_DISPTYPE_VGA;
  else
    return settings.Screen.display;
}


static uint16_t get_keyboard_repeat_rate_mHz(uint8_t rate)
{
  static const uint16_t rates[32] = 
    {2000, 2100, 2300, 2500, 2700, 3000, 3300, 3700, 
     4000, 4300, 4600, 5000, 5500, 6000, 6700, 7500, 
     8000, 8600, 9200, 10000, 10900, 12000, 13300, 15000, 
     16000, 17100, 18500, 20000, 21800, 24000, 26700, 30000};
  
  return rates[rate&0x1F];
}

static uint16_t get_keyboard_repeat_delay_ms(uint8_t delay)
{
  static const uint16_t delays[4] = {1000, 750, 500, 250};
  return delays[delay&3];
}


uint32_t config_get_serial_baud()
{
  return settings.Serial.baud;
}

uint8_t config_get_serial_bits()
{
  return settings.Serial.bits;
}

char config_get_serial_parity()
{
  return "NEOMS"[settings.Serial.parity];
}

uint8_t  config_get_serial_stopbits()
{
  return settings.Serial.stopbits;
}

uint8_t  config_get_serial_ctsmode()
{
  return settings.Serial.ctsmode;
}

uint8_t  config_get_serial_rtsmode()
{
  return settings.Serial.rtsmode;
}

uint8_t  config_get_serial_xonxoff()
{
  return settings.Serial.xonxoff;
}

uint16_t  config_get_serial_blink()
{
  return settings.Serial.blink;
}

uint8_t config_get_terminal_type()
{
  return menuActive ? CFG_TTYPE_VT102 : settings.Terminal.ttype;
}

uint8_t config_get_terminal_localecho()
{
  return menuActive ? 0 : settings.Terminal.echo;
}

uint8_t config_get_terminal_cursortype()
{
  return menuActive ? 0 : settings.Terminal.cursor;
}

uint8_t config_get_terminal_cr()
{
  return menuActive ? 1 : settings.Terminal.recvCR;
}

uint8_t config_get_terminal_lf()
{
  return menuActive ? 3 : settings.Terminal.recvLF;
}

uint8_t config_get_terminal_bs()
{
  return menuActive ? 2 : settings.Terminal.recvBS; 
}

uint8_t config_get_terminal_del()
{
  return menuActive ? 2 : settings.Terminal.recvDEL;
}

bool config_get_terminal_clearBit7()
{
  return menuActive ? false : settings.Terminal.clearBit7!=0;
}

bool config_get_terminal_uppercase()
{
  return settings.Terminal.uppercase!=0;
}

uint8_t config_get_terminal_default_fg()
{
  return menuActive ? 7 : settings.Terminal.fgcolor;
}

uint8_t config_get_terminal_default_bg()
{
  return menuActive ? 0 : settings.Terminal.bgcolor;
}

uint8_t config_get_terminal_default_attr()
{
  return menuActive ? 0 : settings.Terminal.attr;
}

const char *config_get_terminal_answerback()
{
  return settings.Terminal.answerback;
}

uint8_t config_get_screen_rows()
{
  return menuActive ? 30 : settings.Screen.rows;
}

uint8_t config_get_screen_cols()
{
  return menuActive ? 80 : settings.Screen.cols;
}

bool config_get_screen_dblchars()
{
  return menuActive ? false : settings.Screen.dblchars!=0;
}

uint8_t config_get_screen_font_normal()
{
  return menuActive ? FONT_ID_VGA: settings.Screen.font;
}

uint8_t config_get_screen_font_bold()
{
  return menuActive ? FONT_ID_NONE : settings.Screen.bfont;
}

uint8_t config_get_screen_blink_period()
{
  return menuActive ? 60 : settings.Screen.blink;
}

uint8_t config_get_screen_display()
{
  return settings.Screen.display;
}

bool config_get_screen_monochrome()
{
  return menuActive ? false : settings.Screen.mono!=0;
}

uint8_t config_get_screen_monochrome_backgroundcolor(bool dvi)
{
  return dvi ? settings.Screen.AnsiColorDVI.monobg : settings.Screen.AnsiColorVGA.monobg;
}

uint8_t config_get_screen_monochrome_textcolor_normal(bool dvi)
{
  return dvi ? settings.Screen.AnsiColorDVI.mononormal : settings.Screen.AnsiColorVGA.mononormal;
}

uint8_t config_get_screen_monochrome_textcolor_bold(bool dvi)
{
  return dvi ? settings.Screen.AnsiColorDVI.monobold : settings.Screen.AnsiColorVGA.monobold;
}

uint8_t config_get_screen_color(uint8_t color, bool dvi)
{
  if( menuActive )
    return dvi ? default_colors_ansi_dvi[color&15] : default_colors_ansi_vga[color&15];
  else
    {
      if( settings.Terminal.ttype==CFG_TTYPE_PETSCII )
        return dvi ? settings.Screen.PetsciiColorDVI.colors[color&15] : settings.Screen.PetsciiColorVGA.colors[color&15];
      else
        return dvi ? settings.Screen.AnsiColorDVI.colors[color&15] : settings.Screen.AnsiColorVGA.colors[color&15];
    }
}

uint8_t config_get_keyboard_layout()
{
  return settings.Keyboard.layout;
}

uint8_t config_get_keyboard_enter()
{
  return settings.Keyboard.enter;
}

uint8_t config_get_keyboard_backspace()
{
  return settings.Keyboard.backspace;
}

uint8_t config_get_keyboard_delete()
{
  return settings.Keyboard.delete;
}


uint8_t config_get_keyboard_scroll_lock()
{
  return settings.Keyboard.scrolllock;
}

uint8_t config_get_keyboard_repeat_delay()
{
  return settings.Keyboard.repdelay;
}

uint8_t config_get_keyboard_repeat_rate()
{
  return settings.Keyboard.reprate;
}

uint16_t config_get_keyboard_repeat_delay_ms()
{
  return get_keyboard_repeat_delay_ms(settings.Keyboard.repdelay);
}

uint16_t config_get_keyboard_repeat_rate_mHz()
{
  return get_keyboard_repeat_rate_mHz(settings.Keyboard.reprate);
}

uint8_t config_get_usb_mode()
{
  return settings.USB.mode;
}

uint8_t config_get_usb_cdcmode()
{
  return get_current_usbmode()==CFG_USBMODE_DEVICE ? settings.USB.cdcmode : 0;
}

uint16_t config_get_audible_bell_frequency()
{
  return settings.Bell.sound_frequency;
}

uint16_t config_get_audible_bell_volume()
{
  return settings.Bell.sound_volume * 10;
}

uint16_t config_get_audible_bell_duration()
{
  return settings.Bell.sound_duration;
}

uint16_t config_get_visual_bell_color()
{
  return framebuf_is_dvi() ? settings.Bell.visual_color_dvi : settings.Bell.visual_color_vga;
}

uint8_t config_get_visual_bell_duration()
{
  return settings.Bell.visual_duration;
}


// -----------------------------------------------------------------------------------------------------------------

static void changeItemValue(const struct MenuItemStruct *item, int row, int col);


void INFLASHFUN setDefaults(const struct MenuItemStruct *items, uint8_t numItems, bool recurse)
{
  for(int i=0; i<numItems; i++)
    {
      menuIdPath[menuIdPathLen++] = items[i].itemId;
      if( items[i].value!=NULL ) 
        *(items[i].value) = items[i].def;
      if( items[i].itemfun!=NULL )
        (items[i].itemfun)(&items[i], IFT_DEFAULT, 0, 0);
      if( items[i].submenuItems!=NULL && recurse )
        setDefaults(items[i].submenuItems, items[i].submenuItemsNum, recurse);
      menuIdPathLen--;
    }
}


INFLASHFUN bool loadConfig(uint8_t n)
{
  struct SettingsHeaderStruct header;

  // read header information to validate
  flash_read(n, &header, sizeof(struct SettingsHeaderStruct));
  if( header.magic != CONFIG_MAGIC )
    return false;
  else if( sizeof(struct SettingsStruct)!=header.size || header.version!=CONFIG_VERSION )
    return false;
  else
    flash_read(n, &config.data, sizeof(config.data));
  
  currentConfig = n;
  return true;
}


static bool saveConfig(uint8_t n)
{
  settings.Header.magic   = CONFIG_MAGIC;
  settings.Header.version = CONFIG_VERSION;
  settings.Header.size    = sizeof(struct SettingsStruct);

  if( flash_write(n, &config.data, sizeof(config.data))!=0 )
    { currentConfig = n; return true; }
  else
    return false;
}



static uint8_t INFLASHFUN get_userfont_num()
{
  int id = find_menu_item_id(MI_USERFONT1, MI_USERFONT4);
  return id<0 ? 0xFF : id-MI_USERFONT1;
}


static int INFLASHFUN user_font_menulabel_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;
  if( callType==IFT_QUERY )
    res = IFT_PRINT;
  else if( callType==IFT_PRINT )
    {
      uint8_t fontNum = FONT_ID_USER1+get_userfont_num();
      uint8_t charHeight = 0;
      font_get_font_info(fontNum, NULL, NULL, &charHeight, NULL);
      print("%s", charHeight>0 ? font_get_name(fontNum) : "[not defined]");
    }
  
  return res;
}


static int INFLASHFUN user_font_height_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;
  if( callType==IFT_QUERY )
    res = IFT_PRINT | IFT_EDIT;
  else if( callType==IFT_PRINT )
    {
      uint8_t charHeight;
      font_get_font_info(FONT_ID_USER1+get_userfont_num(), NULL, NULL, &charHeight, NULL);
      print("%i", charHeight);
    }
  else if( callType==IFT_EDIT )
    {
      // read-only
    }

  return res;
}


static int INFLASHFUN user_font_underline_fn(const struct MenuItemStruct *_item, int callType, int row, int col)
{
  int res = 0;
  uint16_t underlineRow;
  uint8_t ur, charHeight, fontNum = FONT_ID_USER1+get_userfont_num();
  font_get_font_info(fontNum, NULL, NULL, &charHeight, &ur);
  underlineRow = ur;
  struct MenuItemStruct item;
  memset(&item, 0, sizeof(struct MenuItemStruct));
  item.value = &underlineRow;

  if( callType==IFT_QUERY )
    res = IFT_PRINT | IFT_EDIT;
  if( callType==IFT_PRINT )
    print("%i", *item.value);
  else if( callType==IFT_EDIT && charHeight>0 )
    {
      item.min  = 0;
      item.max  = charHeight-1;
      item.step = 1;
      changeItemValue(&item, row, col);
      font_set_underline_row(fontNum, underlineRow);
    }
  
  return res;
}


static int INFLASHFUN user_font_name_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;

  if( callType==IFT_QUERY )
    res = IFT_PRINT | IFT_EDIT;
  else if( callType==IFT_PRINT )
    print("%s", font_get_name(FONT_ID_USER1+get_userfont_num()));
  else if( callType==IFT_EDIT )
    {
      char name[32];
      uint8_t fontId = FONT_ID_USER1+get_userfont_num();
      strncpy(name, font_get_name(fontId), 31);
      clearToEndOfLine(row, col+4);
      print("\033[%i;%iH\033[?25h", row, col);
      if( getstring(name, 31, false, false, false)==13 ) font_set_name(fontId, name);
      clearToEndOfLine(row, col+4);
      print("\033[?25l\033[%i;%iH%s\033[%i;%iH", row, col, name, row, col);
    }
  
  return res;
}


static int INFLASHFUN user_font_upload_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;

  if( callType==IFT_QUERY )
    res = IFT_EDIT;
  if( callType==IFT_EDIT )
    {
      static const char __in_flash(".configmenus") message[6][80] =
        {"Upload user font  via XModem protocol:",
         "- Windows BMP file format, monochrome, no compression",
         "- characters must be 8 pixels wide and 8-16 pixels high",
         "- bitmap width must be a multiple of 32 pixels",
         "- bitmap height must be a multiple of the character height",
         "- bitmap width * bitmap height must equal 2048 * character height"};
      printLines(14, 3, 5, message);
      print("\033[14;20H%i\033[21;3HWaiting for transmission...", get_userfont_num()+1);

      const char *error = font_receive_fontdata(get_userfont_num());
      
      if( error==NULL )
        print("\033[23;3HSuccess!");
      else
        print("\033[23;3HError: %s", error);
      
      print("\033[25;3HPress any key...");
      waitkey(false);
      res = 1;
    }
  
  return res;
}


static int INFLASHFUN user_font_view_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;

  if( callType==IFT_QUERY )
    res = IFT_EDIT;
  else if( callType==IFT_EDIT )
    {
      print("\033[2J");
      if( font_apply_font(get_userfont_num()+FONT_ID_USER1, false) )
        {
          for(int i=0; i<256; i++)
            {
              uint8_t c = 8+(i % 64), r = 3+(i / 64);
              framebuf_set_char(c, r, i);
              framebuf_set_color(c, r, 7, 0);
              framebuf_set_attr(c, r, 0);
              framebuf_set_char(c, 5+r, i);
              framebuf_set_color(c, 5+r, 7, 0);
              framebuf_set_attr(c, 5+r, ATTR_INVERSE);
              framebuf_set_char(c, 10+r, i);
              framebuf_set_color(c, 10+r, 7, 0);
              framebuf_set_attr(c, 10+r, ATTR_UNDERLINE);
            }
      
          print("\033[20;9HPress any key...");
          waitkey(false);

          font_apply_font(FONT_ID_VGA, false);
        }

      res = 1;
    }
  
  return res;
}


static int INFLASHFUN user_font_graphics_mapping_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  static const char __in_flash(".configmenus") names[31][20] =
    {"Diamond", "Checkerboard", "Horizontal tab", "Form feed", "Carriage return", "Linefeed", "Degree symbol", 
     "Plus/minus", "New line", "Vertical tab", "Lower-right corner", "Upper-right corner", "Upper-left corner", 
     "Lower-left corner", "Crossing lines", "Horizontal line 1", "Horizontal line 3", "Horizontal line 5", 
     "Horizontal line 7", "Horizontal line 9", "Left \"T\"", "Right \"T\"", "Bottom \"T\"", "Top \"T\"", 
     "Vertical bar", "Less or equal", "Greater or equal", "Pi", "Not equal", "UK pound sign", "Centered dot" };

  int res = 0;

  if( callType==IFT_QUERY )
    res = IFT_EDIT;
  else if( callType==IFT_EDIT )
    {
      int topRow = 9, leftColPos = 4, leftColWidth = 28, rightColPos = 44, rightColWidth = 27;
      uint8_t mapping[31];
      memcpy(mapping, font_get_graphics_char_mapping(FONT_ID_USER1+get_userfont_num()), 31);

      print("\033[2J\033[2;16HEdit graphics character mapping for user font %i", get_userfont_num()+1);
      printMenuFrame();

      print("\033[5;5HFor each VT102 graphics symbol, specify the character number (0-255)");
      print("\033[6;5Hthat represents the graphics symbol in the font.");

      for(int i=0; i<31; i++)
        print("\033[%i;%iH%3i) %s (\016%c\017) \033[%i;%iH:  %3i", 
              i<15 ? i+topRow : i-15+topRow, i<15 ? leftColPos : rightColPos, 
              i+96, names[i], i+96, 
              i<15 ? i+topRow : i-15+topRow, i<15 ? leftColPos+leftColWidth : rightColPos+rightColWidth,
              mapping[i]);

      int i = 0;
      while( 1 )
        {
          print("\033[%i;%iH\033[7m %3i \033[27m", i<15 ? i+topRow : i-15+topRow, i<15 ? leftColPos+leftColWidth+2 : rightColPos+rightColWidth+2, mapping[i]);
          uint8_t c = waitkey(false);
          print("\033[%i;%iH %3i ", i<15 ? i+topRow : i-15+topRow, i<15 ? leftColPos+leftColWidth+2 : rightColPos+rightColWidth+2, mapping[i]);
          
          if( c==KEY_UP && i>0 )
            i = i-1;
          else if( c==KEY_DOWN && i<30 )
            i = i+1;
          else if( c==KEY_LEFT )
            i = i>14 ? i-15 : i;
          else if( c==KEY_RIGHT )
            i = i<15 ? i+15 : i;
          else if( c==KEY_HOME )
            i = 0;
          else if( c==KEY_END )
            i = 30;
          else if( c==KEY_PUP )
            i = i>15 ? 15 : 0;
          else if( c==KEY_PDOWN )
            i = i<14 ? 14 : 30;
          else if( c==13 || (c>='0' && c<='9') )
            {
              char buf[5];
              buf[0]=c; buf[1]=0;
              if( c==13 ) buf[0]=0;
              print("\033[%i;%iH     \033[4D\033[?25h", i<15 ? i+topRow : i-15+topRow, i<15 ? leftColPos+leftColWidth+2 : rightColPos+rightColWidth+2);
              c = getstring(buf, 3, false, true, true);
              print("\033[?25l");
              if( c!=27 )
                {
                  int n = atoi(buf);
                  mapping[i] = i<256 ? n : 255;
                  if( c!=13 ) continue;
                }
            }
          else if( c==27 || c=='x' || c=='X' )
            break;

          c = -1;
        }

      font_set_graphics_char_mapping(FONT_ID_USER1+get_userfont_num(), mapping);
      res = 1;
    }
  
  return res;
}


static int INFLASHFUN keyboard_macro_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;

  if( callType==IFT_QUERY )
    res = IFT_EDIT;
  else if( callType==IFT_EDIT )
    {
      print("\033[2J\033[2;28HEdit keyboard macros");
      printMenuFrame();

      KeyboardMacroInfo info;
      bool printPage = true;
      int i=0, j, p=0, numPages = 0, pageSize = 20, totalItems = -1;
      while( 1 )
        {
          // re-count items
          if( totalItems < 0 )
            {
              totalItems=0;
              for( keyboard_macro_getfirst(&info); info.key!=0; keyboard_macro_getnext(&info) ) 
                totalItems++;

              numPages = totalItems/pageSize + ((totalItems % pageSize)==0 ? 0 : 1);
              if( p>=numPages && p>0 )
                { p = numPages-1; i=pageSize-1; }
              else if( i+p*pageSize>=totalItems ) 
                i=totalItems-p*pageSize-1;
              else if( i<0 && totalItems>0 )
                i=0;

              printPage = true;
            }
          
          // re-print page
          if( printPage )
            {
              clearToEndOfLine(5, 2);
              clearToEndOfLine(6, 2);
              print("\033[5;4HPress INSERT for information on how to record a macro.");
              print("\033[6;4HPress DELETE to remove selected macro, ENTER to view selected macro.");
              
              // find first macro on page
              for(keyboard_macro_getfirst(&info), j=p*pageSize; j>0; keyboard_macro_getnext(&info), j--);

              // print macros on page
              for(int i=0; i<pageSize; i++)
                {
                  clearToEndOfLine(8+i, 2);
                  if( p*pageSize+i<totalItems )
                    {
                      char line[80];
                      strcpy(line, keyboard_get_keyname(info.key));
                      if( strlen(line)<5 ) strcat(line, "     "+strlen(line));
                      strcat(line, "  : ");
                      for(int j=0; j<info.data_len; j++)
                        {
                          if( strlen(line)+strlen(keyboard_get_keyname(info.data[j])) < (j==info.data_len-1 ? 76 : 73) )
                            {
                              strcat(line, keyboard_get_keyname(info.data[j]));
                              strcat(line, " ");
                            }
                          else
                            { 
                              if( j<info.data_len ) strcat(line, "..."); 
                              break; 
                            }
                        }
                      
                      print("\033[%i;4H%s", 8+i, line);
                      keyboard_macro_getnext(&info);
                    }
                }

              print("\033[%i;4H%s", 7, p>0 ? "..." : "   ");
              print("\033[%i;4H%s", 8+pageSize, p<numPages-1 ? "..." : "   ");
              printPage = false;
            }

          // find index i-th macro
          for(keyboard_macro_getfirst(&info), j=i+p*pageSize; j>0; keyboard_macro_getnext(&info), j--);
          
          if( i>=0 ) print("\033[%i;3H\033[7m %s \033[27m", 8+i, keyboard_get_keyname(info.key));
          uint8_t c = waitkey(false);
          if( i>=0 ) print("\033[%i;3H %s ", 8+i, keyboard_get_keyname(info.key));
          
          if( c==KEY_UP )
            {
              if( i>0 )
                i--;
              else if( p>0 )
                { p--; i = pageSize-1; printPage = true; }
            }
          else if( c==KEY_DOWN )
            {
              if( i<pageSize-1 && p*pageSize+i<totalItems-1 )
                i++;
              else if( p<numPages-1 )
                { p++; i = 0; printPage = true; }
            }
          else if( c==KEY_HOME )
            { i = 0; if( p>0 ) { p = 0; printPage=true; } }
          else if( c==KEY_END )
            { 
              if( p<numPages-1 ) { p = numPages-1; printPage = true; }
              i = totalItems-(p*pageSize)-1;
            }
          else if( c==KEY_PUP )
            { 
              if( p>0 )
                { p--; printPage = true; }
              else
                i=0;
            }
          else if( c==KEY_PDOWN )
            { 
              if( p<numPages-1 )
                { p++; printPage = true; }
              else
                i = totalItems-(p*pageSize)-1;
            }
          else if( c==KEY_DELETE )
            {
              if( keyboard_get_current_modifiers() & (KEYBOARD_MODIFIER_LEFTCTRL|KEYBOARD_MODIFIER_RIGHTCTRL) )
                { keyboard_macro_clearall(); totalItems = -1; }
              else if( keyboard_macro_delete(info.key) )
                totalItems = -1;
            }
          else if( c==KEY_INSERT )
            {
              static const char __in_flash(".configmenus") info[8][80] =
                {"New macros can be recorded at any time. To record a new macro:",
                 "",
                 "1) Press F11 (single beep will sound)",
                 "2) Press key combination to which the macro should be assigned",
                 "3) Press whatever sequence of keys the macro should consist of",
                 "4) Press F11 (double beep means the macro was successfully recorded)",
                 "",
                 "Press any key to continue..."};
              
              print("\033[2J\033[2;28HRecording a macro");
              printMenuFrame();
              printLines(10, 6, 8, info);
              waitkey(false);
              print("\033[2J\033[2;28HEdit keyboard macros");
              printMenuFrame();
              printPage = true;
            }
          else if( c==KEY_ENTER )
            {
              print("\033[2J\033[2;28HMacro details");
              printMenuFrame();
              print("\033[5;3HMacro key combination : %s\033[6;3H", keyboard_get_keyname(info.key));

              char n = 0, line[80];
              snprintf(line, 79, "Macro sequence : ");

              for(int j=0; j<info.data_len; j++)
                {
                  if( strlen(line)+strlen(keyboard_get_keyname(info.data[j])) < 76 )
                    {
                      strcat(line, keyboard_get_keyname(info.data[j]));
                      strcat(line, " ");
                    }
                  else
                    { 
                      print("\033[%i;3H%s", n+7, line);
                      strcpy(line, keyboard_get_keyname(info.data[j]));
                      strcat(line, " ");
                      n++;

                      if( n>=20 )
                        {
                          print("\033[%i;3HPress any key to continue...", n+8);
                          waitkey(false);
                          for(int i=0; i<20; i++) clearToEndOfLine(i+7, 2);
                          clearToEndOfLine(n+8, 2);
                          n = 0;
                        }
                    }
                }
              if( line[0]!=0 ) print("\033[%i;3H%s", n+7, line); 
              if( line[0]!=0 || n>0 ) { print("\033[%i;3HPress any key to continue...", n+9); waitkey(false); }

              print("\033[2J\033[2;28HEdit keyboard macros");
              printMenuFrame();
              printPage = true;
            }
          else if( c==KEY_F11 && !keyboard_macro_recording() )
            {
              // a macro recording just finished => show new macro
              totalItems = -1;
            }
          else if( c==27 || c=='x' || c=='X' )
            break;
        }
      
      res = 1;
    }
  
  return res;
}


static int INFLASHFUN keyboard_key_mapping_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;

  if( callType==IFT_QUERY )
    res = IFT_EDIT;
  else if( callType==IFT_EDIT )
    {
      print("\033[2J\033[2;26HEdit keyboard key mapping");
      printMenuFrame();

      bool printPage = true;
      int i=0, j, k, p=0, numPages = 0, pageSize = 20, totalItems = -1;
      while( 1 )
        {
          // re-count items
          if( totalItems < 0 )
            {
              totalItems = 0;
              for(int i=0; i<256; i++)
                if( settings.Keyboard.user_mapping[i]!=i )
                  totalItems++;

              numPages = totalItems/pageSize + ((totalItems % pageSize)==0 ? 0 : 1);

              if( p>=numPages && p>0 )
                { p = numPages-1; i=pageSize-1; }
              else if( i+p*pageSize>=totalItems ) 
                i=totalItems-p*pageSize-1;
              else if( i<0 && totalItems>0 )
                i=0;

              printPage = true;
            }
          
          // re-print page
          if( printPage )
            {
              clearToEndOfLine(5, 2);
              print("\033[5;5HPress INSERT to add new mapping, DELETE to remove selected mapping");

              // find first mapping on page
              for(k=0, j=p*pageSize+1; j>0; k++)
                if( settings.Keyboard.user_mapping[k]!=k && --j==0 )
                  break;

              for(int i=0; i<pageSize; i++)
                {
                  clearToEndOfLine(8+i, 2);
                  if( p*pageSize+i<totalItems )
                    {
                      print("\033[%i;%iH%s", 8+i, 10, keyboard_get_keyname(k));
                      print("\033[%i;%iH =>  %s", 8+i, 25, keyboard_get_keyname(settings.Keyboard.user_mapping[k]));
                      do { k++; } while( settings.Keyboard.user_mapping[k]==k && k<256 );
                    }
                }

              print("\033[%i;10H%s", 7, p>0 ? "..." : "   ");
              print("\033[%i;10H%s", 8+pageSize, p<numPages-1 ? "..." : "   ");
              printPage = false;
            }

          // find index k of i-th non-identical mapping
          for(k=0, j=i+p*pageSize+1; j>0; k++)
            if( settings.Keyboard.user_mapping[k]!=k && --j==0 )
              break;
          
          if( i>=0 ) print("\033[%i;%iH\033[7m %s \033[27m", 8+i, 9, keyboard_get_keyname(k));
          uint8_t c = waitkey(false);
          if( i>=0 ) print("\033[%i;%iH %s ", 8+i, 9, keyboard_get_keyname(k));
          
          if( c==KEY_UP )
            {
              if( i>0 )
                i--;
              else if( p>0 )
                { p--; i = pageSize-1; printPage = true; }
            }
          else if( c==KEY_DOWN )
            {
              if( i<pageSize-1 && p*pageSize+i<totalItems-1 )
                i++;
              else if( p<numPages-1 )
                { p++; i = 0; printPage = true; }
            }
          else if( c==KEY_HOME )
            { i = 0; if( p>0 ) { p = 0; printPage=true; } }
          else if( c==KEY_END )
            { 
              if( p<numPages-1 ) { p = numPages-1; printPage = true; }
              i = totalItems-(p*pageSize)-1;
            }
          else if( c==KEY_PUP )
            { 
              if( p>0 )
                { p--; printPage = true; }
              else
                i=0;
            }
          else if( c==KEY_PDOWN )
            { 
              if( p<numPages-1 )
                { p++; printPage = true; }
              else
                i = totalItems-(p*pageSize)-1;
            }
          else if( c==KEY_INSERT )
            {
              clearToEndOfLine(5, 2);
              print("\033[5;5HPress key to map...");
              keyboard_keymap_map_start();

              // wait for "from key" to be pressed
              uint8_t fromKey = HID_KEY_NONE;
              while( fromKey==HID_KEY_NONE && keyboard_keymap_mapping(&fromKey) ) run_tasks(false);

              clearToEndOfLine(5, 2);
              print("\033[5;5HPress key to which %s should be mapped...", keyboard_get_keyname(fromKey));

              // wait for "to key" to be pressed
              while( keyboard_keymap_mapping(NULL) ) run_tasks(false);

              totalItems = -1;
            }
          else if( c==KEY_DELETE )
            {
              settings.Keyboard.user_mapping[k] = k;
              totalItems = -1;
            }
          else if( c==27 || c=='x' || c=='X' )
            break;
        }
      
      res = 1;
    }
  
  return res;
}


static int INFLASHFUN recvChar(int msDelay) 
{ 
  absolute_time_t endtime = make_timeout_time_ms(msDelay);
  while( !time_reached(endtime) )
    { 
      if( tuh_inited() ) tuh_task();
      keyboard_task();
      if( keyboard_read_keypress()==HID_KEY_ESCAPE ) return -2;
      if( uart_is_readable(PIN_UART_ID) ) return uart_getc(PIN_UART_ID);
    }

  return -1; 
}


static void INFLASHFUN sendData(const char *data, int size)
{
  uart_write_blocking(PIN_UART_ID, (uint8_t *) data, size);
}


static int xmodem_confignum = 0;
static uint8_t *xmodem_configdata = NULL;

static bool INFLASHFUN sendConfigDataPacket(unsigned long no, char* charData, int size)
{
  if( no<=4096/128 )
    {
      memcpy(charData, flash_get_read_ptr(xmodem_confignum)+(no-1)*128, 128);
      return true;
    }
  else
    return false;
}


static bool INFLASHFUN receiveConfigDataPacket(unsigned long no, char* charData, int size)
{
  if( no<=4096/128 ) memcpy(xmodem_configdata+(no-1)*128, charData, 128);
  return true;
}


static const char *INFLASHFUN get_config_name(uint8_t i, struct SettingsHeaderStruct *header)
{
  if( i>9 )
    header->name[0] = 0;
  else
    {
      flash_read(i, header, sizeof(struct SettingsHeaderStruct));
      if( header->magic != CONFIG_MAGIC )
        strcpy(header->name, "[empty slot]");
      else if( header->name[0]==0 )
        sprintf(header->name, "Configuration %i", i+1);
    }
  
  return header->name;
}


static int INFLASHFUN configs_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;
  struct SettingsHeaderStruct header;

  if( callType==IFT_QUERY )
    res = IFT_EDIT;
  else if( callType==IFT_EDIT )
    {
      print("\033[2J\033[2;29HManage Configurations");
      printMenuFrame();

      static const char __in_flash(".configmenus") info[5][80] =
        {"ENTER  : load settings from slot          * : set slot as startup",
         "S      : save current settings to slot    E : send slot data via XModem",
         "N      : change slot name                 I : receive slot data via XModem",
         "DELETE : clear slot",
         "> current configuration   * startup configuration    ! unsaved changes"};

      printLines(5, 5, 4, info);
      printLines(22, 5, 1, info+4);
      
      uint8_t firstItemRow = 11;
      uint32_t currentStartupConfig = 0;
      bool printPage = true;
      int i=0;
      while( 1 )
        {
          if( printPage )
            {
              get_config_name(0, &header);
              currentStartupConfig = header.startupConfig;
              for(int i=0; i<10; i++)
                {
                  clearToEndOfLine(firstItemRow+i,2);
                  print("\033[%i;%iH%c%c%c)%c %s", firstItemRow+i, 8, 
                        (i==currentConfig) && (memcmp(flash_get_read_ptr(i), &config, sizeof(config.data))!=0) ? '!' : ' ',
                        (i==currentConfig) ? '>' : ' ', 
                        i<9 ? '1'+i : 'A'-9+i, 
                        i==currentStartupConfig ? '*' : ' ', 
                        get_config_name(i, &header));
                }

              for(int i=23; i<30; i++ ) clearToEndOfLine(i, 2);
              printPage = false;
            }

          if( i>=0 ) print("\033[%i;%iH\033[7m %s \033[27m", firstItemRow+i, 13, get_config_name(i, &header));
          uint8_t c = waitkey(false);
          if( i>=0 ) print("\033[%i;%iH %s ", firstItemRow+i, 13, header.name);
          
          if( c==KEY_UP && i>0 )
            i--;
          else if( c==KEY_DOWN && i<9 )
            i++;
          else if( c==KEY_HOME || c==KEY_PUP )
            i = 0;
          else if( c==KEY_END || c==KEY_PDOWN )
            i = 9;
          else if( c>='0' && c<='9' )
            i = (c=='0') ? 9 : c-'1';
          else if( c=='a' || c=='A' )
            i = 9;
          else if( (c==KEY_ENTER || c=='l' || c=='L') && (i!=currentConfig) && (header.magic==CONFIG_MAGIC) )
            {
              char c = 0;
              if( memcmp(flash_get_read_ptr(currentConfig), &config, sizeof(config.data))!=0 )
                {
                  print("\033[?25l\033[%i;5HSave changes to current configuration before loading (y/n)? ", firstItemRow+14);
                  c = waitkey(true);
                  if( c=='y' ) saveConfig(currentConfig);
                  clearToEndOfLine(firstItemRow+14, 2);
                }
              
              if( c!=27 )
                {
                  if( loadConfig(i) )
                    printPage = true;
                  else
                    {
                      print("\033[?25l\033[%i;5HLoading configuration '%s' failed.", firstItemRow+14, get_config_name(i, &header));
                      waitkey(false);
                    }
                }
            }
          else if( c=='s' || c=='S' )
            {
              char c = 'y';
              if( header.magic==CONFIG_MAGIC && i!=currentConfig )
                {
                  print("\033[?25l\033[%i;5HOverwrite configuration '%s' with current settings (y/n)? ", firstItemRow+14, get_config_name(i, &header));
                  c = waitkey(true);
                  clearToEndOfLine(firstItemRow+14, 2);
                }
              
              if( c=='y' )
                {
                  if( header.magic==CONFIG_MAGIC ) 
                    flash_read(i, &settings.Header, sizeof(struct SettingsHeaderStruct));
                  else
                    settings.Header.name[0] = 0;
                    
                  if( saveConfig(i) )
                    printPage = true;
                  else
                    {
                      print("\033[?25l\033[%i;5HSaving configuration '%s' failed.", firstItemRow+14, get_config_name(i, &header));
                      waitkey(false);
                    }
                }
            }
          else if( (c=='n' || c=='N') && (header.magic==CONFIG_MAGIC) )
            {
              flash_read(i, &header, sizeof(struct SettingsHeaderStruct));
              clearToEndOfLine(firstItemRow+i, 13);
              print("\033[%i;%iH\033[?25h", firstItemRow+i, 14);
              if( getstring(header.name, 63, false, false, false)!=27 )
                {
                  flash_write_partial(i, &header, 0, sizeof(struct SettingsHeaderStruct));
                  if( i==currentConfig ) memcpy(&settings, &header, sizeof(struct SettingsHeaderStruct));
                }
              printPage = true;
            }
          else if( (c=='e' || c=='E') && (header.magic==CONFIG_MAGIC) )
            {
              xmodem_confignum = i;
              print("\033[?25l\033[%i;5HSending configuration data via XModem protocol...", firstItemRow+14);
              
              if( xmodem_transmit(recvChar, sendData, sendConfigDataPacket) )
                print("\033[?25l\033[%i;5HSuccessfully sent configuration data. Press any key...", firstItemRow+14);
              else
                print("\033[?25l\033[%i;5HTransmission of configuration data failed. Press any key...", firstItemRow+14);
              waitkey(false);

              printPage = true;
            }
          else if( c=='i' || c=='I' )
            {
              char c = 'y';
              if( header.magic==CONFIG_MAGIC )
                {
                  print("\033[?25l\033[%i;5HOverwrite configuration '%s' with received data (y/n)? ", firstItemRow+14, get_config_name(i, &header));
                  c = waitkey(true);
                  clearToEndOfLine(firstItemRow+14, 2);
                }
              
              if( c=='y' )
                {
                  xmodem_configdata = (uint8_t *) malloc(4096);
                  if( xmodem_configdata!=NULL )
                    {
                      print("\033[?25l\033[%i;5HReceiving configuration data via XModem protocol...", firstItemRow+14);
                      print("\r\n");

                      int res = xmodem_receive(recvChar, sendData, receiveConfigDataPacket);
                      if( res && *((uint32_t *) xmodem_configdata)==CONFIG_MAGIC )
                        {
                          flash_write(i, xmodem_configdata, 4096);
                          print("\033[?25l\033[%i;5HSuccessfully received configuration data. Press any key...", firstItemRow+14);
                        }
                      else
                        print("\033[?25l\033[%i;5HReceiving configuration data failed. Press any key...", firstItemRow+14);

                      waitkey(false);
                      free(xmodem_configdata);
                    }
                }

              printPage = true;
            }
          else if( (c=='*') && i!=currentStartupConfig && (header.magic==CONFIG_MAGIC) )
            {
              flash_read(0, &header, sizeof(struct SettingsHeaderStruct));
              header.startupConfig = i;
              flash_write_partial(0, &header, 0, sizeof(struct SettingsHeaderStruct));
              if( currentConfig==0 ) settings.Header.startupConfig = i;
              printPage = true;
            }
          else if( c==KEY_DELETE && (header.magic==CONFIG_MAGIC) )
            {
              print("\033[?25l\033[%i;5HDelete configuration '%s' (y/n)? ", firstItemRow+14, get_config_name(i, &header));
              char c = waitkey(true);
              clearToEndOfLine(firstItemRow+14, 2);
              if( c=='y' )
                {
                  memset(&header, 0, sizeof(struct SettingsHeaderStruct));
                  flash_write_partial(i, &header, 0, sizeof(struct SettingsHeaderStruct));
                  if( i==currentConfig ) memcpy(&settings, &header, sizeof(struct SettingsHeaderStruct));
                  printPage = true;
                }
            }
          else if( c==KEY_LEFT || c==27 || c=='x' || c=='X' )
            break;
        }

      res = 1;
    }
  
  return res;
}


static int INFLASHFUN keyboard_reprate_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;

  if( callType==IFT_QUERY )
    res = IFT_PRINT;
  else if( callType==IFT_PRINT )
    {
      uint16_t rate = get_keyboard_repeat_rate_mHz(settings.Keyboard.reprate);
      print("%i.%iHz", rate/1000, (rate%1000)/100);
    }
  
  return res;
}

    
static int INFLASHFUN color_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;
  uint8_t *color = NULL;

  // vga/dvi
  bool dvi = (callType!=IFT_DEFAULT || defaults_force_dvi<0) ? framebuf_is_dvi() : (defaults_force_dvi>0);

  // color number
  int id = find_menu_item_id(MI_FLASHCOLOR, MI_PCOLOR15);
  if( id==MI_FLASHCOLOR )
    color = dvi ? &settings.Bell.visual_color_dvi : &settings.Bell.visual_color_vga;
  else if( id==MI_COLOR_MONO_BG )
    color = dvi ? &(settings.Screen.AnsiColorDVI.monobg) : &(settings.Screen.AnsiColorVGA.monobg);
  else if( id==MI_COLOR_MONO_NORM )
    color = dvi ? &(settings.Screen.AnsiColorDVI.mononormal) : &(settings.Screen.AnsiColorVGA.mononormal);
  else if( id==MI_COLOR_MONO_BOLD )
    color = dvi ? &(settings.Screen.AnsiColorDVI.monobold) : &(settings.Screen.AnsiColorVGA.monobold);
  else if( id>=MI_COLOR0 && id<=MI_COLOR15 )
    color = dvi ? &(settings.Screen.AnsiColorDVI.colors[id-MI_COLOR0]) : &(settings.Screen.AnsiColorVGA.colors[id-MI_COLOR0]);
  else if( id==MI_PCOLOR_MONO_BG )
    color = dvi ? &(settings.Screen.PetsciiColorDVI.monobg) : &(settings.Screen.PetsciiColorVGA.monobg);
  else if( id==MI_PCOLOR_MONO_NORM )
    color = dvi ? &(settings.Screen.PetsciiColorDVI.mononormal) : &(settings.Screen.PetsciiColorVGA.mononormal);
  else if( id==MI_PCOLOR_MONO_BOLD )
    color = dvi ? &(settings.Screen.PetsciiColorDVI.monobold) : &(settings.Screen.PetsciiColorVGA.monobold);
  else if( id>=MI_PCOLOR0 && id<=MI_PCOLOR15 )
    color = dvi ? &(settings.Screen.PetsciiColorDVI.colors[id-MI_PCOLOR0]) : &(settings.Screen.PetsciiColorVGA.colors[id-MI_PCOLOR0]);

  if( color!=NULL )
    {
      if( callType==IFT_QUERY )
        res = IFT_PRINT | IFT_EDIT | IFT_DEFAULT;
      else if( callType==IFT_PRINT )
        {
          print("%02X  (Hello World)", *color);
          for(int c=0; c<13; c++) framebuf_set_fullcolor(col+3+c, row-1, *color, 0);
        }
      else if( callType==IFT_EDIT )
        {
          while( 1 )
            {
              print("\033[%i;%iH\033[7m %02X \033[27m (Hello World)", row, col-1, *color);
              for(int c=0; c<13; c++) framebuf_set_fullcolor(col+3+c, row-1, *color, 0);

              uint8_t c = waitkey(false);
              if( c==KEY_UP && *color < (dvi ? 63 : 255) )
                *color = color==&settings.Bell.visual_color_dvi ? *color+0b010101 : *color+1;
              else if( c==KEY_DOWN && *color > 0 )
                *color = color==&settings.Bell.visual_color_dvi ? *color-0b010101 : *color-1;
              else if( c==KEY_HOME )
                *color = dvi ? 63 : 255;
              else if( c==KEY_PUP && color!=&settings.Bell.visual_color_dvi )
                *color = MIN(*color+16, dvi ? 63 : 255);
              else if( c==KEY_END && color!=&settings.Bell.visual_color_dvi )
                *color = 0;
              else if( c==KEY_PDOWN )
                *color = MAX((int) *color-16, 0);
              else if( c==KEY_LEFT || c==13 || c==27 || c=='x' || c=='X' )
                break;
            }
      
          print("\033[%i;%iH %02X  (Hello World)", row, col-1, *color);
          for(int c=0; c<13; c++) framebuf_set_fullcolor(col+3+c, row-1, *color, 0);
        }
      else if( callType==IFT_DEFAULT )
        {
          if( id==MI_FLASHCOLOR )
            *color = dvi ? 0x15 : 0x40;
          else
            {
              switch( id )
                {
                case MI_COLOR_MONO_BG:    id = MI_COLOR0;   break;
                case MI_COLOR_MONO_NORM:  id = MI_COLOR7;   break;
                case MI_COLOR_MONO_BOLD:  id = MI_COLOR15;  break;
                case MI_PCOLOR_MONO_BG:   id = MI_PCOLOR0;  break;
                case MI_PCOLOR_MONO_NORM: id = MI_PCOLOR15; break;
                case MI_PCOLOR_MONO_BOLD: id = MI_PCOLOR1;  break;
                }

              if( id>=MI_PCOLOR0 )
                *color = dvi ? default_colors_petscii_dvi[id-MI_PCOLOR0] : default_colors_petscii_vga[id-MI_PCOLOR0];
              else if( id>=MI_COLOR0 )
                *color = dvi ? default_colors_ansi_dvi[id-MI_COLOR0] : default_colors_ansi_vga[id-MI_COLOR0];
            }
        }
    }

  return res;
}


static int INFLASHFUN ttype_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;

  if( callType==IFT_QUERY )
    res = IFT_EDIT;
  else if( callType==IFT_EDIT )
    {
      int v = *(item->value);
      changeItemValue(item, row, col);
      if( settings.Terminal.ttype!=CFG_TTYPE_PETSCII ) 
        { settings.Terminal.bgcolor &= 7; settings.Terminal.fgcolor &= 7; }
      res = *(item->value) != v;
    }

  return res;
}


static int INFLASHFUN color16_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;

  static const char __in_flash(".configmenus") ansicolornames[16][20] =
    {"black", "red", "green", "yellow", "blue", "magenta", "cyan", "light gray", 
     "dark gray", "bright red", "bright green", "bright yellow", "bright blue", 
     "bright magenta", "bright cyan", "white"};

  static const char __in_flash(".configmenus") petsciicolornames[16][20] =
    {"black", "white", "red", "cyan", "purple", "green", "blue", "yellow", 
     "orange", "brown", "light red", "dark grey", "grey", 
     "light green", "light blue", "light grey"};

  if( callType==IFT_QUERY )
    res = IFT_PRINT | IFT_EDIT;
  else if( callType==IFT_PRINT )
    {
      if( settings.Terminal.ttype==CFG_TTYPE_PETSCII )
        {
          print("%i \033[27m (%s)", *(item->value), petsciicolornames[*(item->value)]);
          uint8_t color = framebuf_is_dvi() ? settings.Screen.PetsciiColorDVI.colors[*(item->value)] : settings.Screen.PetsciiColorVGA.colors[*(item->value)];
          for(int c=0; c<strlen(petsciicolornames[*(item->value)])+4; c++) framebuf_set_fullcolor(col+2+c, row-1, color, 0);
        }
      else
        print("%i \033[27;38;5;%im (%s)\033[39m", *(item->value), *(item->value), ansicolornames[*(item->value)]);
    }
  else if( callType==IFT_EDIT )
    {
      struct MenuItemStruct item2;
      memcpy(&item2, item, sizeof(item2));
      item2.max = settings.Terminal.ttype==CFG_TTYPE_PETSCII ? 15 : 7;
      changeItemValue(&item2, row, col);
    }

  return res;
}


static int INFLASHFUN attr_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;

  if( callType==IFT_QUERY )
    res = IFT_PRINT;
  else if( callType==IFT_PRINT )
    {
      bool first = true;
      print("%i \033[27m \033[0", *(item->value));
      if( *(item->value) & ATTR_BOLD )      print(";1");
      if( *(item->value) & ATTR_UNDERLINE ) print(";4");
      if( *(item->value) & ATTR_BLINK )     print(";5");
      if( *(item->value) & ATTR_INVERSE )   print(";7");
      print("m");
      if( *(item->value) & ATTR_BOLD )      { print("%cbold",      first ? '(' : '+'); first=false; }
      if( *(item->value) & ATTR_UNDERLINE ) { print("%cunderline", first ? '(' : '+'); first=false; }
      if( *(item->value) & ATTR_BLINK )     { print("%cblink",     first ? '(' : '+'); first=false; }
      if( *(item->value) & ATTR_INVERSE )   { print("%cinverse",   first ? '(' : '+'); first=false; }
      if( first ) print("(none");
      print(")\033[0m");
    }
  
  return res;
}


static float INFLASHFUN get_actual_baud(uart_inst_t *uart)
{
  // Get PL011's baud divisor registers
  uint32_t baud_ibrd = uart_get_hw(uart)->ibrd;
  uint32_t baud_fbrd = uart_get_hw(uart)->fbrd;
  
  // See datasheet
  return (4.0 * ((float) clock_get_hz(clk_peri))) / (float) (64 * baud_ibrd + baud_fbrd);
}


static void INFLASHFUN print_baud(uint32_t baud, bool selectmode)
{
  if( selectmode && baud==0 )
    print("\033[7m Custom \033[27m");
  else
    {
      uart_set_baudrate(PIN_UART_ID, baud);
      print("%s%lu%s", selectmode ? "\033[7m " : "", baud, selectmode ? " \033[27m" : "");

      float fbaud   = baud;
      float actual  = get_actual_baud(PIN_UART_ID);
      float pctdiff = (100.0*fabs(actual-fbaud)) / fbaud;
      if( pctdiff >= 0.05 )
        print(" actual %.0f (%c%.1f%%)", actual, actual<fbaud ? '-' : '+', pctdiff);
    }
}


static int INFLASHFUN baud_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;
  const uint32_t preset[] = {50, 75, 110, 150, 300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 76800, 115200, 230400, 460800, 576000, 921600, 0};

  if( callType==IFT_QUERY )
    res = IFT_PRINT | IFT_EDIT | IFT_DEFAULT;
  else if( callType==IFT_PRINT )
    print_baud(settings.Serial.baud, false);
  else if( callType==IFT_EDIT )
    {
      int i;
      for(i=0; preset[i]>0 && preset[i]!=settings.Serial.baud; i++);
      
      while( 1 )
        {
          clearToEndOfLine(row, col-1);
          print("\033[%i;%iH", row, col-1);
          print_baud(preset[i], true);
          
          uint8_t c = waitkey(false);
          if( c==KEY_UP && preset[i]!=0 )
            i = i+1;
          else if( c==KEY_DOWN && i>0 )
            i = i-1;
          else if( c==KEY_HOME || c==KEY_PUP )
            i = (sizeof(preset)/sizeof(uint32_t))-1;
          else if( c==KEY_END || c==KEY_PDOWN )
            i = 0;
          else if( c==KEY_LEFT || c==13 || c==27 || c=='x' || c=='X' )
            break;
        }

      if( preset[i]==0 )
        {
          char buf[10];
          buf[0] = 0;
          clearToEndOfLine(row, col-1);
          print("\033[?25h\033[%i;%iH ", row, col-1);
          getstring(buf, 7, false, true, false);
          if( strlen(buf)>0 ) settings.Serial.baud=atoi(buf);
          print("\033[?25l");
        }
      else
        settings.Serial.baud = preset[i];
      
      clearToEndOfLine(row, col-1);
      print("\033[%i;%iH ", row, col-1);
      print_baud(settings.Serial.baud, false);
    }
  else if( callType==IFT_DEFAULT )
    {
      settings.Serial.baud = 9600;
    }

  return res;
}


static int INFLASHFUN answerback_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;

  if( callType==IFT_QUERY )
    res = IFT_PRINT | IFT_EDIT;
  else if( callType==IFT_PRINT )
    print("%s", settings.Terminal.answerback);
  else if( callType==IFT_EDIT )
    {
      clearToEndOfLine(row, col+4);
      print("\033[%i;%iH\033[?25h", row, col);
      getstring(settings.Terminal.answerback, 31, false, false, false);
      clearToEndOfLine(row, col+4);
      print("\033[?25l\033[%i;%iH%s\033[%i;%iH", row, col, settings.Terminal.answerback, row, col);
    }
  else if( callType==IFT_DEFAULT )
    strcpy(settings.Terminal.answerback, "VersaTerm 1.0");
  
  return res;
}


static int INFLASHFUN bell_test_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;

  if( callType==IFT_QUERY )
    res = IFT_EDIT;
  else if( callType==IFT_EDIT )
    {
      sound_play_tone(config_get_audible_bell_frequency(), 
                      config_get_audible_bell_duration(), 
                      config_get_audible_bell_volume(), 
                      false);
      framebuf_flash_screen(config_get_visual_bell_color(), config_get_visual_bell_duration());
    }
  
  return res;
}


static int INFLASHFUN displaytype_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;

  if( callType==IFT_QUERY )
    res = IFT_PRINT;
  else if( callType==IFT_PRINT )
    {
      print("%s", item->valueLabels[settings.Screen.display]);
      if( settings.Screen.display==0 )
        print(" (currently %s)", item->valueLabels[get_current_displaytype()]);
    }
  
  return res;
}


static int usbtype_fn(const struct MenuItemStruct *item, int callType, int row, int col)
{
  int res = 0;

  if( callType==IFT_QUERY )
    res = IFT_PRINT;
  else if( callType==IFT_PRINT )
    {
      print("%s", item->valueLabels[settings.USB.mode]);
      if( settings.USB.mode==CFG_USBMODE_AUTODETECT )
        print(" (currently %s)", item->valueLabels[get_current_usbmode()]);
    }
  
  return res;
}


// -----------------------------------------------------------------------------------------------------------------


static void INFLASHFUN printItemValue(const struct MenuItemStruct *item, int row, int col)
{
  if( HAVE_IFT(item, IFT_PRINT) )
    item->itemfun(item, IFT_PRINT, row, col);
  else if( item->valueLabels[0][0]==0 )
    print("%u", *(item->value));
  else
    print("%s", item->valueLabels[*(item->value)]);
}


const char *INFLASHFUN getItemLabel(const struct MenuItemStruct *items, uint8_t numItems, bool haveDefaults, int i)
{
  if( i<numItems )
    return items[i].label;
  else if( i==numItems+1 || (i==numItems && !haveDefaults) )
    return "Exit";
  else if( i==numItems && menuIdPathLen==0 )
    return "Reset all settings to defaults";
  else if( i==numItems )
    return "Reset this menu to defaults";
  else
    return "";
}


static int INFLASHFUN printMenu(const char *title, int col, const struct MenuItemStruct *items, uint8_t numItems, bool haveDefaults)
{
  print("\033[2J\033[2;%iH%s\n\n", 40-strlen(title)/2, title);
  printMenuFrame();

  int maxlen = 0;
  for(int i=0; i<numItems; i++)
    if( strlen(items[i].label)>maxlen )
      maxlen=strlen(items[i].label);

  int i, itemValueCol = col+maxlen+8;
  for(i=0; i<numItems; i++)
    {
      print("\033[%i;%iH%c)  %s", 5+i, col, items[i].key, getItemLabel(items, numItems, haveDefaults, i));
      menuIdPath[menuIdPathLen++] = items[i].itemId;
      if( HAVE_IFT(items+i, IFT_PRINT) || items[i].value!=NULL )
        {
          print("\033[%i;%iH :  ", 5+i, itemValueCol-4);
          printItemValue(items+i, 5+i, itemValueCol);
        }
      menuIdPathLen--;
    }
  
  if( haveDefaults ) { print("\033[%i;%iHD)  %s", 6+i, col, getItemLabel(items, numItems, haveDefaults, i)); i++; }
  print("\033[%i;%iHx)  %s", 6+i, col, getItemLabel(items, numItems, haveDefaults, i));
  
  return itemValueCol;
}


static void INFLASHFUN changeItemValue(const struct MenuItemStruct *item, int row, int col)
{
  uint8_t up=KEY_UP, down=KEY_DOWN, pup=KEY_PUP, pdown=KEY_PDOWN, home=KEY_HOME, end=KEY_END;
  if( item->valueLabels[0][0]!=0 )
    { up=KEY_DOWN, down=KEY_UP, pup=KEY_PDOWN, pdown=KEY_PUP, home=KEY_END, end=KEY_HOME; }

  while( 1 )
    {
      clearToEndOfLine(row, col);
      print("\033[%i;%iH\033[7m ", row, col-1);
      printItemValue(item, row, col);
      print(" \033[27m");
      
      uint8_t c = waitkey(false);
      if( (c==up || c==down) && item->max==item->min + item->step )
        *(item->value) = *(item->value)==item->min ? item->max : item->min;
      else if( c==down && *(item->value) > item->min )
        *(item->value) = *(item->value) - item->step;
      else if( c==up && *(item->value) < item->max )
        *(item->value) = *(item->value) + item->step;
      else if( c==down && *(item->value) > item->min )
        *(item->value) = *(item->value) - item->step;
      else if( c==pup )
        *(item->value) = MIN((int) item->max, ((int) *(item->value))+(10 * (int) item->step));
      else if( c==pdown )
        *(item->value) = MAX((int) item->min, ((int) *(item->value))-(10 * (int) item->step));
      else if( c==end )
        *(item->value) = item->min;
      else if( c==home )
        *(item->value) = item->max;
      else if( c==KEY_LEFT || c==13 || c==27 || c=='x' || c=='X' )
        break;
    }
  
  clearToEndOfLine(row, col);
  print("\033[%i;%iH ", row, col-1);
  printItemValue(item, row, col);
  print(" ");
}


void INFLASHFUN handleMenu(const char *title, const struct MenuItemStruct *items, uint8_t numItems)
{
  int menuRow = 5, menuCol = 13;
  int totalItems = numItems;
  totalItems++;

  bool haveDefaults = (items == mainMenu);
  for(uint8_t i=0; !haveDefaults && i<numItems; i++)
    {
      if( items[i].itemfun==NULL )
        haveDefaults = items[i].value!=NULL;
      else
        {
          menuIdPath[menuIdPathLen++] = items[i].itemId;
          haveDefaults = HAVE_IFT(items+i, IFT_DEFAULT);
          menuIdPathLen--;
        }
    }


  if( haveDefaults ) totalItems++;

  int itemValueCol = printMenu(title, menuCol, items, numItems, haveDefaults);

  int i = 0;
  while( 1 )
    {
      int k = i;
      print("\033[%i;%iH\033[7m %s \033[27m", i<numItems ? menuRow+i : menuRow+1+i, menuCol+3, getItemLabel(items, numItems, haveDefaults, i));

      uint8_t c = waitkey(false);
      if( c=='x' || c=='X' || c==27 || c==KEY_LEFT )
        return;
      else if( c==KEY_UP && i>0 )
        i--;
      else if( c==KEY_DOWN && i<totalItems-1 )
        i++;
      else if( c==KEY_RIGHT && i<totalItems-1 )
        c = 13;
      else if( c==KEY_HOME || c==KEY_PUP )
        i = 0;
      else if( c=='D' && haveDefaults )
        { i = totalItems-2; c = 13; }
      else if( c==KEY_END || c==KEY_PDOWN )
        i = totalItems-1;
      else
        {
          for(int j=0; j<numItems; j++)
            if( c==items[j].key )
              { i = j; c = 13; }
        }

      if( i!=k || c==13 )
        print("\033[%i;%iH %s ", k<numItems ? menuRow+k : menuRow+1+k, menuCol+3, getItemLabel(items, numItems, haveDefaults, k));
      
      if( c==13 )
        {
          if( i==totalItems-1 )
            return;
          else if( haveDefaults && i==totalItems-2 )
            {
              print("\033[%i;15HReally reset to defaults (y/n)? \033[?25h", 8+totalItems);
              uint8_t c=0;
              while( c!='y' && c!='n' && c!=27) c=tolower(waitkey(false));
              print("%c\033[?25l", c);
              if( c=='y' ) setDefaults(items, totalItems, menuIdPathLen==0);
              printMenu(title, menuCol, items, numItems, haveDefaults);
            }
          else
            {
              bool clearScreen = false;
              menuIdPath[menuIdPathLen++] = items[i].itemId;

              if( HAVE_IFT(items+i, IFT_EDIT) )
                clearScreen = ((items[i].itemfun)(&items[i], IFT_EDIT, menuRow+i, itemValueCol))!=0;
              else if( items[i].submenuItems!=NULL ) 
                {
                  handleMenu(items[i].label, items[i].submenuItems, items[i].submenuItemsNum);
                  clearScreen = true;
                }
              else if( items[i].value!=NULL ) 
                changeItemValue(items+i, menuRow+i, itemValueCol);

              menuIdPathLen--;
              if( clearScreen ) printMenu(title, menuCol, items, numItems, haveDefaults);
            }
        }
    }
}


void INFLASHFUN config_show_splash()
{
  static const char __in_flash(".configmenus") splash[9][80] =
    {"\016lqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqk\n",
     "x\017                     VersaTerm 1.0                     \016x", 
     "x\017                 (C) 2022 David Hansel                 \016x",
     "x\017          https://github.com/dhansel/VersaTerm         \016x",
     "tqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqu",
     "x\017  DVI output  via https://github.com/Wren6991/PicoDVI  \016x", 
     "x\017  VGA output  via https://github.com/Panda381/PicoVGA  \016x", 
     "x\017  USB support via https://github.com/hathach/tinyusb   \016x", 
     "mqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqj\017"};

  if( settings.Screen.splash!=0 )
    {
      menuActive = true;
      framebuf_apply_settings();
      terminal_apply_settings();

      int top  = framebuf_get_nrows()/2-5;
      int left = framebuf_get_ncols(-1)/2-29;
      print("\033[?25l\033)0\033[%i;1H", top);
      printLines(top, left, 9, splash);
      while( keyboard_num_keypress()==0 && !serial_readable() )
        run_tasks(false);
      
      menuActive = false;
      framebuf_apply_settings();
      terminal_apply_settings();
    }
}


void config_init()
{
  gpio_init(PIN_DEFAULTS);
  gpio_set_dir(PIN_DEFAULTS, false); // input
  gpio_pull_up(PIN_DEFAULTS);
  busy_wait_us_32(100); // wait a short time for voltage to stabilize before reading

  // set defaults for both VGA and DVI/HDMI mode
  memset(&config.data, 0, sizeof(config.data));
  defaults_force_dvi = 1;
  setDefaults(mainMenu, NUM_MENU_ITEMS(mainMenu), true);
  defaults_force_dvi = 0;
  setDefaults(mainMenu, NUM_MENU_ITEMS(mainMenu), true);
  defaults_force_dvi = -1;

  for(int i=0; i<256; i++)
    settings.Keyboard.user_mapping[i]=i;

  *((uint16_t *) &settings.keyboard_macros_start) = 0;
  if( gpio_get(PIN_DEFAULTS) )
    {
      // PIN_DEFAULTS==true => attempt to load config (inverted logic)
      if( loadConfig(0) )
        {
          // if different startup config is set then attempt to load it
          if( settings.Header.startupConfig>0 )
            loadConfig(settings.Header.startupConfig);
        }
      else
        {
          // loading config 0 failed => save default values
          saveConfig(0);
        }
    }
}


uint8_t  *config_get_keyboard_user_mapping()
{
  return settings.Keyboard.user_mapping;
}


uint8_t  *config_get_keyboard_macros_start()
{
  return &(settings.keyboard_macros_start);
}


bool config_menu_active()
{
  return menuActive;
}


bool INFLASHFUN config_load(uint8_t n)
{
  bool res = false;

  if( n==0xFF )
    {
      struct SettingsHeaderStruct header;
      menuActive = true;
      framebuf_apply_settings();
      terminal_apply_settings();
      print("\033[?25l\033)0\033[2J\033[2;32HLoad Configuration");
      printMenuFrame();
      for(int i=0; i<10; i++)
        print("\033[%i;%iHF%i%s : %s", 9+i, 10, i+1, i<9 ? " " : "", get_config_name(i, &header));
      print("\033[%i;%iHESC : Exit", 20, 10);

      while( true )
        {
          uint8_t c = waitkey(false);
          if( c>=KEY_F1 && c<=KEY_F10 )
            {
              int i = c-KEY_F1;
              flash_read(i, &header, sizeof(struct SettingsHeaderStruct));
              if( header.magic==CONFIG_MAGIC )
                {
                  if( memcmp(flash_get_read_ptr(currentConfig), &config, sizeof(config.data))!=0 )
                    {
                      print("\033[?25l\033[23;5HSave changes to current configuration before loading (y/n)? ");
                      c = waitkey(true);
                      if( c=='y' ) saveConfig(currentConfig);
                      clearToEndOfLine(23, 2);
                    }
                  
                  if( c!=KEY_ESC && loadConfig(i) )
                    { res = true; break; }
                }
            }
          else if( c==KEY_ESC )
            break;
        }
      
      print("\033[?25h");
      menuActive = false;
      framebuf_apply_settings();
      terminal_apply_settings();
    }
  else
    res = loadConfig(n);

  return res;
}


int INFLASHFUN config_menu()
{
  uint8_t usbmode = get_current_usbmode();
  uint8_t displaytype = get_current_displaytype();

  menuActive = true;
  framebuf_apply_settings();
  terminal_apply_settings();
  print("\033[?25l\033)0");
  menuIdPathLen = 0;

  while( 1 )
    {
      handleMenu("Settings", mainMenu, NUM_MENU_ITEMS(mainMenu));
      if( get_current_usbmode() == usbmode && get_current_displaytype()==displaytype )
        break;
      else
        {
          print("\033[20;1H");
          if( usbmode != get_current_usbmode() )
            print("\033[4CUSB mode changes (host/device) require a restart to take effect.\n");
          if( displaytype != get_current_displaytype() )
            print("\033[4CDisplay mode changes (HDMI/VGA) require a restart to take effect.\n");

          print("\n\033[4CSave current settings as startup default and restart now (y/n)? ");

          uint8_t c=0;
          while( c!='y' && c!='n' && c!=27 ) c=tolower(waitkey(true));
          if( c=='y' )
            {
              if( currentConfig==0 ) 
                settings.Header.startupConfig = 0;
              else
                {
                  struct SettingsHeaderStruct header;
                  flash_read(0, &header, sizeof(struct SettingsHeaderStruct));
                  header.startupConfig = currentConfig;
                  flash_write_partial(0, &header, 0, sizeof(struct SettingsHeaderStruct));
                }
              
              saveConfig(currentConfig);
              watchdog_reboot(0, 0, 0);
            }
          else if( c=='n' )
            break;
        }
    }

  print("\033[?25h");
  terminal_clear_screen();
  menuActive = false;
  
  return 1;
}
