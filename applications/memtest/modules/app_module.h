/* DreamShell NeXT maintenance app exports. */
#include "ds.h"

void Memtest_Init(App_t *app);
void Memtest_Open(App_t *app);
void Memtest_Close(App_t *app);
void Memtest_Shutdown(App_t *app);
void Memtest_Row(GUI_Widget *widget);
void Memtest_Action(GUI_Widget *widget);
void Memtest_Back(GUI_Widget *widget);
void Memtest_Confirm(GUI_Widget *widget);
void Memtest_Cancel(GUI_Widget *widget);
void Memtest_Item(dirent_fm_t *entry);
void Memtest_Up(GUI_Widget *widget);
void Memtest_Devices(GUI_Widget *widget);
void Memtest_Folder(GUI_Widget *widget);
