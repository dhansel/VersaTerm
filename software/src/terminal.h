#ifndef TERMINAL_H
#define TERMINAL_H

void terminal_receive_char(char c);
void terminal_receive_string(const char* str);
void terminal_process_key(uint16_t key);

void terminal_clear_screen();
void terminal_init();
void terminal_apply_settings();

#endif
