typedef struct _HotkeysHandler HotkeysHandler;
HotkeysHandler *init_global_hotkeys(XfceTaskBar *taskbar);
void finish_global_hotkeys(HotkeysHandler *handler);
