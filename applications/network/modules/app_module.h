/* DreamShell NeXT Network exports. */
#include "ds.h"

void NetworkApp_Init(App_t *app);
void NetworkApp_Open(App_t *app);
void NetworkApp_Close(App_t *app);
void NetworkApp_Shutdown(App_t *app);
void NetworkApp_Row(GUI_Widget *widget);
void NetworkApp_Action(GUI_Widget *widget);
void NetworkApp_Back(GUI_Widget *widget);
void NetworkApp_Confirm(GUI_Widget *widget);
void NetworkApp_Cancel(GUI_Widget *widget);
void NetworkApp_Item(dirent_fm_t *entry);
void NetworkApp_Up(GUI_Widget *widget);
void NetworkApp_Devices(GUI_Widget *widget);
void NetworkApp_Folder(GUI_Widget *widget);
