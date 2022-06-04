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
