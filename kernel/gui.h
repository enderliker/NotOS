#ifndef NOTOS_GUI_H
#define NOTOS_GUI_H
/* UI events are interpreted by the kernel event loop, never in IRQ context. */
enum gui_action { GUI_NONE, GUI_CLEAR, GUI_FILES, GUI_TASKS };
void gui_pointer(int x, int y, unsigned buttons);
enum gui_action gui_next_action(void);
#endif
