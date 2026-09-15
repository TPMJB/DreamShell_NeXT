/* DreamShell NeXT maintenance app exports. */
#include "ds.h"

void Speedtest_Init(App_t *app);
void Speedtest_Open(App_t *app);
void Speedtest_Close(App_t *app);
void Speedtest_Shutdown(App_t *app);
void Speedtest_Row(GUI_Widget *widget);
void Speedtest_Action(GUI_Widget *widget);
void Speedtest_Back(GUI_Widget *widget);
void Speedtest_Confirm(GUI_Widget *widget);
void Speedtest_Cancel(GUI_Widget *widget);
void Speedtest_Item(dirent_fm_t *entry);
void Speedtest_Up(GUI_Widget *widget);
void Speedtest_Devices(GUI_Widget *widget);
void Speedtest_Folder(GUI_Widget *widget);
