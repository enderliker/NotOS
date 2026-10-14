#ifndef NOTOS_LINE_INPUT_H
#define NOTOS_LINE_INPUT_H
#include "line_core.h"
/* Event-loop only: renderer, serial CSI parser and VFS completion. */
void line_input_char(char character);
void line_input_key(enum line_key key);
void line_input_clear(void);
void line_input_set_command(void (*command)(const char *));
#endif
