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

#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "hardware/uart.h"
#include "hardware/clocks.h"

#include "serial_uart.h"
#include "serial_cdc.h"
#include "config.h"
#include "pins.h"
#include "terminal.h"
#include "config.h"

#define XON  17
#define XOFF 19

// extended TX FIFO (512 bytes) f that is not affected by disabling the UART fifos
static queue_t uart_tx_queue;

// RX FIFO used for Xon/Xoff flow control
static queue_t uart_rx_queue;

// timeout when to turn off blink LED
static absolute_time_t offtime = 0;


static void blink_led(uint16_t ms)
{
  if( ms>0 )
    {
      gpio_put(PIN_LED, true);
      offtime = make_timeout_time_ms(ms);
    }
}


void serial_uart_set_break(bool set)
{
  uart_set_break(PIN_UART_ID, set);
}


int serial_uart_can_send()
{
  return 512-queue_get_level(&uart_tx_queue);
}


void serial_uart_send_char(char c)
{
  if( uart_is_writable(PIN_UART_ID) && queue_is_empty(&uart_tx_queue) )
    {
      blink_led(config_get_serial_blink());
      uart_get_hw(PIN_UART_ID)->dr = c;
    }
  else
    queue_try_add(&uart_tx_queue, (uint8_t *) &c);
}


void serial_uart_send_string(const char *s)
{
  while( *s ) serial_uart_send_char(*s++);
}


bool serial_uart_receive_char(uint8_t *b)
{
  bool res = false;

  if( config_get_serial_xonxoff() )
    {
      // if xon/xoff is enabled then we maintain our own RX queue
      // so we can react faster to XON/XOFF requests.
      res = queue_try_remove(&uart_rx_queue, b);
    }
  else
    {
      // if xon/xoff is disabled then we use the built-in 
      // UART RX buffer for better performance
      if( uart_is_readable(PIN_UART_ID) ) 
        {
          blink_led(config_get_serial_blink());
          *b = uart_getc(PIN_UART_ID);
          res = true;
        }
    }

  return res;
}


bool serial_uart_readable()
{
  return config_get_serial_xonxoff() ? !queue_is_empty(&uart_rx_queue) : uart_is_readable(PIN_UART_ID);
}


void serial_uart_apply_settings()
{
  uart_parity_t parity = UART_PARITY_NONE;
  switch( config_get_serial_parity() )
    {
    case 'N': parity = UART_PARITY_NONE; break;
    case 'E': parity = UART_PARITY_EVEN; break;
    case 'O': parity = UART_PARITY_ODD;  break;
    case 'S': parity = UART_PARITY_EVEN; break; // together with "stick parity" bit below
    case 'M': parity = UART_PARITY_ODD;  break; // together with "stick parity" bit below
    }

  // set serial baud rate and format
  uart_set_baudrate(PIN_UART_ID, config_get_serial_baud());
  uart_set_format(PIN_UART_ID, config_get_serial_bits(), config_get_serial_stopbits(), parity);

  // set "stick parity" bit for mark/space parity
  hw_write_masked(&uart_get_hw(PIN_UART_ID)->lcr_h,
                  (config_get_serial_parity()=='M' || config_get_serial_parity()=='S') ? (1 << UART_UARTLCR_H_SPS_LSB) : 0,
                  UART_UARTLCR_H_SPS_BITS);

  // hardware CTS/RTS flow control
  switch( config_get_serial_rtsmode() )
    {
    case 0:
    case 1:
      gpio_set_function(PIN_UART_RTS, GPIO_FUNC_NULL);
      gpio_init(PIN_UART_RTS);
      gpio_set_dir(PIN_UART_RTS, true); // output
      gpio_put(PIN_UART_RTS, config_get_serial_rtsmode()==1 );
      break;

    case 2:
      gpio_set_function(PIN_UART_RTS, GPIO_FUNC_UART);
      break;
    }

  switch( config_get_serial_ctsmode() )
    {
    case 0: 
      gpio_set_function(PIN_UART_CTS, GPIO_FUNC_NULL);
      gpio_init(PIN_UART_CTS);
      gpio_set_dir(PIN_UART_CTS, false); // input (high-z)
      break;
      
    case 1:
      gpio_set_function(PIN_UART_CTS, GPIO_FUNC_UART);
      break;
    }

  uart_set_hw_flow(PIN_UART_ID, config_get_serial_ctsmode(), config_get_serial_rtsmode());

  // make sure transmitter is always enabled if Xon/Xoff flow control is disabled
  if( config_get_serial_xonxoff()==0 )
    hw_set_bits(&uart_get_hw(PIN_UART_ID)->cr, UART_UARTCR_TXE_BITS);

  // disable FIFOs when using XOn/XOff flow control (so we can react faster)
  hw_write_masked(&uart_get_hw(PIN_UART_ID)->lcr_h,
                  config_get_serial_xonxoff()==2 ? 0 : (1 << UART_UARTLCR_H_FEN_LSB),
                  UART_UARTLCR_H_FEN_BITS);
}


void serial_uart_task(bool processInput)
{
  static bool isxon = true;
  uint8_t b;
  
  // send serial output if we have some buffered
  if( !queue_is_empty(&uart_tx_queue) && uart_is_writable(PIN_UART_ID) )
    {
      if( queue_try_remove(&uart_tx_queue, &b) ) 
        {
          blink_led(config_get_serial_blink());
          uart_get_hw(PIN_UART_ID)->dr = b;
        }
    }

  // handle XON/XOFF flow control
  if( config_get_serial_xonxoff()>0 )
    {
      // if xon/xoff is enabled then we maintain our own RX queue
      // so we can react faster to XON/XOFF requests.
      if( uart_is_readable(PIN_UART_ID) )
        {
          blink_led(config_get_serial_blink());

          b = uart_getc(PIN_UART_ID);
          if( b==XON || b==XOFF )
            {
              // disable UART transmitter when receiving XOff / enable transmitter when receiving XOn
              hw_write_masked(&uart_get_hw(PIN_UART_ID)->cr, (b==XON) ? (1 << UART_UARTCR_TXE_LSB) : 0, UART_UARTCR_TXE_BITS);
            }
          else
            {
              queue_try_add(&uart_rx_queue, &b);

              // send XOFF if our receive queue is almost full
              if( queue_get_level(&uart_rx_queue)>20 && isxon )
                { uart_get_hw(PIN_UART_ID)->dr = XOFF; isxon = false; }
            }
        }
      else if( queue_get_level(&uart_rx_queue)<12 && !isxon )
        {
          // send XON if our receive queue is emptying again
          uart_get_hw(PIN_UART_ID)->dr = XON;
          isxon = true;
        }
    }

  // handle LED flashing
  if( offtime>0 && get_absolute_time() >= offtime )
    { offtime = 0; gpio_put(PIN_LED, false); }

  // handle serial input
  if( processInput && serial_uart_receive_char(&b) )
    {
      switch( config_get_usb_cdcmode() )
        {
        case 0: // disabled
        case 1: // regular serial
          terminal_receive_char(b);
          break;

        case 2: // pass-through
          terminal_receive_char(b);
          serial_cdc_send_char(b);
          break;

        case 3: // pass-through (terminal disabled)
          serial_cdc_send_char(b);
          break;
        }
    }
}


void serial_uart_init()
{
  offtime = 0;
  gpio_init(PIN_LED);
  gpio_set_dir(PIN_LED, true); // output
  blink_led(1000);

  queue_init(&uart_tx_queue, 1, 512);
  queue_init(&uart_rx_queue, 1, 32);
  serial_uart_apply_settings();
}
