#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "pico/time.h"
#include "hardware/gpio.h"
#include "keyboard.h"
#include "keyboard_ps2.h"
#include "config.h"
#include "pins.h"

//#define DEBUG

#ifdef DEBUG
#include "terminal.h"
#include <stdarg.h>
static void print(const char *format, ...)
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


// defined in main.c
void wait(uint32_t milliseconds);
void run_tasks(bool processInput);


// keyboard receiver state
#define KBD_START  0
#define KBD_BIT0   1
#define KBD_BIT7   8
#define KBD_PARITY 9
#define KBD_STOP   10
#define KBD_ERROR  255
static int kbd_state = KBD_START;


// keyboard states
static uint8_t sendLEDStatus = 0xFF, ignoreBytes = 0;
static bool breakcode = false, extkey = false, keyboardPresent = false;
static int  waitResponse = 0;


static const uint8_t __in_flash(".keymaps") scancodes[136] = 
  { HID_KEY_NONE,      HID_KEY_F9,             HID_KEY_NONE,       HID_KEY_F5,              HID_KEY_F3,              HID_KEY_F1,        HID_KEY_F2,          HID_KEY_F12,      // 0x00-0x07
    HID_KEY_NONE,      HID_KEY_F10,            HID_KEY_F8,         HID_KEY_F6,              HID_KEY_F4,              HID_KEY_TAB,       HID_KEY_GRAVE,       HID_KEY_NONE,     // 0x08-0x0F
    HID_KEY_NONE,      HID_KEY_ALT_LEFT,       HID_KEY_SHIFT_LEFT, HID_KEY_NONE,            HID_KEY_CONTROL_LEFT,    HID_KEY_Q,         HID_KEY_1,           HID_KEY_NONE,     // 0x10-0x17
    HID_KEY_NONE,      HID_KEY_NONE,           HID_KEY_Z,          HID_KEY_S,               HID_KEY_A,               HID_KEY_W,         HID_KEY_2,           HID_KEY_NONE,     // 0x18-0x1F
    HID_KEY_NONE,      HID_KEY_C,              HID_KEY_X,          HID_KEY_D,               HID_KEY_E,               HID_KEY_4,         HID_KEY_3,           HID_KEY_NONE,     // 0x20-0x27
    HID_KEY_NONE,      HID_KEY_SPACE,          HID_KEY_V,          HID_KEY_F,               HID_KEY_T,               HID_KEY_R,         HID_KEY_5,           HID_KEY_NONE,     // 0x28-0x2F
    HID_KEY_NONE,      HID_KEY_N,              HID_KEY_B,          HID_KEY_H,               HID_KEY_G,               HID_KEY_Y,         HID_KEY_6,           HID_KEY_NONE,     // 0x30-0x37
    HID_KEY_NONE,      HID_KEY_NONE,           HID_KEY_M,          HID_KEY_J,               HID_KEY_U,               HID_KEY_7,         HID_KEY_8,           HID_KEY_NONE,     // 0x38-0x3F
    HID_KEY_NONE,      HID_KEY_COMMA,          HID_KEY_K,          HID_KEY_I,               HID_KEY_O,               HID_KEY_0,         HID_KEY_9,           HID_KEY_NONE,     // 0x40-0x47
    HID_KEY_NONE,      HID_KEY_PERIOD,         HID_KEY_SLASH,      HID_KEY_L,               HID_KEY_SEMICOLON,       HID_KEY_P,         HID_KEY_MINUS,       HID_KEY_NONE,     // 0x48-0x4F
    HID_KEY_NONE,      HID_KEY_NONE,           HID_KEY_APOSTROPHE, HID_KEY_NONE,            HID_KEY_BRACKET_LEFT,    HID_KEY_EQUAL,     HID_KEY_NONE,        HID_KEY_NONE,     // 0x50-0x57
    HID_KEY_CAPS_LOCK, HID_KEY_SHIFT_RIGHT,    HID_KEY_ENTER,      HID_KEY_BRACKET_RIGHT,   HID_KEY_NONE,            HID_KEY_BACKSLASH, HID_KEY_NONE,        HID_KEY_NONE,     // 0x58-0x5F
    HID_KEY_NONE,      HID_KEY_NONE,           HID_KEY_NONE,       HID_KEY_NONE,            HID_KEY_NONE,            HID_KEY_NONE,      HID_KEY_BACKSPACE,   HID_KEY_NONE,     // 0x60-0x67
    HID_KEY_NONE,      HID_KEY_KEYPAD_1,       HID_KEY_NONE,       HID_KEY_KEYPAD_4,        HID_KEY_KEYPAD_7,        HID_KEY_NONE,      HID_KEY_NONE,        HID_KEY_NONE,     // 0x68-0x6F
    HID_KEY_KEYPAD_0,  HID_KEY_KEYPAD_DECIMAL, HID_KEY_KEYPAD_2,   HID_KEY_KEYPAD_5,        HID_KEY_KEYPAD_6,        HID_KEY_KEYPAD_8,  HID_KEY_ESCAPE,      HID_KEY_NUM_LOCK, // 0x70-0x77
    HID_KEY_F11,       HID_KEY_KEYPAD_ADD,     HID_KEY_KEYPAD_3,   HID_KEY_KEYPAD_SUBTRACT, HID_KEY_KEYPAD_MULTIPLY, HID_KEY_KEYPAD_9,  HID_KEY_SCROLL_LOCK, HID_KEY_NONE,     // 0x78-0x7F
    HID_KEY_NONE,      HID_KEY_NONE,           HID_KEY_NONE,       HID_KEY_F7,              HID_KEY_NONE,            HID_KEY_NONE,      HID_KEY_NONE,        HID_KEY_NONE};    // 0x80-0x87


    
void keyboard_reset()
{
  breakcode = false; extkey = false;
  kbd_state = KBD_START;
  waitResponse = 0;
  ignoreBytes = 0;
}


bool keyboard_wait_clk_timeout(bool state, uint32_t timeout)
{
  // time out after 200us
  absolute_time_t endtime = make_timeout_time_us(timeout);
  while( get_absolute_time() < endtime )
    if( gpio_get(PIN_PS2_CLOCK)==state )
      return true;
  
  return false;
}


bool keyboard_wait_clk(bool state)
{
  return keyboard_wait_clk_timeout(state, 200);
}


bool keyboard_send_bits(uint8_t data)
{
  int i;
  bool parity = true;

  for(i=0; i<8; i++)
    {
      // wait for CLK low
      if( !keyboard_wait_clk_timeout(false, 100000) ) return false;

      if( data&1 )
        { gpio_set_dir(PIN_PS2_DATA, false); parity = !parity; } // data high
      else
        gpio_set_dir(PIN_PS2_DATA, true); // data low
  
      // shift data
      data >>= 1;
    
      // wait for CLK high
      if( !keyboard_wait_clk(true) ) return false;
    }

  // wait for CLK low
  if( !keyboard_wait_clk(false) ) return false;

  // send parity bit
  if( parity )
    gpio_set_dir(PIN_PS2_DATA, false); 
  else
    gpio_set_dir(PIN_PS2_DATA, true); 
          
  // wait for CLK high
  if( !keyboard_wait_clk(true) ) return false;
  
  // wait for CLK low
  if( !keyboard_wait_clk(false) ) return false;

  // send stop bit (1))
  gpio_set_dir(PIN_PS2_DATA, false); 
          
  // wait for CLK high
  if( !keyboard_wait_clk(true) ) return false;
  
  // release DATA
  gpio_set_dir(PIN_PS2_DATA, false); 
  
  // wait for CLK low transition
  if( !keyboard_wait_clk(false) ) return false;
  
  // read ACK bit (must be 0)
  if( gpio_get(PIN_PS2_DATA) )
    return false;

  // wait for CLK high transition
  if( !keyboard_wait_clk(true) ) return false;

  return true;  
}


bool keyboard_send_byte(uint8_t data)
{
  bool ok = false;

  // disable CLK interrupts
  gpio_set_irq_enabled(PIN_PS2_CLOCK, GPIO_IRQ_EDGE_FALL, false);

  // pull CLK line low 
  gpio_set_dir(PIN_PS2_CLOCK, true); 
  busy_wait_us_32(110);
  
  // pull DATA line low and release CLK
  gpio_set_dir(PIN_PS2_DATA, true); 
  gpio_set_dir(PIN_PS2_CLOCK, false); 
  // [wait ?us for CLK to settle]

  ok = keyboard_send_bits(data);
  
  // re-enable CLK interrupts
  gpio_set_irq_enabled(PIN_PS2_CLOCK, GPIO_IRQ_EDGE_FALL, true);
  
  return ok;
}


uint8_t keyboard_send_byte_wait_response(uint8_t b)
{
  uint8_t retries = 10;
  while( retries-- > 0 )
    {
      waitResponse = -1;
      if( keyboard_send_byte(b) ) 
        {
          absolute_time_t timeout = make_timeout_time_ms(100);
          while( waitResponse<0 && get_absolute_time()<timeout );
          if( waitResponse>=0 && waitResponse!=0xFE ) retries = 0;
        }
      
      if( retries>0 ) wait(10);
    }
    
  return waitResponse < 0 ? 0 : waitResponse;
}


static bool keyboard_send_led_status(uint8_t leds)
{
  if( keyboard_send_byte_wait_response(0xED)==0xFA )
    if( keyboard_send_byte_wait_response(leds)==0xFA )
      return true;
   
  return false;
}

               
static bool keyboard_set_repeat_rate(uint8_t rate)
{
  if( keyboard_send_byte_wait_response(0xF3)==0xFA )
    if( keyboard_send_byte_wait_response(rate)==0xFA )
      return true;
   
  return false;
}


static void process_byte(uint8_t b)
{
  uint8_t key = HID_KEY_NONE;

  print("%02X ", b);

  // translate key code to keycode
  if( ignoreBytes>0 )
    { ignoreBytes--; return; }
  else if( b == 0xE0 )
    extkey = true;
  else if( b == 0xE1 )
    {
      // pause/break key sends the following sequence when pressed
      // 0xE1, 0x14, 0x77, 0xE1, 0xF0, 0x14, 0xF0, 0x77
      // and nothing when released.
      // Since no other key sends 0xE1, we just take 0xE1 as meaning
      // "pause/break has been pressed and released" and ignore the
      // other bytes
      ignoreBytes = 7;
      keyboard_key_change(HID_KEY_PAUSE, true);
      keyboard_key_change(HID_KEY_PAUSE, false);
    }
  else if( extkey )
    {
      switch( b )
        {
        case 0x11: key = HID_KEY_ALT_RIGHT;     break;
        case 0x14: key = HID_KEY_CONTROL_RIGHT; break;
        case 0x1F: key = HID_KEY_GUI_LEFT;      break;
        case 0x27: key = HID_KEY_GUI_RIGHT;     break;
        case 0x4A: key = HID_KEY_KEYPAD_DIVIDE; break;
        case 0x5A: key = HID_KEY_ENTER;         break;
        case 0x69: key = HID_KEY_END;           break;
        case 0x6B: key = HID_KEY_ARROW_LEFT;    break;
        case 0x6C: key = HID_KEY_HOME;          break;
        case 0x70: key = HID_KEY_INSERT;        break;
        case 0x71: key = HID_KEY_DELETE;        break;
        case 0x72: key = HID_KEY_ARROW_DOWN;    break;
        case 0x74: key = HID_KEY_ARROW_RIGHT;   break;
        case 0x75: key = HID_KEY_ARROW_UP;      break;
        case 0x7A: key = HID_KEY_PAGE_DOWN;     break;
        case 0x7C: key = HID_KEY_PRINT_SCREEN;  break;
        case 0x7D: key = HID_KEY_PAGE_UP;       break;
        case 0x7E: key = HID_KEY_PAUSE;         break;
        }
    }
  else if( b < 136 )
    key = scancodes[b];

  if( key!=HID_KEY_NONE )
    keyboard_key_change(key, !breakcode);

  if( extkey && b!=0xE0 && b!=0xF0 ) extkey = false;
  breakcode = (b==0xF0);
}


static void keyboard_bit_received(bool data)
{
  static uint8_t kbd_data = 0, kbd_parity = 0;
  static absolute_time_t kbd_prev_pulse = 0;

  // clear error state if CLK line was idle for longer than 1ms
  if( kbd_state == KBD_ERROR && (get_absolute_time()-kbd_prev_pulse)>1000 )
    kbd_state = KBD_START;
  
  if( kbd_state == KBD_START )
    {
      // first bit is START bit and must be 0
      if( data )
        kbd_state = KBD_ERROR;
      else
        { kbd_state = KBD_BIT0; kbd_data = 0; kbd_parity = 1; }
    }
  else if( kbd_state >= KBD_BIT0 && kbd_state <= KBD_BIT7 )
    {
      kbd_data >>= 1;
      if( data ) { kbd_data |= 128; kbd_parity = !kbd_parity; }
      kbd_state++;
    }
  else if( kbd_state == KBD_PARITY )
    {
      if( kbd_parity == data )
        kbd_state = KBD_STOP;
      else
        kbd_state = KBD_ERROR;
    }
  else if( kbd_state == KBD_STOP )
    {
      if( kbd_data == 0xAA )
        keyboard_reset();
      else if( waitResponse<0 )
        waitResponse = kbd_data;
      else
        process_byte(kbd_data);
      
      kbd_state = KBD_START;
    }
}


void gpio_callback(uint gpio, uint32_t events) 
{
  if( !gpio_get(PIN_PS2_CLOCK) )
    keyboard_bit_received(gpio_get(PIN_PS2_DATA));
}


void keyboard_ps2_set_led_status(uint8_t leds)
{
  sendLEDStatus = 0;
  if( leds & KEYBOARD_LED_SCROLLLOCK ) sendLEDStatus |= 0x01;
  if( leds & KEYBOARD_LED_NUMLOCK )    sendLEDStatus |= 0x02;
  if( leds & KEYBOARD_LED_CAPSLOCK )   sendLEDStatus |= 0x04;
}


void keyboard_ps2_task()
{
  if( keyboardPresent && sendLEDStatus!=0xFF )
    { keyboard_send_led_status(sendLEDStatus); sendLEDStatus = 0xFF; }
}


void keyboard_ps2_apply_settings()
{
  if( keyboardPresent ) 
    keyboard_set_repeat_rate(((~config_get_keyboard_repeat_delay()<<5) & 0x60) | (~config_get_keyboard_repeat_rate() & 0x1F));
}


void keyboard_ps2_init()
{
  // reset keyboard states
  keyboard_reset();
  
  // Set up DATA/CLK pins
  // simulate open-collector by using pin direction:
  // - direction "output": outputs 0
  // - direction "input": high-z state, pull-up resistor makes 1
  gpio_init(PIN_PS2_CLOCK);
  gpio_set_dir(PIN_PS2_CLOCK, false); // input
  gpio_pull_up(PIN_PS2_CLOCK);
  gpio_put(PIN_PS2_CLOCK, false);

  gpio_init(PIN_PS2_DATA);
  gpio_set_dir(PIN_PS2_DATA, false); // input
  gpio_pull_up(PIN_PS2_DATA);
  gpio_put(PIN_PS2_DATA, false);

  // Set up change notification interrupt for CLK pin
  gpio_set_irq_enabled_with_callback(PIN_PS2_CLOCK, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
  
  keyboardPresent = keyboard_send_led_status(0);
  if( !keyboardPresent ) gpio_set_irq_enabled_with_callback(PIN_PS2_CLOCK, GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
  keyboard_ps2_apply_settings();
}
