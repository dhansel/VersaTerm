#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "tusb.h"
#include "framebuf.h"
#include "serial.h"
#include "keyboard.h"
#include "terminal.h"
#include "config.h"
#include "font.h"
#include "pins.h"
#include "sound.h"


void apply_settings()
{
  font_apply_settings();
  framebuf_apply_settings();
  keyboard_apply_settings();
  terminal_apply_settings();
  serial_apply_settings();
}


void wait(uint32_t milliseconds);

void run_tasks(bool processInput)
{
  // tinyusb tasks
  if( tud_inited() ) tud_task();
  if( tuh_inited() ) tuh_task();
  
  // process serial input
  serial_task(processInput);
  
  // process keyboard input
  keyboard_task();
  if( processInput && keyboard_num_keypress()>0 )
    {
      uint16_t key = keyboard_read_keypress();
      
      if( key==HID_KEY_F12 )
        {
          if( config_menu() ) apply_settings();
        }
      else if( keyboard_ctrl_pressed(key) && (key&0xFF)==HID_KEY_F12 )
        {
          if( config_load(0xFF) ) apply_settings();
        }
      else if( key==HID_KEY_F11 )
        keyboard_macro_record_startstop();
      else if( keyboard_ctrl_pressed(key) && (key&0xFF)>=HID_KEY_F1 && (key&0xFF)<=HID_KEY_F10 )
        {
          uint8_t vol = config_get_audible_bell_volume();
          if( config_load((key&0xFF)-HID_KEY_F1) )
            {
              apply_settings();
              sound_play_tone(880, 50, vol, false);
            }
          else
            {
              sound_play_tone(880, 50, vol, true); wait(50);
              sound_play_tone(880, 50, vol, true); wait(50);
              sound_play_tone(880, 50, vol, false); 
            }
        }
      else
        terminal_process_key(key);
    }
}


void wait(uint32_t milliseconds)
{
  absolute_time_t timeout = make_timeout_time_ms(milliseconds);
  while( get_absolute_time()<timeout ) run_tasks(false);
}


int main()
{
  config_init();
  stdio_uart_init_full(PIN_UART_ID, 300, PIN_UART_TX, PIN_UART_RX);
  serial_init();

  // initialize USB (needed for keyboard)
  if( config_get_usb_mode()==1 )
    tud_init(TUD_OPT_RHPORT);
  else if( config_get_usb_mode()==2 )
    tuh_init(TUH_OPT_RHPORT);
  else if( config_get_usb_mode()==3 )
    {
      // GPIO24 is high if the Pico's on-board USB port is connected to power (i.e. a host)
      // In that case we want to be a device, otherwise a host
      gpio_init(24);
      gpio_set_dir(24, false); // input
      if( gpio_get(24) )
        tud_init(TUH_OPT_RHPORT);
      else
        tuh_init(TUH_OPT_RHPORT);
    }

  // initialize keyboard
  keyboard_init();

  // if DEFAULTS button and CTRL key is pressed then force DVI
  if( !gpio_get(PIN_DEFAULTS) )
    {
      // give some time for keyboard to fully initialize
      wait(1500);
      framebuf_init((keyboard_get_current_modifiers() & (KEYBOARD_MODIFIER_LEFTCTRL|KEYBOARD_MODIFIER_RIGHTCTRL))!=0);
    }
  else
    framebuf_init(false);

  terminal_init();
  sound_init();
  config_show_splash();

  while( true ) run_tasks(true);
}
