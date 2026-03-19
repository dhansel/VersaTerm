// -----------------------------------------------------------------------------
// terminal_mode7.c — BBC Micro Mode 7 terminal emulator for VersaTerm
//
// Implements the BBC Micro Mode 7 (SAA5050 teletext) protocol on top of
// VersaTerm's existing framebuf layer.
//
// Mode 7 is closely related to Viewdata/Prestel but differs in:
//   - 25 rows instead of 24
//   - No parity stripping (8-bit clean data; 0x80-0x9F are direct attributes)
//   - No ESC-as-attribute-introducer; ESC has no special meaning
//   - Several BBC VDU commands absorbed (VDU 17-19, 22-25, 28-31)
//   - VDU 30 (0x1E) = unconditional home to (0,0)
//   - VDU 31 (0x1F) = cursor to column x, row y (2-byte argument follows)
//
// Display geometry, font, attribute model, and rendering logic are inherited
// verbatim from terminal_viewdata.c.
// -----------------------------------------------------------------------------

#include "terminal_mode7.h"
#include "framebuf.h"
#include "font.h"
#include "config.h"
#include "keyboard.h"
#include "serial.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define INFLASHFUN __in_flash(".terminalfun")

// ---------------------------------------------------------------------------
// Screen dimensions
// ---------------------------------------------------------------------------
#define VD_COLS  40
#define VD_ROWS  24

// ---------------------------------------------------------------------------
// Viewdata 8-colour values — ANSI 4-bit colour indices mapped to the
// "bright" (high-intensity) ANSI palette entries (8-15) so that
// framebuf_set_color() produces full-brightness primaries matching the
// original SAA5050 chip output.  Black stays at index 0 (pure black).
// ---------------------------------------------------------------------------
#define VD_BLACK    0u
#define VD_RED      9u
#define VD_GREEN    10u
#define VD_YELLOW   11u
#define VD_BLUE     12u
#define VD_MAGENTA  13u
#define VD_CYAN     14u
#define VD_WHITE    15u

// Colour-index table: C1 code offset (1-7) -> framebuf colour
// Alpha: code 0x41-0x47 -> index = code - 0x40 = 1-7
// Mosaic: code 0x51-0x57 -> index = code - 0x50 = 1-7
static const uint8_t vd_colour_table[8] =
{
  VD_BLACK, VD_RED, VD_GREEN, VD_YELLOW,
  VD_BLUE,  VD_MAGENTA, VD_CYAN, VD_WHITE
};

// ---------------------------------------------------------------------------
// Raw screen buffer
// ---------------------------------------------------------------------------
static uint8_t vd_buf [VD_ROWS][VD_COLS];
static uint8_t vd_ctrl[VD_ROWS][VD_COLS];

// ---------------------------------------------------------------------------
// Cursor and Mode 7 state
// ---------------------------------------------------------------------------
static int     vd_col, vd_row;
static bool    vd_cursor_on;
static uint8_t m7_vdu31_state;   // 0=idle, 1=waiting x, 2=waiting y
static uint8_t m7_vdu31_x;       // stored x (column) byte
static uint8_t m7_absorb_count;  // param bytes still to skip

// ---------------------------------------------------------------------------
// Row-level attribute state (rebuilt by vd_render_row scanning from col 0)
// ---------------------------------------------------------------------------
typedef struct {
  uint8_t fg;
  uint8_t bg;
  bool    graphics;
  bool    flash;
  bool    conceal;
  bool    separated;
  bool    double_height;
  bool    hold_graphics;
  uint8_t held_slot;
  bool    held_slot_valid;
} VDRowState;

static void vd_row_state_defaults(VDRowState *s)
{
  s->fg            = VD_WHITE;
  s->bg            = VD_BLACK;
  s->graphics      = false;
  s->flash         = false;
  s->conceal       = false;
  s->separated     = false;
  s->double_height = false;
  s->hold_graphics    = false;
  s->held_slot        = 0x00;
  s->held_slot_valid  = false;
}

// ---------------------------------------------------------------------------
// Mosaic font slot helper
// ---------------------------------------------------------------------------
static INFLASHFUN uint8_t vd_mosaic_slot(uint8_t c, bool separated)
{
  uint8_t pattern;
  if      (c >= 0x20 && c <= 0x3F) pattern = c - 0x20u;
  else if (c >= 0x60 && c <= 0x7F) pattern = c - 0x40u;
  else                              return 0xFF; // blast-through
  return pattern + (separated ? 0x40u : 0x00u);
}

// ---------------------------------------------------------------------------
// Scan source row and write rendered output to dst_row.
// ---------------------------------------------------------------------------
static INFLASHFUN bool vd_render_row_to(int src, int dst)
{
  VDRowState s;
  vd_row_state_defaults(&s);
  bool saw_double_height = false;

  for (int col = 0; col < VD_COLS; col++) {
    uint8_t raw   = vd_buf [src][col];
    uint8_t ctrl  = vd_ctrl[src][col];
    uint8_t display_char;
    uint8_t attr = 0;

    uint8_t cell_fg, cell_bg;

    if (ctrl) {
      uint8_t fg_before      = s.fg;
      uint8_t bg_before      = s.bg;
      bool    flash_before   = s.flash;
      bool    conceal_before = s.conceal;
      bool    hold_before    = s.hold_graphics;

      switch (ctrl) {
        case 0x40:
          s.graphics        = false;
          s.conceal         = false;
          s.held_slot       = 0x00;
          s.held_slot_valid = false;
          break;
        case 0x41: case 0x42: case 0x43: case 0x44:
        case 0x45: case 0x46: case 0x47:
          s.fg              = vd_colour_table[ctrl - 0x40u];
          s.graphics        = false;
          s.conceal         = false;
          s.held_slot       = 0x00;
          s.held_slot_valid = false;
          break;

        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54:
        case 0x55: case 0x56: case 0x57:
          s.fg       = vd_colour_table[ctrl - 0x50u];
          s.graphics = true;
          s.conceal  = false;
          if (!s.hold_graphics) { s.held_slot = 0x00; s.held_slot_valid = false; }
          break;

        case 0x48: s.flash         = true;  break;
        case 0x49: s.flash         = false; break;
        case 0x4A: break;
        case 0x4B: break;
        case 0x4C:
          s.double_height   = false;
          s.held_slot       = 0x00;
          s.held_slot_valid = false;
          break;
        case 0x4D:
          s.double_height   = true;
          saw_double_height = true;
          s.held_slot       = 0x00;
          s.held_slot_valid = false;
          break;
        case 0x58: s.conceal       = true;  break;
        case 0x59: s.separated     = false; break;
        case 0x5A: s.separated     = true;  break;
        case 0x5C: s.bg            = VD_BLACK; break;
        case 0x5D: s.bg            = s.fg;    break;
        case 0x5E: s.hold_graphics = true;  break;
        case 0x5F: s.hold_graphics = false; break;
        default: break;
      }

      bool display_hold = (ctrl == 0x5E) ? s.hold_graphics : hold_before;

      if (display_hold && s.graphics && s.held_slot_valid) {
        display_char = s.held_slot;
        attr = ATTR_BOLD;
      } else {
        display_char = 0x20;
        attr = 0;
      }
      if (flash_before) attr |= ATTR_BLINK;

      cell_bg = (ctrl == 0x5C || ctrl == 0x5D) ? s.bg : bg_before;
      cell_fg = conceal_before ? bg_before : fg_before;

    } else {
      if (s.graphics) {
        uint8_t slot = vd_mosaic_slot(raw, s.separated);
        if (slot == 0xFF) {
          display_char = raw;
          attr = 0;
        } else {
          display_char      = slot;
          attr              = ATTR_BOLD;
          s.held_slot       = slot;
          s.held_slot_valid = true;
        }
      } else {
        display_char = raw;
        attr = 0;
      }
      if (s.flash) attr |= ATTR_BLINK;

      cell_fg = s.conceal ? s.bg : s.fg;
      cell_bg = s.bg;
    }

    framebuf_set_char (2*col,   dst, display_char);
    framebuf_set_char (2*col+1, dst, display_char | 0x80);
    framebuf_set_color(2*col,   dst, cell_fg, cell_bg);
    framebuf_set_color(2*col+1, dst, cell_fg, cell_bg);
    framebuf_set_attr (2*col,   dst, attr);
    framebuf_set_attr (2*col+1, dst, attr);
  }

  return saw_double_height;
}

// ---------------------------------------------------------------------------
// Render one row
// ---------------------------------------------------------------------------
static INFLASHFUN void vd_render_row(int row)
{
  bool is_lower_dbl = (row > 0) && (framebuf_get_row_attr(row-1) == ROW_ATTR_DBL_HEIGHT_TOP);

  if (is_lower_dbl) {
    vd_render_row_to(row - 1, row);
    framebuf_set_row_attr(row, ROW_ATTR_DBL_HEIGHT_BOT);
    return;
  }

  bool saw_double_height = vd_render_row_to(row, row);

  if (saw_double_height) {
    framebuf_set_row_attr(row, ROW_ATTR_DBL_HEIGHT_TOP);
  } else {
    framebuf_set_row_attr(row, 0);
  }
}

// ---------------------------------------------------------------------------
// Re-render the entire screen
// ---------------------------------------------------------------------------
static INFLASHFUN void vd_render_all(void)
{
  for (int r = 0; r < VD_ROWS; r++)
    vd_render_row(r);
}

// ---------------------------------------------------------------------------
// Cursor
// ---------------------------------------------------------------------------
static int  cursor_col_saved = -1;
static int  cursor_row_saved = -1;
static uint8_t cursor_attr_saved_l;
static uint8_t cursor_attr_saved_r;

static INFLASHFUN void vd_cursor_erase(void)
{
  if (cursor_col_saved >= 0) {
    framebuf_set_attr(2*cursor_col_saved,   cursor_row_saved, cursor_attr_saved_l);
    framebuf_set_attr(2*cursor_col_saved+1, cursor_row_saved, cursor_attr_saved_r);
    cursor_col_saved = -1;
  }
}

static INFLASHFUN void vd_cursor_draw(void)
{
  if (!vd_cursor_on) return;
  cursor_col_saved    = vd_col;
  cursor_row_saved    = vd_row;
  cursor_attr_saved_l = framebuf_get_attr(2*vd_col,   vd_row);
  cursor_attr_saved_r = framebuf_get_attr(2*vd_col+1, vd_row);
  framebuf_set_attr(2*vd_col,   vd_row, cursor_attr_saved_l ^ ATTR_INVERSE);
  framebuf_set_attr(2*vd_col+1, vd_row, cursor_attr_saved_r ^ ATTR_INVERSE);
}

// ---------------------------------------------------------------------------
// Screen clear
// ---------------------------------------------------------------------------
static INFLASHFUN void vd_clear_screen(void)
{
  memset(vd_buf,  0x20, sizeof(vd_buf));
  memset(vd_ctrl, 0x00, sizeof(vd_ctrl));
  framebuf_fill_screen(0x20, VD_WHITE, VD_BLACK);
  for (int r = 0; r < VD_ROWS; r++)
    framebuf_set_row_attr(r, 0);
}

// ---------------------------------------------------------------------------
// Scroll up one row
// ---------------------------------------------------------------------------
static INFLASHFUN void vd_scroll_up(void)
{
  memmove(vd_buf[0],  vd_buf[1],  (VD_ROWS - 1) * VD_COLS);
  memmove(vd_ctrl[0], vd_ctrl[1], (VD_ROWS - 1) * VD_COLS);
  memset(vd_buf [VD_ROWS - 1], 0x20, VD_COLS);
  memset(vd_ctrl[VD_ROWS - 1], 0x00, VD_COLS);
  framebuf_scroll_screen(1, VD_WHITE, VD_BLACK);
  vd_render_all();
}

// ---------------------------------------------------------------------------
// Move cursor with wrapping
// ---------------------------------------------------------------------------
static INFLASHFUN void vd_move_cursor(int row, int col)
{
  vd_cursor_erase();
  if (col < 0)        { col = VD_COLS - 1; row--; }
  if (col >= VD_COLS) { col = 0;           row++; }
  if (row < 0)        row = VD_ROWS - 1;
  if (row >= VD_ROWS) row = VD_ROWS - 1;
  vd_col = col;
  vd_row = row;
}

// ---------------------------------------------------------------------------
// Write a cell to the raw buffer then re-render its row
// ---------------------------------------------------------------------------
static INFLASHFUN void vd_put_cell(uint8_t raw_char, uint8_t ctrl_code)
{
  vd_cursor_erase();

  vd_buf [vd_row][vd_col] = raw_char;
  vd_ctrl[vd_row][vd_col] = ctrl_code;

  vd_render_row(vd_row);
  if (vd_row + 1 < VD_ROWS) {
    bool is_dbl = false;
    for (int c = 0; c < VD_COLS; c++)
      if (vd_ctrl[vd_row][c] == 0x4D) { is_dbl = true; break; }
    if (is_dbl) vd_render_row(vd_row + 1);
  }

  vd_col++;
  if (vd_col >= VD_COLS) {
    vd_col = 0;
    vd_row++;
    if (vd_row >= VD_ROWS) {
      vd_scroll_up();
      vd_row = VD_ROWS - 1;
    }
  }

  if (!serial_readable()) vd_cursor_draw();
}

// ---------------------------------------------------------------------------
// Public: init
// ---------------------------------------------------------------------------
void INFLASHFUN terminal_mode7_init(void)
{
  vd_cursor_on     = true;
  m7_vdu31_state   = 0;
  m7_vdu31_x       = 0;
  m7_absorb_count  = 0;
  cursor_col_saved = -1;
  cursor_row_saved = -1;
  vd_col           = 0;
  vd_row           = 0;

  vd_clear_screen();
  vd_cursor_draw();
}

// ---------------------------------------------------------------------------
// Public: apply settings
// ---------------------------------------------------------------------------
void INFLASHFUN terminal_mode7_apply_settings(void)
{
  font_apply_font(FONT_ID_VIEWDATA,        false);
  font_apply_font(FONT_ID_VIEWDATA_MOSAIC, true);

  framebuf_set_screen_size(80, VD_ROWS);

  terminal_mode7_init();
}

// ---------------------------------------------------------------------------
// Public: receive a character from the host
// ---------------------------------------------------------------------------
void INFLASHFUN terminal_mode7_receive_char(char ch)
{
  // BBC Mode 7 is 8-bit clean — no parity stripping
  uint8_t c = (uint8_t)ch;

  // VDU parameter absorber: silently consume multi-byte command arguments
  if (m7_absorb_count > 0) { m7_absorb_count--; return; }

  // VDU 31 cursor-position state machine
  if (m7_vdu31_state == 1) { m7_vdu31_x = c; m7_vdu31_state = 2; return; }
  if (m7_vdu31_state == 2) {
    int col = (int)m7_vdu31_x;
    int row = (int)c;
    if (col >= VD_COLS) col = VD_COLS - 1;
    if (row >= VD_ROWS) row = VD_ROWS - 1;
    vd_move_cursor(row, col);
    m7_vdu31_state = 0;
    return;
  }

  switch (c) {
    case 0x00: return;                                          // NUL  ignore
    case 0x08: vd_move_cursor(vd_row,   vd_col-1); return;    // BS   left
    case 0x09: vd_move_cursor(vd_row,   vd_col+1); return;    // HT   right
    case 0x0A:                                                  // LF   down (scroll at bottom)
      if (vd_row + 1 >= VD_ROWS) {
        vd_cursor_erase();
        vd_scroll_up();
      } else {
        vd_move_cursor(vd_row + 1, vd_col);
      }
      return;
    case 0x0B: vd_move_cursor(vd_row-1, vd_col);   return;    // VT   up
    case 0x0C:                                                  // FF   clear+home
      vd_cursor_erase(); vd_clear_screen();
      vd_col = 0; vd_row = 0; return;
    case 0x0D: vd_move_cursor(vd_row, 0);           return;    // CR   col→0
    case 0x11: m7_absorb_count = 1; return;                    // VDU 17: set text colour (1 param)
    case 0x12: m7_absorb_count = 2; return;                    // VDU 18: set graphics colour (2 params)
    case 0x13: m7_absorb_count = 5; return;                    // VDU 19: define colour (5 params)
    case 0x16: m7_absorb_count = 1; return;                    // VDU 22: mode select (1 param)
    case 0x17: m7_absorb_count = 9; return;                    // VDU 23: define char (9 params)
    case 0x18: m7_absorb_count = 8; return;                    // VDU 24: graphics viewport (8 params)
    case 0x19: m7_absorb_count = 5; return;                    // VDU 25: plot (5 params)
    case 0x1C: m7_absorb_count = 4; return;                    // VDU 28: text window (4 params)
    case 0x1D: m7_absorb_count = 4; return;                    // VDU 29: graphics origin (4 params)
    case 0x1E:                                                  // VDU 30: unconditional home
      vd_cursor_erase(); vd_row = 0; vd_col = 0; return;
    case 0x1F: m7_vdu31_state = 1; return;                     // VDU 31: cursor x,y
    default: break;
  }

  // 8-bit direct C1 (0x80-0x9F): direct teletext attribute codes
  if (c >= 0x80u && c <= 0x9Fu) {
    vd_put_cell(0x20u, (uint8_t)(c - 0x40u));
    return;
  }

  // Printable (0x20-0x7F, or 0xA0-0xFF with bit 7 set — strip it)
  if (c >= 0x20u)
    vd_put_cell(c & 0x7Fu, 0x00u);
}

// ---------------------------------------------------------------------------
// Public: process a keypress
// ---------------------------------------------------------------------------
void INFLASHFUN terminal_mode7_process_key(uint16_t key)
{
  bool isaltcode = false;
  uint8_t c = keyboard_map_key_ascii(key, &isaltcode);

  // LeftAlt-NNN pass-through
  if (isaltcode) { serial_send_char((char)c); return; }
  if (c == 0)    return;

  switch (c) {
    // Navigation — VDU cursor codes
    case KEY_UP:        serial_send_char(0x0B); return;
    case KEY_DOWN:      serial_send_char(0x0A); return;
    case KEY_LEFT:      serial_send_char(0x08); return;
    case KEY_RIGHT:     serial_send_char(0x09); return;
    case KEY_HOME:      serial_send_char(0x1E); return;

    // Enter sends CR
    case KEY_ENTER:     serial_send_char(0x0D); return;

    // Backspace/Delete send cursor-left
    case KEY_BACKSPACE: serial_send_char(0x08); return;
    case KEY_DELETE:    serial_send_char(0x08); return;

    // Function keys — VT-style escape sequences
    case KEY_F1:        serial_send_string("\033OP"); return;
    case KEY_F2:        serial_send_string("\033OQ"); return;
    case KEY_F3:        serial_send_string("\033OR"); return;
    case KEY_F4:        serial_send_string("\033OS"); return;
    // F5-F10: no standard BBC serial mapping; omit

    default:
      if (c >= 0x20 && c < 0x7F) {
        serial_send_char((char)c);
        if (config_get_terminal_localecho()) terminal_mode7_receive_char((char)c);
      }
      return;
  }
}
