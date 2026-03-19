// -----------------------------------------------------------------------------
// terminal_viewdata.c — Prestel/Viewdata terminal emulator for VersaTerm
//
// Implements the Prestel/Viewdata (UK Videotex) protocol on top of VersaTerm's
// existing framebuf layer.
//
// Display geometry
// ----------------
//   40 columns × 24 rows, rendered at 640×480 using:
//     - 8×20 pixel Viewdata font split into left/right 8-px halves per char
//     - 80 physical columns × 8 px = 640 px (sw renderer, no double-width)
//     - Each logical column N → physical columns 2N (left half) and 2N+1 (right half)
//     - Right-half char code = left-half code | 0x80
//
// Font slots (in VersaTerm's normal/bold font pair)
// --------------------------------------------------
//   "Normal" font  → FONT_ID_VIEWDATA        (Bedstead alpha G0 set)
//   "Bold"   font  → FONT_ID_VIEWDATA_MOSAIC (programmatic mosaic G1 set)
//
//   Both fonts use the 16-px-native split:
//     Slots 0x00-0x7F : left  half (written to physical column 2N)
//     Slots 0x80-0xFF : right half (written to physical column 2N+1, char|0x80)
//
//   Mosaic character indexing:
//     left_slot  = pattern + (separated ? 0x40 : 0x00)
//     right_slot = left_slot | 0x80
//     where pattern 0..63 encodes the 6 sixel bits.
//
// Attribute model
// ---------------
//   Viewdata attributes are "in-band" and streaming: a C1 control code in a
//   cell affects all following cells on the same row.  On every new row the
//   attributes reset to defaults (white alpha text on black).
//
//   We store the raw byte for every cell plus a flag marking C1 control codes.
//   To render correctly we re-scan each row left-to-right whenever a cell
//   changes (mirroring the syncterm prestel_get_state() approach).
//
// C0 controls (0x00-0x1F)
// ------------------------
//   0x08  BS   cursor left
//   0x09  HT   cursor right
//   0x0A  LF   cursor down
//   0x0B  VT   cursor up
//   0x0C  FF   clear screen + home
//   0x0D  CR   carriage return (column 0, new row)
//   0x11  DC1  cursor on
//   0x14  DC4  cursor off
//   0x1B  ESC  next byte is C1 (in range 0x40-0x5F)
//   0x1E  RS   cursor home
//
// C1 control codes (byte sent after ESC, range 0x40-0x5F)
// --------------------------------------------------------
//   0x40  Alpha Black            0x50  Mosaic Black
//   0x41  Alpha Red              0x51  Mosaic Red
//   0x42  Alpha Green            0x52  Mosaic Green
//   0x43  Alpha Yellow           0x53  Mosaic Yellow
//   0x44  Alpha Blue             0x54  Mosaic Blue
//   0x45  Alpha Magenta          0x55  Mosaic Magenta
//   0x46  Alpha Cyan             0x56  Mosaic Cyan
//   0x47  Alpha White            0x57  Mosaic White
//   0x48  Flash                  0x58  Conceal
//   0x49  Steady                 0x59  Contiguous graphics
//   0x4C  Normal height          0x5A  Separated graphics
//   0x4D  Double height          0x5C  Black background
//                                0x5D  New background (fg -> bg)
//                                0x5E  Hold graphics
//                                0x5F  Release graphics
// -----------------------------------------------------------------------------

#include "terminal_viewdata.h"
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
// Bright ANSI DVI defaults: 9=0b110000 10=0b001100 11=0b111100
//                            12=0b000011 13=0b110011 14=0b001111 15=0b111111
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
// vd_buf  — the raw byte stored in each cell (0x20 = space if empty)
// vd_ctrl — if non-zero, the cell contains this C1 control code (0x40-0x5F)
static uint8_t vd_buf [VD_ROWS][VD_COLS];
static uint8_t vd_ctrl[VD_ROWS][VD_COLS];

// ---------------------------------------------------------------------------
// Cursor and escape state
// ---------------------------------------------------------------------------
static int  vd_col, vd_row;
static bool vd_cursor_on;
static bool vd_esc;      // true: next incoming byte is a C1 control code

// ---------------------------------------------------------------------------
// Row-level attribute state (rebuilt by vd_render_row scanning from col 0)
// ---------------------------------------------------------------------------
typedef struct {
  uint8_t fg;            // foreground framebuf colour
  uint8_t bg;            // background framebuf colour
  bool    graphics;      // in mosaic G1 mode
  bool    flash;
  bool    conceal;
  bool    separated;     // separated graphics mode
  bool    double_height;
  bool    hold_graphics;
  uint8_t held_slot;       // font slot of last displayed mosaic char
  bool    held_slot_valid; // true if held_slot contains a real mosaic slot
} VDRowState;

static void vd_row_state_defaults(VDRowState *s)
{
  s->fg           = VD_WHITE;
  s->bg           = VD_BLACK;
  s->graphics     = false;
  s->flash        = false;
  s->conceal      = false;
  s->separated    = false;
  s->double_height = false;
  s->hold_graphics    = false;
  s->held_slot        = 0x00;
  s->held_slot_valid  = false;
}

// ---------------------------------------------------------------------------
// Mosaic font slot helper
// ---------------------------------------------------------------------------
// Given a displayable byte in graphics mode, return the font slot index in
// font_viewdata_mosaic.  Returns 0xFF for "blast-through" chars (0x40-0x5F).
static INFLASHFUN uint8_t vd_mosaic_slot(uint8_t c, bool separated)
{
  uint8_t pattern;
  if      (c >= 0x20 && c <= 0x3F) pattern = c - 0x20u;
  else if (c >= 0x60 && c <= 0x7F) pattern = c - 0x40u;
  else                              return 0xFF; // blast-through
  return pattern + (separated ? 0x40u : 0x00u);
}

// ---------------------------------------------------------------------------
// Scan source row's buffer data and write rendered output to dst_row.
// Used both for normal rendering (src==dst) and for the lower half of a
// double-height pair (src==row-1, dst==row).
// Returns true if a double-height control code was seen (only meaningful
// when src==dst, i.e. rendering the top row).
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
      // ---------- C1 control code in this cell ----------
      // Most codes are SET AFTER: the new attribute applies from the NEXT cell.
      // The control cell itself renders as a space (or held graphic) using the
      // PRE-UPDATE attribute state.  Exceptions: Black Background (0x5C) and
      // Hold Graphics (0x5E) are SET AT and take effect on the control cell too.

      // Snapshot all state that feeds the control-cell's visual appearance.
      uint8_t fg_before      = s.fg;
      uint8_t bg_before      = s.bg;
      bool    flash_before   = s.flash;
      bool    conceal_before = s.conceal;
      bool    hold_before    = s.hold_graphics;

      switch (ctrl) {
        case 0x40:                                    // Alpha Black: enter alpha mode, keep fg
          s.graphics        = false;
          s.conceal         = false;
          s.held_slot       = 0x00;
          s.held_slot_valid = false;
          break;
        case 0x41: case 0x42: case 0x43: case 0x44:
        case 0x45: case 0x46: case 0x47:              // Alpha colours (SET AFTER)
          s.fg              = vd_colour_table[ctrl - 0x40u];
          s.graphics        = false;
          s.conceal         = false;
          s.held_slot       = 0x00;    // always clear held graphic on alpha colour
          s.held_slot_valid = false;
          break;

        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54:
        case 0x55: case 0x56: case 0x57:              // Mosaic colours incl. Black (SET AFTER)
          s.fg       = vd_colour_table[ctrl - 0x50u];
          s.graphics = true;
          s.conceal  = false;
          if (!s.hold_graphics) { s.held_slot = 0x00; s.held_slot_valid = false; }
          break;

        case 0x48: s.flash         = true;  break;    // Flash   (SET AFTER)
        case 0x49: s.flash         = false; break;    // Steady  (SET AFTER)
        case 0x4A: break;                             // End Box   (not used in Prestel)
        case 0x4B: break;                             // Start Box (not used in Prestel)
        case 0x4C:                                    // Normal Height (SET AFTER)
          s.double_height   = false;
          s.held_slot       = 0x00;
          s.held_slot_valid = false;
          break;
        case 0x4D:                                    // Double Height (SET AFTER)
          s.double_height   = true;
          saw_double_height = true;
          s.held_slot       = 0x00;
          s.held_slot_valid = false;
          break;
        case 0x58: s.conceal       = true;  break;    // Conceal        (SET AFTER)
        case 0x59: s.separated     = false; break;    // Contiguous Gfx (SET AFTER)
        case 0x5A: s.separated     = true;  break;    // Separated Gfx  (SET AFTER)
        case 0x5C: s.bg            = VD_BLACK; break; // Black Background (SET AT)
        case 0x5D: s.bg            = s.fg;    break;  // New Background   (SET AT)
        case 0x5E: s.hold_graphics = true;  break;    // Hold Graphics    (SET AT)
        case 0x5F: s.hold_graphics = false; break;    // Release Graphics (SET AFTER)
        default: break;
      }

      // For SET AT codes (0x5C Black Bg, 0x5E Hold Graphics), the new state
      // applies at the control cell.  For all SET AFTER codes, use pre-update.
      // hold_graphics: 0x5E is SET AT (use post-update); 0x5F is SET AFTER (use pre).
      bool display_hold = (ctrl == 0x5E) ? s.hold_graphics : hold_before;

      if (display_hold && s.graphics && s.held_slot_valid) {
        display_char = s.held_slot;
        attr = ATTR_BOLD;
      } else {
        display_char = 0x20;
        attr = 0;
      }
      if (flash_before) attr |= ATTR_BLINK;

      // Colour: 0x5C (Black Bg) and 0x5D (New Bg) are SET AT so use updated s.bg;
      // everything else uses pre-update fg/bg.
      cell_bg = (ctrl == 0x5C || ctrl == 0x5D) ? s.bg : bg_before;
      cell_fg = conceal_before ? bg_before : fg_before;

    } else {
      // ---------- Displayable character ----------
      if (s.graphics) {
        uint8_t slot = vd_mosaic_slot(raw, s.separated);
        if (slot == 0xFF) {
          // Blast-through: alpha glyph rendered in graphics mode
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

    // Write two physical columns per logical column:
    //   2*col   → left  half (char as-is)
    //   2*col+1 → right half (char | 0x80, same attr/colour)
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
// Render one row by scanning attributes left-to-right from column 0
// ---------------------------------------------------------------------------
static INFLASHFUN void vd_render_row(int row)
{
  // Determine whether this row is the lower row of a double-height pair.
  // Use the already-rendered row attribute of the previous row rather than
  // scanning raw ctrl codes - this prevents cascading when the data also has
  // a double-height code on the bottom-half row itself.
  bool is_lower_dbl = (row > 0) && (framebuf_get_row_attr(row-1) == ROW_ATTR_DBL_HEIGHT_TOP);

  if (is_lower_dbl) {
    // Lower row of a double-height pair: re-render the row above's source data
    // into this row so that characters, colours and attrs all match exactly.
    vd_render_row_to(row - 1, row);
    framebuf_set_row_attr(row, ROW_ATTR_DBL_HEIGHT_BOT);
    return;
  }

  // Normal render: source and destination are the same row.
  bool saw_double_height = vd_render_row_to(row, row);

  // Set row attributes (no double-width needed; each logical col is two physical cols)
  if (saw_double_height) {
    framebuf_set_row_attr(row, ROW_ATTR_DBL_HEIGHT_TOP);
    // Lower row will be fixed on next call to vd_render_row for that row
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
// Cursor (simple: invert the attribute of the character under the cursor)
// ---------------------------------------------------------------------------
static int  cursor_col_saved = -1;
static int  cursor_row_saved = -1;
static uint8_t cursor_attr_saved_l;  // left  physical column attr
static uint8_t cursor_attr_saved_r;  // right physical column attr

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
// Move cursor with wrapping at column edges and row 0/23 boundary.
//
// Column wrap:  BS past col 0  → col 39, row-1  (BS wraps to end of prev row)
//               HT past col 39 → col 0,  row+1  (HT wraps to start of next row)
// Row wrap:     VT past row 0  → row 23          (wraps to last row)
//               LF past row 23 → row 23 (clamp)  (no scroll on cursor movement;
//                                                  only vd_put_cell scrolls)
// ---------------------------------------------------------------------------
static INFLASHFUN void vd_move_cursor(int row, int col)
{
  vd_cursor_erase();
  // Column edge wrapping adjusts the row before row bounds are checked
  if (col < 0)        { col = VD_COLS - 1; row--; }
  if (col >= VD_COLS) { col = 0;           row++; }
  // Row bounds: wrap upward, clamp downward
  if (row < 0)        row = VD_ROWS - 1;
  if (row >= VD_ROWS) row = VD_ROWS - 1;
  vd_col = col;
  vd_row = row;
  // Don't draw cursor while more data is incoming; it will be drawn
  // at the final position once the stream pauses.
  if (!serial_readable()) vd_cursor_draw();
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
  // If this is a double-height top row, also re-render the row below
  if (vd_row + 1 < VD_ROWS) {
    bool is_dbl = false;
    for (int c = 0; c < VD_COLS; c++)
      if (vd_ctrl[vd_row][c] == 0x4D) { is_dbl = true; break; }
    if (is_dbl) vd_render_row(vd_row + 1);
  }

  // Advance cursor
  vd_col++;
  if (vd_col >= VD_COLS) {
    vd_col = 0;
    vd_row++;
    if (vd_row >= VD_ROWS) {
      vd_scroll_up();
      vd_row = VD_ROWS - 1;
    }
  }

  // Don't draw cursor while more data is incoming; it will be drawn
  // at the final position once the stream pauses.
  if (!serial_readable()) vd_cursor_draw();
}

// ---------------------------------------------------------------------------
// Public: init
// ---------------------------------------------------------------------------
void INFLASHFUN terminal_viewdata_init(void)
{
  vd_cursor_on     = true;
  vd_esc           = false;
  cursor_col_saved = -1;
  cursor_row_saved   = -1;
  vd_col             = 0;
  vd_row             = 0;

  vd_clear_screen();
  vd_cursor_draw();
}

// ---------------------------------------------------------------------------
// Public: apply settings (called when switching to/from Viewdata mode)
// ---------------------------------------------------------------------------
void INFLASHFUN terminal_viewdata_apply_settings(void)
{
  // Viewdata uses Bedstead alpha as "normal" and mosaic as "bold"
  font_apply_font(FONT_ID_VIEWDATA,        false);
  font_apply_font(FONT_ID_VIEWDATA_MOSAIC, true);

  // 80 physical columns × 8 px = 640 px (sw TMDS renderer, no double-width).
  // Each logical Viewdata column N → physical columns 2N (left half, char as-is)
  // and 2N+1 (right half, char | 0x80), filling the full 640 px display.
  framebuf_set_screen_size(80, VD_ROWS);

  terminal_viewdata_init();
}

// ---------------------------------------------------------------------------
// Public: receive a character from the host
// ---------------------------------------------------------------------------
void INFLASHFUN terminal_viewdata_receive_char(char ch)
{
  // Strip parity (Prestel is 7-bit odd-parity over serial)
  uint8_t c = (uint8_t)ch & 0x7Fu;

  // --- ESC: next byte is C1 ---
  if (vd_esc) {
    vd_esc = false;
    if (c >= 0x40u && c <= 0x5Fu)
      vd_put_cell(0x20u, c);  // store as control cell
    return;
  }

  switch (c) {
    case 0x00: return;                                   // NUL  ignore
    case 0x08: vd_move_cursor(vd_row,   vd_col-1); return; // BS   left
    case 0x09: vd_move_cursor(vd_row,   vd_col+1); return; // HT   right
    case 0x0A: vd_move_cursor(vd_row+1, vd_col);   return; // LF   down
    case 0x0B: vd_move_cursor(vd_row-1, vd_col);   return; // VT   up
    case 0x0C:                                            // FF   clear
      vd_cursor_erase();
      vd_clear_screen();
      vd_col = 0; vd_row = 0;
      if (!serial_readable()) vd_cursor_draw();
      return;
    case 0x0D: vd_move_cursor(vd_row, 0);           return; // CR   col→0
    case 0x11: vd_cursor_erase(); vd_cursor_on = true; vd_cursor_draw(); return; // DC1 cursor on
    case 0x14: vd_cursor_erase(); vd_cursor_on = false; return; // DC4 cursor off
    case 0x1B: vd_esc = true;  return;                  // ESC
    case 0x1E:                                           // RS   home
      vd_cursor_erase();
      // Clear the bottom row (input line) to remove residual attribute codes
      // from previous page content.  The service sends RS before both new page
      // content and the '*' input prompt — neither sends FF first — so old
      // in-band attributes (e.g. Alpha-White + New-Background from a coloured
      // banner) would otherwise persist in vd_ctrl[23] and bleed into the
      // prompt rendering, turning it yellow-on-white instead of yellow-on-black.
      memset(vd_buf [VD_ROWS - 1], 0x20, VD_COLS);
      memset(vd_ctrl[VD_ROWS - 1], 0x00, VD_COLS);
      vd_render_row(VD_ROWS - 1);
      vd_col = 0; vd_row = 0;
      if (!serial_readable()) vd_cursor_draw();
      return;
    default: break;
  }

  // 8-bit direct C1 (0x80-0x9F): some services send these instead of ESC+code
  if (c >= 0x80u && c <= 0x9Fu) {
    vd_put_cell(0x20u, (uint8_t)(c - 0x40u));
    return;
  }

  // Printable (0x20-0x7F)
  if (c >= 0x20u)
    vd_put_cell(c, 0x00u);
}

// ---------------------------------------------------------------------------
// Public: process a keypress (key is VersaTerm uint16_t key value)
// ---------------------------------------------------------------------------
void INFLASHFUN terminal_viewdata_process_key(uint16_t key)
{
  bool isaltcode = false;
  uint8_t c = keyboard_map_key_ascii(key, &isaltcode);

  // LeftAlt-NNN pass-through
  if (isaltcode) { serial_send_char((char)c); return; }
  if (c == 0)    return;

  switch (c) {
    // Navigation — Prestel cursor codes
    case KEY_UP:        serial_send_char(0x0B); return;
    case KEY_DOWN:      serial_send_char(0x0A); return;
    case KEY_LEFT:      serial_send_char(0x08); return;
    case KEY_RIGHT:     serial_send_char(0x09); return;
    case KEY_HOME:      serial_send_char(0x1E); return;

    // Enter sends CR (Prestel standard)
    case KEY_ENTER:     serial_send_char(0x0D); return;

    // Backspace/Delete send cursor-left
    case KEY_BACKSPACE: serial_send_char(0x08); return;
    case KEY_DELETE:    serial_send_char(0x08); return;

    // Function keys — Prestel F-key codes
    case KEY_F1:        serial_send_char((char)0xB1); return;
    case KEY_F2:        serial_send_char((char)0xB2); return;
    case KEY_F3:        serial_send_char((char)0xB3); return;
    case KEY_F4:        serial_send_char((char)0xB4); return;
    case KEY_F5:        serial_send_char((char)0xB5); return;
    case KEY_F6:        serial_send_char((char)0xB6); return;
    case KEY_F7:        serial_send_char((char)0xB7); return;
    case KEY_F8:        serial_send_char((char)0xB8); return;
    case KEY_F9:        serial_send_char((char)0xB9); return;
    case KEY_F10:       serial_send_char((char)0xB0); return;

    default:
      // Printable ASCII — send as-is (already layout-mapped by keyboard_map_key_ascii)
      if (c >= 0x20 && c < 0x7F) {
        serial_send_char((char)c);
        if (config_get_terminal_localecho()) terminal_viewdata_receive_char((char)c);
      }
      return;
  }
}
