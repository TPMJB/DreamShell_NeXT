/* DreamShell NeXT maintenance app exports. */
#include "ds.h"

void BiosFlasher_Init(App_t *app);
void BiosFlasher_Open(App_t *app);
void BiosFlasher_Close(App_t *app);
void BiosFlasher_Shutdown(App_t *app);
void BiosFlasher_Row(GUI_Widget *widget);
void BiosFlasher_Action(GUI_Widget *widget);
void BiosFlasher_Back(GUI_Widget *widget);
void BiosFlasher_Confirm(GUI_Widget *widget);
void BiosFlasher_Cancel(GUI_Widget *widget);
void BiosFlasher_Item(dirent_fm_t *entry);
void BiosFlasher_Up(GUI_Widget *widget);
void BiosFlasher_Devices(GUI_Widget *widget);
void BiosFlasher_Folder(GUI_Widget *widget);
