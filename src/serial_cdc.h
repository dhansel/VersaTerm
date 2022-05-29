#ifndef SERIAL_CDC_H
#define SERIAL_CDC_H

#include <stdbool.h>

bool serial_cdc_is_connected();
void serial_cdc_set_break(bool set);
void serial_cdc_send_char(char c);
void serial_cdc_send_string(const char *c);
bool serial_cdc_readable();

void serial_cdc_task(bool processInput);
void serial_cdc_apply_settings();
void serial_cdc_init();


#endif
