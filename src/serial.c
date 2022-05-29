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
