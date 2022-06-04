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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pico/bootrom.h"
#include "tusb.h"
#include "framebuf.h"
#include "serial.h"
#include "keyboard.h"
#include "terminal.h"
#include "config.h"
#include "font.h"
#include "pins.h"
#include "sound.h"


// see comment at start of main()
#define BOOTSEL_TIMEOUT_MS 1500
static absolute_time_t bootsel_timeout = 0;
static const uint32_t bootsel_magic[] = {0xf01681de, 0xbd729b29, 0xd359be7a};
static uint32_t __uninitialized_ram(bootsel_magic_ram)[count_of(bootsel_magic)];
static uint16_t ignore_key = HID_KEY_NONE;


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

  // handle bootsel mechanism timeout
  if( bootsel_timeout>0 && get_absolute_time()>=bootsel_timeout )
    {
      bootsel_timeout = 0;
      for(uint i=0; i<count_of(bootsel_magic); i++) 
        bootsel_magic_ram[i] = 0;
    }
  
  // process keyboard input
  keyboard_task();
  if( processInput && keyboard_num_keypress()>0 )
    {
      uint16_t key = keyboard_read_keypress();
      if( key!=ignore_key )
        {
          ignore_key = HID_KEY_NONE;

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
}


void wait(uint32_t milliseconds)
{
  absolute_time_t timeout = make_timeout_time_ms(milliseconds);
  while( get_absolute_time()<timeout ) run_tasks(false);
}


int main()
{
  // The following mechanism is fundamentally the same as the pico_bootsel_via_double_reset
  // library but that library uses a busy wait until the maximum time for the double-tap
  // has expired. Implementing it ourselves here instead allows to use that wait time
  // for initialization.
  uint i;
  for(i=0; i<count_of(bootsel_magic) && bootsel_magic_ram[i]==bootsel_magic[i]; i++);

  if( i<count_of(bootsel_magic) )
    {
      // arm mechanism and set timeout
      for(i=0; i<count_of(bootsel_magic); i++) 
        bootsel_magic_ram[i] = bootsel_magic[i];
      bootsel_timeout = make_timeout_time_ms(BOOTSEL_TIMEOUT_MS);
    }
  else
    {
      // disarm our mechanism so pressing RESET in boot loader starts up normally
      for(i=0; i<count_of(bootsel_magic); i++) 
        bootsel_magic_ram[i] = 0;
      
      // boot into boot-loader using GPIO25 (on-board LED) as activity LED
      reset_usb_boot(1<<25, 0);
    }
  
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

  // allow some time for keyboard(s) to initialize
  wait(tuh_inited() ? 1500 : 250);
  
  // if DEFAULTS button and CTRL key is pressed then force DVI
  if( !gpio_get(PIN_DEFAULTS) )
    framebuf_init((keyboard_get_current_modifiers() & (KEYBOARD_MODIFIER_LEFTCTRL|KEYBOARD_MODIFIER_RIGHTCTRL))!=0);
  else
    {
      // check F1-F10 keys for startup config
      while( keyboard_num_keypress()>0 )
        {
          uint16_t c = keyboard_read_keypress();
          if( (c & 0xFF)>=HID_KEY_F1 && (c & 0xFF)<=HID_KEY_F10 && config_load((c & 0xFF)-HID_KEY_F1) )
            {
              // apply settings (may have changed)
              keyboard_apply_settings();
              serial_apply_settings();
              // ignore further repeats of Fx key 
              ignore_key = c;
              break;
            }
        }

      framebuf_init(false);
    }
  
  terminal_init();
  sound_init();
  config_show_splash();

  while( true ) run_tasks(true);
}
