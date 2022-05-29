#ifndef SERIAL_UART
#define SERIAL_UART

void serial_uart_set_break(bool set);
void serial_uart_send_char(char c);
void serial_uart_send_string(const char *s);
bool serial_uart_readable();
int  serial_uart_can_send();

void serial_uart_task(bool processInput);
void serial_uart_apply_settings();
void serial_uart_init();

#endif
