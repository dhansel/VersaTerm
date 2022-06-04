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
