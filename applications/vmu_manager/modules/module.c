/* DreamShell ##version##

   module.c - VMU Manager app module
   Copyright (C)2014-2015 megavolt85
   Copyright (C)2024-2026 SWAT

*/

#include "ds.h"
#include "fs_vmd.h"
#include "app_module.h"
#include "ui_logic.h"
#include <errno.h>
#include "transfer.h"
#include "bulk.h"

static void ui_init(void);
static void reset_selected(void);
static void ui_status(const char *text);
static int ui_confirm(void);
static void ui_allow_image(bool enabled);
#include <stdbool.h>
#include <dc/maple/mouse.h>

DEFAULT_MODULE_EXPORTS(app_vmu_manager);

#define MAPLE_FUNC_GUN     0x81000000
#define PACK_NYBBLE_RGB565(nybble) ((((nybble & 0x0f00)>>8)*2)<<11) + \
	((((nybble & 0x00f0)>>4)*4)<<5) + ((((nybble & 0x000f)>>0)*2)<<0)

#define VMU_ICON_WIDTH  32
#define VMU_ICON_HEIGHT 32
#define VMU_ICON_SIZE   (VMU_ICON_WIDTH * VMU_ICON_HEIGHT * 2)
#define LEFT_FM  0
#define RIGHT_FM  1

typedef enum {
	DS_VMS = 0,
	DS_VMD,
	DS_VMI,
	DS_DCI
} save_type_t;

typedef struct vm_root {
	uint16_t size;
	uint16_t partition;
	uint16_t sys_block;
	uint16_t fat_block;
	uint16_t fat_cnt;
	uint16_t file_info_block;
	uint16_t file_info_cnt;
	uint8_t  vol_icon;
	uint8_t  reserved;
	uint16_t extra_block;
	uint16_t extra_cnt;
	uint16_t exe_block;
	uint16_t exe_cnt;
} vm_root_t;

void VMU_Manager_ItemContextClick(dirent_fm_t *fm_ent);
void VMU_Manager_ItemSelect(dirent_fm_t *fm_ent);
int VMU_Manager_Dump(GUI_Widget *widget);
void VMU_Manager_addfileman(GUI_Widget *widget);

static struct {
	App_t* m_App;
	GUI_Widget *pages;
	GUI_Widget *button_dump;
	GUI_Widget *button_home;
	GUI_Widget *cd_c;
	GUI_Widget *sd_c;
	GUI_Widget *hdd_c;
	GUI_Widget *pc_c;
	GUI_Widget *format_c;
	GUI_Widget *dst_vmu;
	GUI_Widget *vmu[4][2];
	GUI_Widget *img_cont[4];
	GUI_Widget *name_device;
	GUI_Widget *free_mem;
	GUI_Widget *filebrowser;
	GUI_Widget *filebrowser2;
	GUI_Widget *sicon;
	GUI_Widget *save_name;
	GUI_Widget *save_size;
	GUI_Widget *save_descshort;
	GUI_Widget *save_desclong;
	GUI_Widget *progressbar_container;
	GUI_Widget *progressbar;
	GUI_Widget *folder_name;
	GUI_Surface *progres_img;
	GUI_Surface *progres_img_b;
	GUI_Surface *confirmimg[3];
	GUI_Surface *m_ItemNormal;
	GUI_Surface *m_ItemSelected;
	GUI_Surface *m_ItemNormal2;
	GUI_Surface *m_ItemSelected2;
	GUI_Surface *logo;
	GUI_Surface *dump_icon;
	GUI_Surface *vmuicon;
	SDL_Surface *vmu_icon;

	GUI_Surface *controller;
	GUI_Surface *treamcast;
	GUI_Surface *arcade;
	GUI_Surface *asciipad;
	GUI_Surface *whell;
	GUI_Surface *fishrod;
	GUI_Surface *twin;
	GUI_Surface *maracas;
	GUI_Surface *popnmusic;
	GUI_Surface *missionstick;
	GUI_Surface *panther;
	GUI_Surface *densha;
	GUI_Surface *x360;
	GUI_Surface *psx;
	GUI_Surface *lightgun;
	GUI_Surface *lightgunus;
	GUI_Surface *treamcastgun;
	GUI_Surface *keyboard;
	GUI_Surface *keyboardjp;
	GUI_Surface *mouse;
	GUI_Surface *dreameye;
	GUI_Surface *mic;
	GUI_Surface *dreameyemic;
	GUI_Surface *vibro_pack;
	GUI_Surface *vmu_d;

	GUI_Widget *vmu_page;
	GUI_Widget *vmu_container;

	GUI_Widget *confirm;
	GUI_Widget *image_confirm;
	GUI_Widget *confirm_text;
	GUI_Widget *drection;

	bool have_args;

	int vmu_freeblock;
	int vmu_freeblock2;
	int direction_flag;
	char* home_path;
	char* m_SelectedFile;
	char* m_SelectedPath;
	char desc_short[17];
	char desc_long[33];
	GUI_Surface *last_port_imgs[4];
} self;

static struct {
	uint32 checksum;
	char title[32];
	char creator[32];
	char date[8];
	char version[2];
	char numfiles[2];
	char source[8];
	char name[12];
	uint16 type;
	char reserved[2];
	uint32 size;
} vmi_t;

static struct {
	char type[1];
	uint8 copyprotect;
	uint16 firstblock;
	char name[12];
	uint16 year;
	uint8 month;
	uint8 day;
	uint8 hours;
	uint8 mins;
	uint8 secs;
	char weekday[1];
	uint16 size; // size in blocks
	uint16 headeroffset;
	uint32 unused;
	uint8 vmsheader[1024];
} dci_t;

static void sanitize_vmu_string(char *str, int size) {
	for(int i = 0; i < size; i++) {
		if(str[i] == 0) break;
		if((uint8)str[i] < 0x20 && str[i] != '\n' && str[i] != '\r') {
			str[i] = ' ';
		}
	}
	str[size-1] = 0;
}

static void* vmu_dev(const char* path) {

	if (!path || strlen(path) < 7 || strncmp(path, "/vmu/", 5) || (path[7] && path[7] != '/')) return NULL;
	int port = path[5] - 'A';
	int slot = path[6] - '0';

	if (port < 0 || port > 3 || slot < 1 || slot > 2) {
		return NULL;
	}

	maple_device_t *dev = maple_enum_dev(port, slot);
	return dev && (dev->info.functions & MAPLE_FUNC_MEMCARD) ? dev : NULL;
}

static int rmdir_recursive(const char* folder) {

	file_t d;
	const dirent_t *de;
	char dst[NAME_MAX];

	d = fs_open(folder, O_DIR);
    if(d == FILEHND_INVALID) return CMD_ERROR;
    int result = CMD_OK;

	while ((de = fs_readdir(d))) {
		if (strcmp(de->name ,".") == 0 || strcmp(de->name ,"..") == 0) {
			continue;
		}

        if(snprintf(dst, sizeof(dst), "%s/%s", folder, de->name) >= (int)sizeof(dst)) {
            result=CMD_ERROR; break;
        }

		if (de->attr == O_DIR) {
			if(rmdir_recursive(dst)!=CMD_OK) { result=CMD_ERROR; break; }
		}
		else {
			if(fs_unlink(dst)) { result=CMD_ERROR; break; }
		}
	}

    if(fs_close(d)) result=CMD_ERROR;
    if(result==CMD_OK && fs_rmdir(folder)) result=CMD_ERROR;
    return result;
}

static void free_blocks(const char *path , int n) {
	maple_device_t *vmucur = vmu_dev(path);

	if(vmucur == NULL) {
        if(n == 0) self.vmu_freeblock = -1; else self.vmu_freeblock2 = -1;
		return;
	}

	if(n == 0) {
		self.vmu_freeblock = vmufs_free_blocks(vmucur);
	}
	else {
		self.vmu_freeblock2 = vmufs_free_blocks(vmucur);
	}
}

static void addbutton() {
	GUI_WidgetSetEnabled(self.button_dump, 0);
	GUI_ContainerRemove(self.vmu_page, self.filebrowser2);

	GUI_WidgetSetEnabled(self.pc_c, DirExists("/pc"));
	GUI_WidgetSetEnabled(self.sd_c, DirExists("/sd"));
	GUI_WidgetSetEnabled(self.hdd_c, DirExists("/ide"));
	GUI_WidgetSetEnabled(self.cd_c, DirExists("/cd"));

	self.direction_flag = 0;
    self.home_path = NULL;
    reset_selected();

	GUI_ContainerAdd(self.vmu_page, self.sd_c);
	GUI_ContainerAdd(self.vmu_page, self.hdd_c);
	GUI_ContainerAdd(self.vmu_page, self.cd_c);
	GUI_ContainerAdd(self.vmu_page, self.pc_c);
	GUI_ContainerAdd(self.vmu_page, self.dst_vmu);
	/* Format lives on the More actions page. */
	GUI_WidgetMarkChanged(self.m_App->body);
}

static void disable_high(int fm)
{
	int i;
	GUI_Widget *panel, *w;

	if(fm == RIGHT_FM) {
		panel = GUI_FileManagerGetItemPanel(self.filebrowser2);

		for(i = 0; i < GUI_ContainerGetCount(panel); i++) {
			w = GUI_FileManagerGetItem(self.filebrowser2, i);
			GUI_ButtonSetNormalImage(w, self.m_ItemNormal2);
			GUI_ButtonSetHighlightImage(w, self.m_ItemNormal2);
		}
	}
	else {
		panel = GUI_FileManagerGetItemPanel(self.filebrowser);

		for(i = 0; i < GUI_ContainerGetCount(panel); i++) {
			w = GUI_FileManagerGetItem(self.filebrowser, i);
			GUI_ButtonSetNormalImage(w, self.m_ItemNormal);
			GUI_ButtonSetHighlightImage(w, self.m_ItemNormal);
		}
	}
}

static void clr_statusbar() {
	GUI_PictureSetImage(self.sicon, self.logo);
	GUI_LabelSetText(self.save_name, "   ");
	GUI_LabelSetText(self.save_size, "   ");
	GUI_LabelSetText(self.save_descshort, "   ");
	GUI_LabelSetText(self.save_desclong, "   ");
}

static int Confirm_Window(void) { return ui_confirm(); }

static void* GetElement(const char *name, ListItemType type, int from)
{
	Item_t *item;
	item = listGetItemByName(from ? self.m_App->elements : self.m_App->resources, name);

	if(item != NULL && item->type == type) {
		return item->data;
	}

	dbgio_printf("Resource not found: %s\n", name);
	return NULL;
}

void Vmu_Manager_Init(App_t* app)
{
	self.m_App = app;

	if (self.m_App == NULL) {
		ds_printf("DS_ERROR: Can't find app named: %s\n", "Vmu Manager");
		return;
	}

	int x,y;
	self.direction_flag = 0;
	self.pages	= (GUI_Widget *) GetElement("pages", LIST_ITEM_GUI_WIDGET, 1);
	self.button_home = (GUI_Widget *) GetElement("home_but", LIST_ITEM_GUI_WIDGET, 1);
	self.cd_c  = (GUI_Widget *) GetElement("/cd", LIST_ITEM_GUI_WIDGET, 1);
	self.sd_c  = (GUI_Widget *) GetElement("/sd", LIST_ITEM_GUI_WIDGET, 1);
	self.hdd_c = (GUI_Widget *) GetElement("/ide", LIST_ITEM_GUI_WIDGET, 1);
	self.pc_c  = (GUI_Widget *) GetElement("/pc", LIST_ITEM_GUI_WIDGET, 1);
	self.format_c  = (GUI_Widget *) GetElement("format-c", LIST_ITEM_GUI_WIDGET, 1);
	self.dst_vmu  = (GUI_Widget *) GetElement("dst-vmu", LIST_ITEM_GUI_WIDGET, 1);
	self.vmu_container  = (GUI_Widget *) GetElement("vmu-container", LIST_ITEM_GUI_WIDGET, 1);
	self.folder_name  = (GUI_Widget *) GetElement("folder-name", LIST_ITEM_GUI_WIDGET, 1);

	self.vmu[0][0]  = (GUI_Widget *) GetElement("A1", LIST_ITEM_GUI_WIDGET, 1);
	self.vmu[0][1]  = (GUI_Widget *) GetElement("A2", LIST_ITEM_GUI_WIDGET, 1);
	self.vmu[1][0]  = (GUI_Widget *) GetElement("B1", LIST_ITEM_GUI_WIDGET, 1);
	self.vmu[1][1]  = (GUI_Widget *) GetElement("B2", LIST_ITEM_GUI_WIDGET, 1);
	self.vmu[2][0]  = (GUI_Widget *) GetElement("C1", LIST_ITEM_GUI_WIDGET, 1);
	self.vmu[2][1]  = (GUI_Widget *) GetElement("C2", LIST_ITEM_GUI_WIDGET, 1);
	self.vmu[3][0]  = (GUI_Widget *) GetElement("D1", LIST_ITEM_GUI_WIDGET, 1);
	self.vmu[3][1]  = (GUI_Widget *) GetElement("D2", LIST_ITEM_GUI_WIDGET, 1);

	self.save_name = (GUI_Widget *) GetElement("save-name", LIST_ITEM_GUI_WIDGET, 1);
	self.save_size = (GUI_Widget *) GetElement("save-size", LIST_ITEM_GUI_WIDGET, 1);
	self.save_descshort = (GUI_Widget *) GetElement("desc-short", LIST_ITEM_GUI_WIDGET, 1);
	self.save_desclong = (GUI_Widget *) GetElement("desc-long", LIST_ITEM_GUI_WIDGET, 1);

	self.name_device = (GUI_Widget *) GetElement("name-device", LIST_ITEM_GUI_WIDGET, 1);
	self.free_mem = (GUI_Widget *) GetElement("free-mem", LIST_ITEM_GUI_WIDGET, 1);

	self.sicon = (GUI_Widget *) GetElement("vmu-icon", LIST_ITEM_GUI_WIDGET, 1);
	self.button_dump = (GUI_Widget *) GetElement("dump-button", LIST_ITEM_GUI_WIDGET, 1);

	self.filebrowser = (GUI_Widget *) GetElement("file_browser", LIST_ITEM_GUI_WIDGET, 1);
	self.filebrowser2 = (GUI_Widget *) GetElement("file_browser2", LIST_ITEM_GUI_WIDGET, 1);
	self.m_ItemNormal = (GUI_Surface *) GetElement("item-normal", LIST_ITEM_GUI_SURFACE, 0);
	self.m_ItemSelected	= (GUI_Surface *) GetElement("item-selected", LIST_ITEM_GUI_SURFACE, 0);
	self.m_ItemNormal2 = (GUI_Surface *) GetElement("item-normal2", LIST_ITEM_GUI_SURFACE, 0);
	self.m_ItemSelected2	= (GUI_Surface *) GetElement("item-selected2", LIST_ITEM_GUI_SURFACE, 0);
	self.logo = (GUI_Surface *) GetElement("logo", LIST_ITEM_GUI_SURFACE, 0);
	self.dump_icon = (GUI_Surface *) GetElement("dump_icon", LIST_ITEM_GUI_SURFACE, 0);
	self.progres_img = (GUI_Surface *) GetElement("progressbar", LIST_ITEM_GUI_SURFACE, 0);
	self.progres_img_b = (GUI_Surface *) GetElement("progressbar_back", LIST_ITEM_GUI_SURFACE, 0);

	self.confirmimg[0] = (GUI_Surface *) GetElement("confirmimg", LIST_ITEM_GUI_SURFACE, 0);
	self.confirmimg[1] = (GUI_Surface *) GetElement("confirmimg0", LIST_ITEM_GUI_SURFACE, 0);
	self.image_confirm = (GUI_Widget *) GetElement("image-confirm", LIST_ITEM_GUI_WIDGET, 1);
	self.confirm = (GUI_Widget *) GetElement("confirm", LIST_ITEM_GUI_WIDGET, 1);
	self.confirm_text = (GUI_Widget *) GetElement("confirm-text", LIST_ITEM_GUI_WIDGET, 1);

	self.drection = (GUI_Widget *) GetElement("drection", LIST_ITEM_GUI_WIDGET, 1);

	self.progressbar = (GUI_Widget *) GetElement("progressbar", LIST_ITEM_GUI_WIDGET, 1);
	self.progressbar_container = (GUI_Widget *) GetElement("progressbar_container", LIST_ITEM_GUI_WIDGET, 1);

	Item_t *i;
	i = listGetItemByName(self.m_App->elements, "vmu_page");
	self.vmu_page = (GUI_Widget*) i->data;

	GUI_FileManagerSetItemContextClick(self.filebrowser, (GUI_CallbackFunction*) VMU_Manager_BrowseSelect);
	GUI_FileManagerSetItemContextClick(self.filebrowser2, (GUI_CallbackFunction*) VMU_Manager_BrowseSelect);

	GUI_ContainerRemove(self.vmu_page, self.progressbar_container);
	GUI_ContainerRemove(self.vmu_page, self.filebrowser2);
	GUI_ContainerRemove(self.vmu_page, self.confirm);

	GUI_WidgetSetEnabled(self.pc_c, DirExists("/pc"));
	GUI_WidgetSetEnabled(self.sd_c, DirExists("/sd"));
	GUI_WidgetSetEnabled(self.hdd_c, DirExists("/ide"));
	GUI_WidgetSetEnabled(self.cd_c, DirExists("/cd"));

	GUI_WidgetSetEnabled(self.button_dump, 0);

	for(x = 0; x < 4; ++x)
	{
		for(y = 0; y < 2; ++y)
		{
			GUI_WidgetSetEnabled(self.vmu[x][y], 0);
		}
	}

	/* Disabling scrollbar for file browsers */
	GUI_FileManagerRemoveScrollbar(self.filebrowser);
	GUI_FileManagerRemoveScrollbar(self.filebrowser2);
    GUI_Widget *lists[2]={self.filebrowser,self.filebrowser2};
    SDL_Rect row={0,0,264,26};
    for(int list=0;list<2;++list) {
        GUI_Surface *normal=list?self.m_ItemNormal2:self.m_ItemNormal;
        GUI_Surface *selected=list?self.m_ItemSelected2:self.m_ItemSelected;
        GUI_Surface *focus=GetElement(list?"item-focus2":"item-focus",LIST_ITEM_GUI_SURFACE,0);
        GUI_FileManagerSetItemSize(lists[list],&row);
        GUI_WidgetSetSize(GUI_FileManagerGetItemPanel(lists[list]),264,182);
        GUI_FileManagerSetItemSurfaces(lists[list],normal,focus,focus,normal);
        GUI_FileManagerSetItemSelectedSurfaces(lists[list],selected,selected,selected,selected);
    }


	if (GUI_CardStackGetIndex(self.pages) == 0) {
		GUI_WidgetSetEnabled(self.button_home, 0);
	}

	self.m_SelectedFile = NULL;
	self.m_SelectedPath = NULL;
	self.home_path = NULL;
	fs_vmd_init();
    ui_init();

	if (app->args != 0)
	{
		/* TODO
		char *path = getFilePath(app->args);
		if (path) {
			GUI_FileManagerSetPath(self.filebrowser2, path);
			free(path);
		}
		*/
		self.have_args = true;
	}
	else {
		self.have_args = false;
	}
}

static void reset_selected() {
	if(self.m_SelectedFile != NULL) {
		free(self.m_SelectedFile);
		free(self.m_SelectedPath);
		self.m_SelectedFile = NULL;
		self.m_SelectedPath = NULL;
	}
}

void VMU_Manager_EnableMainPage() {
	int x, y;

	for(x = 0; x < 4; ++x)
	{
		for(y = 0; y < 2; ++y)
		{
			if(GUI_ContainerContains(self.vmu_container, self.vmu[x][y]) == 0){
				GUI_ContainerAdd(self.vmu_container,self.vmu[x][y]);
			}
		}
	}

	reset_selected();
	disable_high(RIGHT_FM);
	disable_high(LEFT_FM);
	clr_statusbar();
	self.direction_flag = 0;
	GUI_LabelSetText(self.drection, "CHOOSE A VMU");
	GUI_WidgetSetEnabled(self.button_home, 0);
	ScreenFadeOutEx(NULL, 1);
	GUI_CardStackShowIndex(self.pages, 0);
	ScreenFadeIn();
}

void VMU_Manager_vmu(GUI_Widget *widget) {
	char vpath[NAME_MAX];

	ScreenFadeOutEx(NULL, 1);
	GUI_CardStackShowIndex(self.pages, 1);
	GUI_WidgetSetEnabled(self.button_home, 1);

	snprintf(vpath, NAME_MAX, "/vmu/%s", GUI_ObjectGetName(widget));

	if (!vmu_dev(vpath)) { ui_status("That VMU is no longer connected."); GUI_CardStackShowIndex(self.pages, 0); ScreenFadeIn(); return; }
    free_blocks(vpath, self.direction_flag ? 1 : 0);

	if(self.direction_flag == 0) {
		GUI_FileManagerSetPath(self.filebrowser, vpath);
        GUI_FileManagerScan(self.filebrowser);
		/* Source stays visible; destination selection disables this same slot. */
		addbutton();
	}
	else {
		GUI_WidgetSetEnabled(self.button_dump, 0);
		VMU_Manager_addfileman(widget);
	}

	ScreenFadeIn();
}

void VMU_Manager_info_bar(GUI_Widget *widget) {
	char str[80], path[16];

	GUI_LabelSetText(self.name_device, GUI_ObjectGetName( (GUI_Object *)widget));
	snprintf(path, sizeof(path), "/vmu/%s", GUI_ObjectGetName((GUI_Object *) widget));

	if(self.direction_flag == 0) {
		free_blocks(path, 0);
		snprintf(str, sizeof(str), "%s %d %s", "free", self.vmu_freeblock, "blocks");
	}
	else {
		free_blocks(path, 1);
		snprintf(str, sizeof(str), "%s %d %s", "free", self.vmu_freeblock2, "blocks");
	}

	if(self.vmu_freeblock >= 0) {
		GUI_LabelSetText(self.free_mem, str);
	}
}

void VMU_Manager_info_bar_clr(GUI_Widget *widget) {
	GUI_LabelSetText(self.name_device, "");
	GUI_LabelSetText(self.free_mem, "");

#ifdef VMDEBUR
	dbgio_printf("onmouseout");
#endif
}

void VMU_Manager_Exit(GUI_Widget *widget) {
	(void)widget;
	App_t *app = NULL;

	/* The mounted image is closed by the unload lifecycle hook. */

	if(self.have_args == true) {
		app = GetAppByName("File Manager");

		if(!app || !(app->state & APP_STATE_LOADED)) {
			app = NULL;
		}
	}

	if(!app) {
		OpenMainApp();
	}
	else {
		OpenApp(app, NULL);
	}
}

static void copy_save(const char *src, const char *dst) {
    if(vmu_transfer_file(src,dst,false)==CMD_OK) ui_status("Save copied successfully.");
    else ui_status("Copy failed. Check the destination before trying again.");
}

static save_type_t VMU_GetSaveType(const char *name) {
	int len = strlen(name);
	if (len < 4) return DS_VMS;

	if (strcasecmp(name + len - 4, ".vmd") == 0 || strcasecmp(name + len - 4, ".vmu") == 0) {
		return DS_VMD;
	}
	if (strcasecmp(name + len - 4, ".vmi") == 0) {
		return DS_VMI;
	}
	if (strcasecmp(name + len - 4, ".dci") == 0) {
		return DS_DCI;
	}
	return DS_VMS;
}

void VMU_Manager_ItemClick(dirent_fm_t *fm_ent) {

	dirent_t *ent = &fm_ent->ent;
	file_t f;
	int xx, yy;
	int flag = CMD_ERROR;
	save_type_t flag_type = DS_VMS; // vms 0 ; vmd 1 ; vmi 2 ; dci 3
	static char src[NAME_MAX];
	static char dst[NAME_MAX];
	static char text[1024];
	GUI_Widget *fmw = (GUI_Widget*)fm_ent->obj;
	int i;

    if(strlen(GUI_FileManagerGetPath(fmw))+strlen(ent->name)+20>=NAME_MAX ||
       strlen(GUI_FileManagerGetPath(self.filebrowser2))+strlen(ent->name)+20>=NAME_MAX) {
        ui_status("This path is too long. Choose a folder nearer the device root."); return;
    }

	if(ent->attr == O_DIR) { // This is FOLDER
		if (strcmp(GUI_ObjectGetName(fmw), "file_browser") == 0 && fm_ent->index == 0) {
			GUI_ContainerContains(self.vmu_page, self.filebrowser2);
			reset_selected();
			disable_high(RIGHT_FM);
			disable_high(LEFT_FM);
			clr_statusbar();
			GUI_FileManagerScan(self.filebrowser);
			return;
		}

		if(fm_ent->index == 0 && (!self.home_path || strlen(self.home_path) >= strlen(GUI_FileManagerGetPath(self.filebrowser2)))) {
			reset_selected();
			self.home_path = NULL;
			clr_statusbar();
			addbutton();
			return;
		}

		disable_high(RIGHT_FM);
		disable_high(LEFT_FM);
		reset_selected();
		clr_statusbar();
		GUI_FileManagerChangeDir(fmw, ent->name, ent->size);
        GUI_FileManagerScan(fmw);
		return;
	}

	save_type_t type = VMU_GetSaveType(ent->name);

	if( self.m_SelectedFile && strcmp(self.m_SelectedFile,ent->name) == 0 &&
		strcmp(self.m_SelectedPath,GUI_FileManagerGetPath(fmw)) == 0) {		// file selected

		if((strcmp(GUI_ObjectGetName(fmw), "file_browser2") == 0 && type == DS_VMD) &&
						strcmp(self.m_SelectedFile, ent->name) == 0 &&
						strcmp(self.m_SelectedPath, GUI_FileManagerGetPath(fmw)) == 0) {	/* RESTORE DUMP*/

			snprintf(text, sizeof(text), "Restore image to VMU %s? All saves on that VMU will be replaced. Image: %.100s", GUI_FileManagerGetPath(self.filebrowser)+5, ent->name);
            GUI_LabelSetText(self.confirm_text, text);
            ui_allow_image(true);

			GUI_PictureSetImage(self.image_confirm, self.confirmimg[1]);
			flag = Confirm_Window();
            ui_allow_image(false);
			GUI_PictureSetImage(self.image_confirm, self.confirmimg[0]);

			sprintf(src,"%s/%s",GUI_FileManagerGetPath(fmw),ent->name);

			if (flag == CMD_OK && FileSize(src) == (2 << 16)) {
				if ( VMU_Manager_Dump(GUI_FileManagerGetItem(self.filebrowser2, fm_ent->index)) == CMD_OK) {


					free_blocks(GUI_FileManagerGetPath(self.filebrowser) , 0);
					GUI_FileManagerScan(self.filebrowser);
				}
			}
			else if (flag == CMD_NO_ARG) {
				self.home_path = "/vmd";

				fs_vmd_vmdfile(src);
				GUI_WidgetSetEnabled(self.button_dump, 0);
				GUI_FileManagerSetPath(self.filebrowser2, self.home_path);
				GUI_FileManagerScan(self.filebrowser2);
			}
            else if(flag==CMD_OK) ui_status("Full restore requires a 128 KiB VMU image.");

			reset_selected();
			clr_statusbar();
			return;
		}
		else if(strcmp(self.m_SelectedFile,ent->name) == 0 &&
				strcmp(self.m_SelectedPath,GUI_FileManagerGetPath(fmw)) == 0 &&
				GUI_ContainerContains(self.vmu_page, self.filebrowser2) == 1) {	// copy file

			if(strcmp(GUI_ObjectGetName(fmw), "file_browser2") == 0) {		// copy file to vmu
                free_blocks(GUI_FileManagerGetPath(self.filebrowser), 0);
                if (self.vmu_freeblock < 0) { ui_status("The destination VMU is unavailable."); return; }

				if (type == DS_VMI) {

					if((vmi_t.size+511)/512 > (uint32_t)self.vmu_freeblock) {
                        ui_status("Not enough free blocks on the destination VMU.");
						return;
					}

					sprintf(src, "%s/%1.8s.vms", GUI_FileManagerGetPath(fmw), vmi_t.source);
					if(!FileExists(src)) sprintf(src, "%s/%1.8s.VMS", GUI_FileManagerGetPath(fmw), vmi_t.source);

					if(!FileExists(src)) {
						reset_selected();
						disable_high(RIGHT_FM);
						clr_statusbar();
						return;
					}

					sprintf(dst, "%s/%12.12s", GUI_FileManagerGetPath(self.filebrowser), vmi_t.name);
				}
				else if (type == DS_DCI) {

					if(ent->size < 32 || ((ent->size-32+511)/512) > self.vmu_freeblock) {
                        ui_status("DCI is invalid or the destination VMU is full.");
						return;
					}

					sprintf(src, "%s/%s", GUI_FileManagerGetPath(fmw), ent->name);
					sprintf(dst, "%s/%12.12s", GUI_FileManagerGetPath(self.filebrowser), dci_t.name);
					flag_type = DS_DCI;
				}
				else {
					if((ent->size+511)/512 > self.vmu_freeblock) {
                        ui_status("Not enough free blocks on the destination VMU.");
						return;
					}

					sprintf(src, "%s/%s", GUI_FileManagerGetPath(fmw), ent->name);
                    char restored_name[13];
                    vmu_ui_import_name(restored_name,ent->name,!strncmp(GUI_FileManagerGetPath(fmw),"/vm",3));
                    if(!*restored_name) { ui_status("This save has no valid VMU filename."); return; }
					sprintf(dst, "%s/%s", GUI_FileManagerGetPath(self.filebrowser), restored_name);
				}

				if (FileExists(dst) != 0) {

					sprintf(text, "Overwrite %s", dst);
					GUI_LabelSetText(self.confirm_text, text);
					flag = Confirm_Window();
				}
				else {
					flag = CMD_OK;
				}

				if (flag == CMD_OK) {
					GUI_ProgressBarSetImage1(self.progressbar, self.progres_img_b);
					GUI_ProgressBarSetImage2(self.progressbar, self.progres_img);
					GUI_ProgressBarSetPosition(self.progressbar, 1.0);
					GUI_ContainerAdd(self.vmu_page, self.progressbar_container);
					GUI_WidgetMarkChanged(self.vmu_page);
					LockVideo();
#ifdef VMDEBUG
					dbgio_printf("src: %s\ndst: %s\n", src, dst);
					thd_sleep(2000);
#else
					if(flag_type == DS_DCI) {
                        if(vmu_transfer_file(src,dst,true)==CMD_OK) ui_status("DCI save imported successfully.");
                        else ui_status("DCI import failed. Check the source and destination.");
					}
					else {
						copy_save(src, dst);
					}
#endif
					free_blocks(GUI_FileManagerGetPath(self.filebrowser),0);
					GUI_FileManagerScan(self.filebrowser);
				}
			}
			else if(!vmu_ui_read_only(GUI_FileManagerGetPath(self.filebrowser2))) { // copy file from vmu
				sprintf(src, "%s/%s", GUI_FileManagerGetPath(fmw), ent->name);

				if(strncmp(GUI_FileManagerGetPath(self.filebrowser2), "/vmu",4) == 0) {
					free_blocks(GUI_FileManagerGetPath(self.filebrowser2), 1);
                    if((ent->size+511)/512 > self.vmu_freeblock2) { ui_status("Not enough free blocks on the destination VMU."); return; }

					sprintf(dst, "%s/%s", GUI_FileManagerGetPath(self.filebrowser2), ent->name);

					if (FileExists(dst) != 0) {
						sprintf(text, "Overwrite %s", dst);
						GUI_LabelSetText(self.confirm_text, text);

						if(Confirm_Window() != CMD_OK) {
							reset_selected();
							clr_statusbar();
							return;
						}
					}
				}
				else {
					sprintf(dst, "%s/%12.12s.vms", GUI_FileManagerGetPath(self.filebrowser2), ent->name);

					for(i=1;i<99;i++) {
						if(!FileExists(dst)) break;
						sprintf(dst, "%s/%12.12s.%02d.vms", GUI_FileManagerGetPath(self.filebrowser2), ent->name, i);
					}
                    if(FileExists(dst)) { ui_status("Too many backups with this name. Choose another folder."); return; }
				}

				GUI_ProgressBarSetImage1(self.progressbar, self.progres_img_b);
				GUI_ProgressBarSetImage2(self.progressbar, self.progres_img);
				GUI_ProgressBarSetPosition(self.progressbar, 1.0);
				GUI_ContainerAdd(self.vmu_page, self.progressbar_container);
				GUI_WidgetMarkChanged(self.vmu_page);
				LockVideo();
#ifdef VMDEBUG
				dbgio_printf("src: %s\ndst: %s\n", src, dst);
				thd_sleep(2000);
#else
				copy_save(src, dst);
#endif
				if(strncmp(GUI_FileManagerGetPath(self.filebrowser2), "/vmu",4) == 0) free_blocks(GUI_FileManagerGetPath(self.filebrowser2),1);

				GUI_FileManagerScan(self.filebrowser2);
				disable_high(LEFT_FM);
			}
		}

		reset_selected();
		GUI_ContainerRemove(self.vmu_page, self.progressbar_container);
		GUI_WidgetMarkChanged(self.vmu_page);
		clr_statusbar();
		if (VideoIsLocked()) UnlockVideo();
		return;
	}

	VMU_Manager_ItemSelect(fm_ent);
}

static void VMU_ShowFileError(const char *msg) {
	reset_selected();
	disable_high(RIGHT_FM);
	clr_statusbar();
	GUI_LabelSetText(self.save_name, msg);
    ui_status(msg);
	GUI_WidgetMarkChanged(self.vmu_page);
}

void VMU_Manager_ItemSelect(dirent_fm_t *fm_ent) {
	if (!fm_ent) return;

	GUI_Widget *fmw = (GUI_Widget*)fm_ent->obj;

	if (GUI_FileManagerGetSelectedItem(fmw) != fm_ent->index) {
		GUI_FileManagerSetSelectedItem(fmw, fm_ent->index);
	}

	dirent_t *ent = &fm_ent->ent;

	if (ent->attr == O_DIR) {
        reset_selected();
		clr_statusbar();
		return;
	}

	file_t f;
	int xx, yy;
	save_type_t flag_type = DS_VMS;
	uint8 nyb;
	static char tmp[NAME_MAX];
	static char size[64];
	uint16 *tmpbuf = NULL;
	static uint8_t buf[1024];
	static uint8_t icon[512];
	static uint16_t pal[16];
	int i;
	GUI_Widget *panel, *w;
	int name_len = strlen(ent->name);

    if(strlen(GUI_FileManagerGetPath(fmw))+(size_t)name_len+2>=NAME_MAX) {
        VMU_ShowFileError("This path is too long to open."); return;
    }
	save_type_t type = VMU_GetSaveType(ent->name);

	if(strcmp(GUI_ObjectGetName(fmw), "file_browser2") == 0) {
		if(type == DS_VMD) {
			flag_type = DS_VMD;
		}
		else if(type == DS_VMI) {
			flag_type = DS_VMI;
		}
		else if(type == DS_DCI) {
			flag_type = DS_DCI;
		}
		else if((name_len < 4 || (strcasecmp(ent->name + name_len - 4, ".vms") != 0)) &&
				strncmp(GUI_FileManagerGetPath(self.filebrowser2), "/vm", 3) != 0) {
			disable_high(RIGHT_FM);
            reset_selected(); clr_statusbar(); ui_status("Select a VMS, VMI, DCI or VMU image file.");
			return;
		}
	}

	/* file not selected */
	if(self.m_SelectedFile != NULL) {
		free(self.m_SelectedFile);
		free(self.m_SelectedPath);
	}
	self.m_SelectedFile = strdup(ent->name);
	self.m_SelectedPath = strdup(GUI_FileManagerGetPath(fmw));
	self.vmuicon = NULL;

	clr_statusbar();
	GUI_LabelSetText(self.save_name, "Loading...");
	GUI_WidgetMarkChanged(self.vmu_page);
	panel = GUI_FileManagerGetItemPanel(fmw);

	for(i = 0; i < GUI_ContainerGetCount(panel); i++) {

		w = GUI_FileManagerGetItem(fmw, i);
		if(strcmp(GUI_ObjectGetName(fmw), "file_browser") == 0) {
			if(i != fm_ent->index) {
				GUI_ButtonSetNormalImage(w, self.m_ItemNormal);
				GUI_ButtonSetHighlightImage(w, self.m_ItemNormal);
			}
			else {
				GUI_ButtonSetNormalImage(w, self.m_ItemSelected);
				GUI_ButtonSetHighlightImage(w, self.m_ItemSelected);
			}
		}
		else {
			if(i != fm_ent->index) {
				GUI_ButtonSetNormalImage(w, self.m_ItemNormal2);
				GUI_ButtonSetHighlightImage(w, self.m_ItemNormal2);
			}
			else {
				GUI_ButtonSetNormalImage(w, self.m_ItemSelected2);
				GUI_ButtonSetHighlightImage(w, self.m_ItemSelected2);
			}
		}
	}

	if(strcmp(GUI_ObjectGetName(fmw), "file_browser") == 0) {
		disable_high(RIGHT_FM);
	}
	else {
		disable_high(LEFT_FM);
	}

	/* Calculate size first so it can be used even if reading fails */
	if(flag_type == DS_VMI) {
		sprintf(size, "%ld  Block(s)", vmi_t.size / 512);
	}
	else if(flag_type == DS_DCI) {
		sprintf(size, "%d  Block(s)", (ent->size - 32) / 512);
	}
	else {
		sprintf(size, "%d  Block(s)", ent->size / 512);
	}

	if (strcmp(ent->name, "ICONDATA_VMS") == 0) {
		clr_statusbar();
		if(self.logo) {
			GUI_PictureSetImage(self.sicon, self.logo);
		}
		GUI_LabelSetText(self.save_name, ent->name);
		GUI_LabelSetText(self.save_size, size);
		GUI_LabelSetText(self.save_descshort, "No header");
		GUI_LabelSetText(self.save_desclong, "   ");
		GUI_WidgetMarkChanged(self.vmu_page);
		return;
	}

	if(flag_type != DS_VMD) {
		if(!(tmpbuf = (uint16* )calloc(1, 2048))) {
			return;
		}

		sprintf(tmp, "%s/%s", GUI_FileManagerGetPath(fmw), ent->name);

		if(flag_type == DS_VMI) {
			f = fs_open(tmp, O_RDONLY);
			if(f==FILEHND_INVALID || fs_read(f, &vmi_t, 108)!=108) {
                if(f!=FILEHND_INVALID) fs_close(f); free(tmpbuf); VMU_ShowFileError("Invalid VMI metadata."); return;
            }
			fs_close(f);
			sprintf(tmp, "%s/%1.8s.vms", GUI_FileManagerGetPath(fmw), vmi_t.source);
			if(!FileExists(tmp)) sprintf(tmp, "%s/%1.8s.VMS", GUI_FileManagerGetPath(fmw), vmi_t.source);
		}
#ifdef VMDEBUG
		dbgio_printf("VMS filename: %s flag_type: %d\n", tmp, flag_type);
#endif
		/* Clear buffer to avoid using old data */
		memset(buf, 0, sizeof(buf));

		if(flag_type != DS_DCI) {
			if(flag_type == DS_VMS) {
				f = fs_open(tmp, O_RDONLY | O_META);
			}
			else {
				f = fs_open(tmp, O_RDONLY);
			}

			if(f == FILEHND_INVALID) {
				free(tmpbuf);
				VMU_ShowFileError("Open error, file is broken");
				return;
			}
			if(fs_read(f, buf, 1024) <= 0) {
				fs_close(f);
				free(tmpbuf);
				VMU_ShowFileError("Read error, file is broken");
				return;
			}
			fs_close(f);
		}
		else {
			f = fs_open(tmp, O_RDONLY);
			if(f == FILEHND_INVALID) {
				free(tmpbuf);
				VMU_ShowFileError("Open error, file is broken");
				return;
			}
			if(fs_read(f, &dci_t, 1056) != 1056) {
				fs_close(f);
				free(tmpbuf);
				VMU_ShowFileError("Read error, file is broken");
				return;
			}
			fs_close(f);

			for (xx = 0; xx < 1024; xx += 4) {
				for (yy = 3; yy >= 0; yy--) buf[xx + (3-yy)] = dci_t.vmsheader[xx + yy];
			}
		}

		memcpy(self.desc_short, buf, 16);
		self.desc_short[16] = '\0';
		sanitize_vmu_string(self.desc_short, 17);

		memcpy(self.desc_long, buf+0x10, 32);
		self.desc_long[32] = '\0';
		sanitize_vmu_string(self.desc_long, 33);

		memcpy(pal, buf + 0x60, 32);
		memcpy(icon, buf + 0x80, 512);

		for (yy = 0; yy < VMU_ICON_WIDTH; yy++) {
			for (xx = 0; xx < VMU_ICON_HEIGHT; xx += 2) {
				nyb=(icon[yy*16 + xx/2] & 0xf0) >> 4;
				tmpbuf[xx+yy*32]=PACK_NYBBLE_RGB565(pal[nyb]);
				nyb=(icon[yy*16 + xx/2] & 0x0f) >> 0;
				tmpbuf[xx+1+yy*32]=PACK_NYBBLE_RGB565(pal[nyb]);
			}
		}

		// Create a new surface with its own memory
		self.vmu_icon = SDL_CreateRGBSurface(0, 32, 32, 16, 0xf800, 0x07e0, 0x001f, 0);

		if(self.vmu_icon) {
			// Copy pixel data from temporary buffer to surface
			memcpy(self.vmu_icon->pixels, tmpbuf, 2048);

			self.vmuicon = GUI_SurfaceFrom("vmuicon", self.vmu_icon);
			self.vmu_icon = NULL;
		}
		free(tmpbuf);
	}
	else {	// is DS_VMD
		sprintf(self.desc_short, "VMU Dump");
		sprintf(self.desc_long, "Dreamshell VMU Dump file");
	}

	if(flag_type == DS_VMI) {
		sprintf(size, "%ld  Block(s)", vmi_t.size / 512);
	}
	else if(flag_type == DS_DCI) {
		sprintf(size, "%d  Block(s)", (ent->size - 32) / 512);
	}
	else {
		sprintf(size, "%d  Block(s)", ent->size / 512);
	}

#ifdef VMDEBUG
	dbgio_printf("name: %s\n", ent->name);
	dbgio_printf("size: %s\n", size);
	dbgio_printf("descshort: %s\n", self.desc_short);
	dbgio_printf("desclong: %s\n", self.desc_long);
#endif

	if(flag_type == DS_VMD) {
		GUI_PictureSetImage(self.sicon, self.dump_icon);
	}
	else if(self.vmuicon) {
		GUI_PictureSetImage(self.sicon, self.vmuicon);
		GUI_ObjectDecRef((GUI_Object *)self.vmuicon);
	}
	else if(self.logo) {
		GUI_PictureSetImage(self.sicon, self.logo);
	}

	GUI_LabelSetText(self.save_name, ent->name);
	GUI_LabelSetText(self.save_size, size);
	GUI_LabelSetText(self.save_descshort, self.desc_short);
	GUI_LabelSetText(self.save_desclong, self.desc_long);
}

void VMU_Manager_addfileman(GUI_Widget *widget) {
    static char path[NAME_MAX];
    const char *name=GUI_ObjectGetName(widget);
    if(name[0]=='/' && !DirExists(name)) {
        GUI_WidgetSetEnabled(widget,0); ui_status("That location is not available."); return;
    }

	if(self.direction_flag == 1) {
		sprintf(path,"/vmu/%s", GUI_ObjectGetName(widget));
		self.home_path = path;
	}
	else {
		self.home_path = (char *)GUI_ObjectGetName(widget);
	}

	reset_selected();
	disable_high(RIGHT_FM);
	disable_high(LEFT_FM);
	clr_statusbar();

	GUI_ContainerRemove(self.vmu_page, self.sd_c);
	GUI_ContainerRemove(self.vmu_page, self.hdd_c);
	GUI_ContainerRemove(self.vmu_page, self.cd_c);
	GUI_ContainerRemove(self.vmu_page, self.pc_c);
	GUI_ContainerRemove(self.vmu_page, self.format_c);
	GUI_ContainerRemove(self.vmu_page, self.dst_vmu);
	GUI_FileManagerSetPath(self.filebrowser2, self.home_path);
    GUI_FileManagerScan(self.filebrowser2);
	GUI_ContainerAdd(self.vmu_page, self.filebrowser2);
	GUI_WidgetMarkChanged(self.vmu_page);
}

void VMU_Manager_ItemContextClick(dirent_fm_t *fm_ent) {
	dirent_t *ent = &fm_ent->ent;
	GUI_Widget *fmw = (GUI_Widget*)fm_ent->obj;
	char text[1024];
	char path[NAME_MAX];
    if(strlen(GUI_FileManagerGetPath(fmw))+strlen(ent->name)+2>=sizeof(path)) {
        ui_status("This path is too long to delete safely."); return;
    }

	if(ent->attr == O_DIR) {
		if (strcmp(GUI_ObjectGetName(fmw), "file_browser") == 0) {
			VMU_Manager_ItemClick(fm_ent);
			return;
		}
		else if( strncmp(GUI_FileManagerGetPath(fmw),"/cd",3) == 0 ||
				 strncmp(GUI_FileManagerGetPath(fmw),"/vm",3) == 0) {

			reset_selected();
			disable_high(RIGHT_FM);
			clr_statusbar();
			return;
		}
		else {
			reset_selected();
			disable_high(RIGHT_FM);
			clr_statusbar();

			if (fm_ent->index == 0) {
				GUI_CardStackShowIndex(self.pages, 2);
			}
			else {
				sprintf(text, "Delete folder: %s ?", ent->name);
				sprintf(path, "%s/%s",GUI_FileManagerGetPath(self.filebrowser2), ent->name);
				GUI_LabelSetText(self.confirm_text, text);
				if(Confirm_Window() == CMD_OK){
                    if(rmdir_recursive(path)!=CMD_OK) ui_status("Folder deletion failed. Some contents may remain.");
                    else ui_status("Folder deleted.");
					GUI_FileManagerScan(fmw);
				}
			}
		}
		return;
	}
	else if(!self.m_SelectedFile) {
		VMU_Manager_ItemClick(fm_ent);
	}
	else if(strcmp(self.m_SelectedFile,ent->name) != 0 || strcmp(self.m_SelectedPath,GUI_FileManagerGetPath(fmw)) != 0) {
		VMU_Manager_ItemClick(fm_ent);
	}
	else if(!vmu_ui_read_only(self.m_SelectedPath)) {
		/* Delete file */

		sprintf(text, "Delete %s/%s", GUI_FileManagerGetPath(fmw), ent->name);
		GUI_LabelSetText(self.confirm_text, text);

		if(Confirm_Window() == CMD_OK) {
			sprintf(text, "%s/%s", GUI_FileManagerGetPath(fmw), ent->name);
			if(fs_unlink(text)) ui_status("Delete failed. The save could not be removed.");
            else ui_status("Save deleted.");

			if(strncmp(GUI_FileManagerGetPath(fmw) , "/vmu", 4) == 0) {
				if(strcmp(GUI_ObjectGetName(fmw), "file_browser") == 0) {
					free_blocks(text,0);
				}
				else {
					free_blocks(text,1);
				}
			}
		}

		reset_selected();
		clr_statusbar();
		GUI_FileManagerScan(fmw);
		GUI_WidgetMarkChanged(self.vmu_page);
	}
	return;
}

int VMU_Manager_Dump(GUI_Widget *widget) {
    const char *card=GUI_FileManagerGetPath(self.filebrowser);
    const char *other=GUI_FileManagerGetPath(self.filebrowser2);
    maple_device_t *dev=vmu_dev(card);
    bool backup=!strcmp(GUI_ObjectGetName(widget),"dump-button"), created=false;
    char path[NAME_MAX], message[120];
    uint8_t *data=NULL;
    file_t fd=FILEHND_INVALID;
    int result=CMD_ERROR;
    if(!dev) { ui_status("The selected VMU is no longer connected."); return CMD_ERROR; }
    if(backup && (vmu_ui_read_only(other) || !strncmp(other,"/vmu/",5))) {
        ui_status("Choose an SD, IDE or PC folder for the backup."); return CMD_ERROR;
    }
    data=memalign(32,131072);
    if(!data) { ui_status("Not enough memory for a full VMU image."); return CMD_ERROR; }
    GUI_ProgressBarSetImage1(self.progressbar,self.progres_img_b);
    GUI_ProgressBarSetImage2(self.progressbar,self.progres_img);
    GUI_ProgressBarSetPosition(self.progressbar,0.0);
    GUI_ContainerAdd(self.vmu_page,self.progressbar_container);
    if(backup) {
        int number;
        for(number=1;number<=999;++number) {
            if(snprintf(path,sizeof(path),"%s/VMU_%.2s_%03d.vmd",other,card+5,number)>=(int)sizeof(path)) goto done;
            if(!FileExists(path)) break;
        }
        if(number>999) goto done;
        for(int block=0;block<256;++block) {
            if(vmu_block_read(dev,block,data+block*512)<0) goto done;
            GUI_ProgressBarSetPosition(self.progressbar,(block+1)/256.0);
        }
        fd=fs_open(path,O_WRONLY|O_CREAT|O_EXCL);
        if(fd==FILEHND_INVALID) goto done;
        created=true;
        if(fs_write(fd,data,131072)!=131072) goto done;
        if(!strncmp(path,"/sd/",4) || !strncmp(path,"/ide/",5)) {
            ssize_t complete=0; if(fs_complete(fd,&complete)) goto done;
        }
        int closed=fs_close(fd); fd=FILEHND_INVALID;
        if(closed) goto done;
        snprintf(message,sizeof(message),"Backup saved: VMU_%.2s_%03d.vmd",card+5,number);
    } else {
        if(!self.m_SelectedPath || !self.m_SelectedFile ||
            snprintf(path,sizeof(path),"%s/%s",self.m_SelectedPath,self.m_SelectedFile)>=(int)sizeof(path)) goto done;
        fd=fs_open(path,O_RDONLY);
        if(fd==FILEHND_INVALID || fs_total(fd)!=131072) goto done;
        size_t done_bytes=0;
        while(done_bytes<131072) {
            ssize_t got=fs_read(fd,data+done_bytes,131072-done_bytes);
            if(got<=0 || (size_t)got>131072-done_bytes) goto done;
            done_bytes+=(size_t)got;
        }
        int closed=fs_close(fd); fd=FILEHND_INVALID;
        if(closed) goto done;
        /* Only a complete, validated-size source reaches the VMU write loop. */
        for(int block=0;block<256;++block) {
            if(vmu_block_write(dev,block,data+block*512)<0) goto done;
            GUI_ProgressBarSetPosition(self.progressbar,(block+1)/256.0);
        }
        snprintf(message,sizeof(message),"Image restored to VMU %.2s.",card+5);
    }
    result=CMD_OK;
 done:
    if(fd!=FILEHND_INVALID) fs_close(fd);
    if(result!=CMD_OK && created) fs_unlink(path);
    free(data);
    GUI_ContainerRemove(self.vmu_page,self.progressbar_container);
    if(backup) GUI_FileManagerScan(self.filebrowser2);
    else { free_blocks(card,0); GUI_FileManagerScan(self.filebrowser); }
    ui_status(result==CMD_OK?message:backup?"Backup failed. Check the VMU and destination.":"Restore failed. Check the VMU before using its saves.");
    return result;
}

void VMU_Manager_format(GUI_Widget *widget) {

	static uint8_t tmp_buf[512];
	maple_device_t *vmu = vmu_dev(GUI_FileManagerGetPath(self.filebrowser));

	if (!vmu) {
		return;
	}

	char question[100];
    snprintf(question, sizeof(question), "Format VMU %s? All saves on this memory card will be erased.", GUI_FileManagerGetPath(self.filebrowser)+5);
    GUI_LabelSetText(self.confirm_text, question);
	if(Confirm_Window() != CMD_OK) return;

	reset_selected();
	disable_high(LEFT_FM);
	clr_statusbar();

	GUI_ProgressBarSetImage1(self.progressbar, self.progres_img_b);
	GUI_ProgressBarSetImage2(self.progressbar, self.progres_img);
	GUI_ProgressBarSetPosition(self.progressbar, 0.0);
	GUI_ContainerAdd(self.vmu_page, self.progressbar_container);
	GUI_WidgetMarkChanged(self.vmu_page);

	memset(tmp_buf, 0, 512);

	for (int i = 1; i < 14; i++) {
		if(vmu_block_write(vmu, 240+i, tmp_buf)<0) goto format_failed;
		GUI_ProgressBarSetPosition(self.progressbar, (double) i / 15);
		GUI_WidgetMarkChanged(self.progressbar_container);
	}

	uint16_t *tmp_ptr = (uint16_t *) tmp_buf;

	for (int i = 0; i < 241; i++)
	{
		*tmp_ptr++ = 0xFFFC;
	}

	*tmp_ptr++ = 0xFFFA;

	for (int i = 0; i < 12; i++)
	{
		*tmp_ptr++ = 0xF1 + i;
	}

	*tmp_ptr++ = 0xFFFA;
	*tmp_ptr++ = 0xFFFA;

	if(vmu_block_write(vmu, 254, tmp_buf)<0) goto format_failed;

	GUI_ProgressBarSetPosition(self.progressbar, 0.93);
	GUI_WidgetMarkChanged(self.progressbar_container);

	tmp_ptr = (uint16_t *) tmp_buf;

	memset(tmp_buf, 0, 512);

	for (int i = 0; i < 8; i++)
	{
		*tmp_ptr++ = 0x5555;
	}

	*tmp_ptr++ = 0xFF01;
	*tmp_ptr++ = 0xFFFF;
	*tmp_ptr++ = 0x00FF;

	tmp_ptr = (uint16_t *) &tmp_buf[0x30];

	*tmp_ptr++ = 0x2420;
	*tmp_ptr++ = 0x2503;
	*tmp_ptr++ = 0x0022;
	*tmp_ptr++ = 0x0038;

	vm_root_t *root = (vm_root_t *) &tmp_buf[0x40];

	root->size = 255;
	root->partition = 0;
	root->sys_block = 255;
	root->fat_block = 254;
	root->fat_cnt = 1;
	root->file_info_block = 253;
	root->file_info_cnt = 13;
	root->vol_icon = 0;
	root->reserved = 0;
	root->extra_block = 200;
	root->extra_cnt = 31;
	root->exe_block = 0;
	root->exe_cnt = 128;

	if(vmu_block_write(vmu, 255, tmp_buf)<0) goto format_failed;

	GUI_ProgressBarSetPosition(self.progressbar, 1.0);
	GUI_WidgetMarkChanged(self.progressbar_container);

	GUI_ContainerRemove(self.vmu_page, self.progressbar_container);
	free_blocks(GUI_FileManagerGetPath(self.filebrowser), 0);
	GUI_FileManagerScan(self.filebrowser);
	GUI_WidgetMarkChanged(self.vmu_page);
    ui_status("VMU formatted successfully.");
    return;
format_failed:
    GUI_ContainerRemove(self.vmu_page,self.progressbar_container);
    ui_status("Format failed. Reconnect the VMU and check it before use.");

}

void VMU_Manager_sel_dst_vmu(GUI_Widget *widget) {
	self.direction_flag = 1;
	GUI_LabelSetText(self.drection, "CHOOSE THE DESTINATION VMU");
	ScreenFadeOutEx(NULL, 1);
	GUI_CardStackShowIndex(self.pages, 0);
	ScreenFadeIn();
}

void VMU_Manager_make_folder(GUI_Widget *widget) {
    if (!strcmp(GUI_ObjectGetName(widget), "confirm-no")) {
        GUI_CardStackShowIndex(self.pages, 1); return;
    }
    const char *base = GUI_FileManagerGetPath(self.filebrowser2);
    const char *name = GUI_TextEntryGetText(self.folder_name);
    char path[NAME_MAX];
    if (vmu_ui_read_only(base) || !strncmp(base, "/vmu/", 5)) { ui_status("Choose a writable SD, IDE or PC folder first."); return; }
    if (!vmu_ui_folder_name(name) || snprintf(path, sizeof(path), "%s/%s", base, name) >= (int)sizeof(path)) {
        ui_status("Use a short folder name without slashes or special characters."); return;
    }
    if (fs_mkdir(path)) { ui_status("Could not create the folder. It may already exist."); return; }
    GUI_FileManagerScan(self.filebrowser2);
    GUI_CardStackShowIndex(self.pages, 1);
    ui_status("Folder created.");
}

void VMU_Manager_clr_name(GUI_Widget *widget) {

	GUI_TextEntrySetText(widget, "");
}


#include "ui.h"
