#ifndef SERIAL_H
#define SERIAL_H

#include <stdbool.h>

void serial_set_break(bool set);
void serial_send_char(char c);
void serial_send_string(const char *s);
bool serial_readable();

int  serial_xmodem_receive_char(int msDelay);
void serial_xmodem_send_data(const char *data, int size);

void serial_task(bool processInput);
void serial_apply_settings();
void serial_init();

#endif
