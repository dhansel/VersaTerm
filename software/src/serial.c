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

#include "pins.h"
#include "serial.h"
#include "serial_uart.h"
#include "serial_cdc.h"
#include "config.h"
#include "terminal.h"


void serial_set_break(bool set)
{
  serial_uart_set_break(set);
  serial_cdc_set_break(set);
}


void serial_send_char(char c)
{
  if( config_get_usb_cdcmode()!=3 || !serial_cdc_is_connected() )
    serial_uart_send_char(c);

  if( config_get_usb_cdcmode()==1 ) serial_cdc_send_char(c);
}


void serial_send_string(const char *s)
{
  serial_uart_send_string(s);
  if( config_get_usb_cdcmode()==1 ) serial_cdc_send_string(s);
}


bool serial_readable()
{
  return serial_cdc_readable() || serial_uart_readable();
}


void serial_task(bool processInput)
{
  serial_uart_task(processInput);
  serial_cdc_task(processInput);
}


void serial_apply_settings()
{
  serial_uart_apply_settings();
  serial_cdc_apply_settings();
}


void serial_init()
{
  serial_uart_init();
  serial_cdc_init();
}


// --------------------------------------------------------------------------------------
// for XModem data upload and download
// --------------------------------------------------------------------------------------

#include "tusb.h"
#include "hardware/uart.h"
#include "keyboard.h"


int __in_flash(".configfun")  serial_xmodem_receive_char(int msDelay)
{ 
  absolute_time_t endtime = make_timeout_time_ms(msDelay);
  while( !time_reached(endtime) )
    { 
      if( tuh_inited() ) tuh_task();
      if( tud_inited() ) tud_task();
      keyboard_task();
      if( keyboard_read_keypress()==HID_KEY_ESCAPE ) return -2;

      if( tud_cdc_connected() )
        {
          if( tud_cdc_available() ) { char c; tud_cdc_read(&c, 1); return c; }
        }
      else
        {
          if( uart_is_readable(PIN_UART_ID) ) return uart_getc(PIN_UART_ID);
        }
    }
  
  return -1; 
}


void __in_flash(".configfun")  serial_xmodem_send_data(const char *data, int size)
{
  if( tud_cdc_connected() ) 
    {
      while( size>0 )
        {
          if( tud_cdc_write_available() )
            {
              uint32_t n = tud_cdc_write(data, size);
              size -= n;
              data += n;
            }
          tud_task();
        }

      tud_cdc_write_flush();
    }
  else
    uart_write_blocking(PIN_UART_ID, (uint8_t *) data, size);
}
