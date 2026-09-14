/* DreamShell NeXT maintenance app exports. */
#include "ds.h"

void RegionChanger_Init(App_t *app);
void RegionChanger_Open(App_t *app);
void RegionChanger_Close(App_t *app);
void RegionChanger_Shutdown(App_t *app);
void RegionChanger_Row(GUI_Widget *widget);
void RegionChanger_Action(GUI_Widget *widget);
void RegionChanger_Back(GUI_Widget *widget);
void RegionChanger_Confirm(GUI_Widget *widget);
void RegionChanger_Cancel(GUI_Widget *widget);
void RegionChanger_Item(dirent_fm_t *entry);
void RegionChanger_Up(GUI_Widget *widget);
void RegionChanger_Devices(GUI_Widget *widget);
void RegionChanger_Folder(GUI_Widget *widget);
