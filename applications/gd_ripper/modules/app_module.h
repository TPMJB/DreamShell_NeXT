/* DreamShell GD Ripper app module header
   Copyright (C)2014 megavolt85
*/
#ifndef GD_RIPPER_APP_MODULE_H
#define GD_RIPPER_APP_MODULE_H
#include "ds.h"
void gd_ripper_Init(App_t *app, const char *fileName);
void gd_ripper_Exit(void);
void gd_ripper_Open(void);
void gd_ripper_Close(void);
void gd_ripper_Gamename(void);
void gd_ripper_Number_read(void);
void gd_ripper_ipbin_name(void);
void gd_ripper_StartRip(GUI_Widget *widget);
void gd_ripper_Verify(GUI_Widget *widget);
void gd_ripper_Recover(GUI_Widget *widget);
void gd_ripper_CancelRip(GUI_Widget *widget);
void gd_ripper_Quit(GUI_Widget *widget);
void gd_ripper_Advanced(GUI_Widget *widget);
void gd_ripper_Toggle(GUI_Widget *widget);
void gd_ripper_Destination(GUI_Widget *widget);
void gd_ripper_ShowFileBrowser(GUI_Widget *widget);
void gd_ripper_ShowMainPage(GUI_Widget *widget);
void gd_ripper_FileBrowserConfirm(GUI_Widget *widget);
#endif
