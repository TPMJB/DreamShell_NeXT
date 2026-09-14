/* DreamShell ##version##

   module.c - Games app module
   Copyright (C) 2024-2026 Maniac Vera

*/

#include <ds.h>
#include <sfx.h>
#include <ctype.h>
#include "app_menu.h"
#include "app_system_menu.h"
#include "app_preset.h"
#include "app_utils.h"
#include "app_module.h"
#include "app_definition.h"
#include <kos/md5.h>
#include <isoldr.h>
#include <dc/video.h>
#include <tsunami/tsudefinition.h>
#include <tsunami/tsunamiutils.h>
#include <tsunami/font.h>
#include <tsunami/color.h>
#include <tsunami/drawable.h>
#include <tsunami/texture.h>
#include <tsunami/trigger.h>
#include <tsunami/vector.h>
#include <tsunami/anims/expxymover.h>
#include <tsunami/anims/logxymover.h>
#include <tsunami/anims/fadein.h>
#include <tsunami/anims/fadeout.h>
#include <tsunami/anims/fadeto.h>
#include <tsunami/triggers/chainanim.h>
#include <tsunami/triggers/death.h>
#include <tsunami/drawables/triangle.h>
#include <tsunami/drawables/box.h>
#include <tsunami/drawables/optiongroup.h>
#include <tsunami/drawables/form.h>
#include <tsunami/dsapp.h>

DEFAULT_MODULE_EXPORTS(app_games_menu);

extern struct MenuStructure menu_data;
static Event_t *do_menu_control_event;
static Event_t *do_menu_video_event;
static void* MenuExitHelper(void *params);
static mutex_t change_page_mutex = MUTEX_INITIALIZER;

static struct
{
	App_t *app;
	Scene *scene_ptr;
	Drawable *over_drawable_ptr;
	Drawable *last_over_drawable_ptr;
	uint over_object_type;

	bool have_args;
	int image_type;
	int sector_size;
	uint8 md5[16];
	uint8 boot_sector[2048];
	uint32 addr;
	isoldr_info_t *isoldr;
	
	bool exit_app, wait_to_exit_app;
	bool first_menu_load;
	volatile bool game_changed;
	volatile bool show_cover_game;
	uint32 scan_covers_start_time;
	uint32 scan_covers_end_time;
	int pages;
	int current_page;
	int previous_page;
	int game_count;
	int menu_cursel;
	int scan_count;
	uint8 device_selected;

	int game_index_selected;
	char item_value_selected[NAME_MAX];

	LogXYMover *item_selector_animation;
	Rectangle *item_selector;
	Font *menu_font;
	Death *exit_trigger_list[MAX_SIZE_ITEMS];
	LogXYMover *run_animation;
	ExpXYMover *exit_animation_list[MAX_SIZE_ITEMS];
	LogXYMover *item_game_animation[MAX_SIZE_ITEMS];
	ItemMenu *item_game[MAX_SIZE_ITEMS];
	ItemMenu *item_button[MAX_BUTTONS];
	ItemMenu *action_left_button;
	ItemMenu *action_right_button;
	ItemMenu *action_view_button;
	Banner *img_cover_game_background;
	Banner *img_cover_game;
	FadeTo *animation_cover_game;
	FadeOut *fadeout_cover_animation;
	Texture *texture_cover_game_background;
	Texture *texture_cover_game;
	Label *title;
	Label *title_type;
	Label *page_label;
	Label *total_label;
	LogXYMover *title_animation;
	LogXYMover *title_type_animation;
	Box *main_box;
	Rectangle *area_rectangle;
	Rectangle *title_rectangle;
	Rectangle *title_background_rectangle;
	Rectangle *title_type_rectangle;
	Rectangle *menu_options_rectangle;
	Rectangle *game_list_rectangle;
	Rectangle *img_cover_game_rectangle;
	kthread_t *show_cover_thread;
} self;

#include "next_library.h"
static void CreateMainView(void);

static void* ShowCoverThread(void *params)
{
	int game_index = (int)params;
	if (game_index >= 0)
	{
		srand(time(NULL));
		uint64_t start_time = 0;
		uint64_t end_time = 0;
		if (self.img_cover_game != NULL)
		{
			self.show_cover_game = false;
			if (self.animation_cover_game != NULL)
			{
				TSU_AnimationComplete((Animation *)self.animation_cover_game, (Drawable *)self.img_cover_game);
				thd_pass();
				TSU_FadeToDestroy(&self.animation_cover_game);
			}
			
			if (self.fadeout_cover_animation == NULL)
			{			
				self.fadeout_cover_animation = TSU_FadeOutCreate(10.0f);
				TSU_DrawableAnimAdd((Drawable *)self.img_cover_game, (Animation *)self.fadeout_cover_animation);
			}

			const uint32_t max_time = 200;
			end_time = start_time = timer_ms_gettime64();
			while (!self.game_changed && menu_data.menu_type == MT_PLANE_TEXT && (end_time - start_time) <= max_time)
			{
				thd_pass();
				end_time = timer_ms_gettime64();
			}

			if (self.game_changed || menu_data.menu_type != MT_PLANE_TEXT)
				return NULL;

			TSU_AnimationComplete((Animation *)self.fadeout_cover_animation, (Drawable *)self.img_cover_game);
			thd_pass();
			TSU_FadeOutDestroy(&self.fadeout_cover_animation);

			TSU_DrawableSetFinished((Drawable *)self.img_cover_game);
			TSU_AppSubRemoveBanner(self.app->tsunami, self.img_cover_game);

			thd_pass();
			TSU_BannerDestroy(&self.img_cover_game);
			TSU_TextureDestroy(&self.texture_cover_game);
		}

		const uint32_t max_time = 400;		
		if (start_time == 0)
			end_time = start_time = timer_ms_gettime64();

		while (!self.game_changed && menu_data.menu_type == MT_PLANE_TEXT && (end_time - start_time) <= max_time)
		{
			thd_pass();
			end_time = timer_ms_gettime64();
		}

		if (self.game_changed || menu_data.menu_type != MT_PLANE_TEXT)
			return NULL;

		uint16 checked_cover = CheckCover(game_index, MT_PLANE_TEXT);
		if (checked_cover == SC_EXISTS || checked_cover == SC_DEFAULT)
		{
			self.show_cover_game = true;

			Vector vector_position = {486, 236, ML_ITEM + 2, 1};
			char *game_cover_path = NULL;
			uint16 cover_type = GetCoverType(game_index, MT_PLANE_TEXT);

            if(!GetGameCoverPath(game_index,&game_cover_path,MT_PLANE_TEXT)) return NULL;

			self.texture_cover_game = TSU_TextureCreateFromFile(game_cover_path, cover_type != IT_JPG, false, 0);
			if(!self.texture_cover_game) { free(game_cover_path); self.show_cover_game=false; return NULL; }
			self.img_cover_game = TSU_BannerCreate(cover_type == IT_JPG ? PVR_LIST_OP_POLY : PVR_LIST_TR_POLY, self.texture_cover_game);
			free(game_cover_path);
			
			TSU_DrawableTranslate((Drawable *)self.img_cover_game, &vector_position);
			TSU_BannerSetSize(self.img_cover_game, 256, 256);

			vector_position.x = 0.0f;
			vector_position.y = 0.0f;			
			TSU_DrawableSetScale((Drawable *)self.img_cover_game, &vector_position);
			
			TSU_AppSubAddBanner(self.app->tsunami, self.img_cover_game);
			self.animation_cover_game = TSU_FadeToCreate(0.0f, 1.0f, 7.5f);
			TSU_DrawableAnimAdd((Drawable *)self.img_cover_game, (Animation *)self.animation_cover_game);
			
			self.show_cover_game = false;
		}
	}

	return NULL;
}

static bool StopShowCover()
{
	if (self.show_cover_thread != NULL)
	{
		thd_join(self.show_cover_thread, NULL);
		self.show_cover_thread = NULL;
	}
	self.game_changed = false;

	return (self.show_cover_thread == NULL);
}

static void ShowCover(int game_index)
{
	if (menu_data.menu_type == MT_PLANE_TEXT && game_index >= 0)
	{
		if (StopShowCover())
		{
			self.show_cover_thread = thd_create(0, ShowCoverThread, (void *)game_index);
			if(self.show_cover_thread) thd_set_prio(self.show_cover_thread, PRIO_DEFAULT - 2);
		}
	}
}

static bool LoadCover(ItemMenu *item_menu, int game_count)
{
	bool loaded_cover = false;

	if (item_menu != NULL)
	{
		int game_index = TSU_ItemMenuGetItemIndex(item_menu);

		if (menu_data.menu_type == MT_PLANE_TEXT)
		{
			if (self.menu_cursel == game_count)
			{
				ShowCover(game_index);
				loaded_cover = true;
			}
		}
		else
		{
			char *game_cover_path = NULL;
			int cover_mode=menu_data.menu_type;
            if(CheckCover(game_index,cover_mode)!=SC_EXISTS && CheckCover(game_index,MT_PLANE_TEXT)==SC_EXISTS)
                cover_mode=MT_PLANE_TEXT;
            if (GetGameCoverPath(game_index, &game_cover_path, cover_mode))
			{	
				TSU_ItemMenuSetImage(item_menu, game_cover_path, ContainsCoverType(game_index, cover_mode, IT_JPG) ? PVR_LIST_OP_POLY : PVR_LIST_TR_POLY);
				loaded_cover = true;
				free(game_cover_path);
			}
		}
	}

	return loaded_cover;
}

static void SetTitle(int game_index, const char *text, bool limit_length)
{
    (void)game_index; (void)limit_length;
    if(!self.title) self.title=NextLabel(text,17,24,80,520,&next_white);
    else TSU_LabelSetText(self.title,text);
    TSU_DrawableSetSize((Drawable *)self.title,520,17);
}

static void SetTitleType(const char *full_path_game, bool is_gdi_optimized)
{
    (void)is_gdi_optimized;
    const char *ext=full_path_game ? strrchr(full_path_game,'.') : NULL;
    char type[8]="";
    if(ext) snprintf(type,sizeof(type),"%s",ext+1);
    for(char *c=type;*c;++c) *c=toupper((unsigned char)*c);
    if(!self.title_type) self.title_type=NextLabel(type,12,564,80,52,&next_cyan);
    else TSU_LabelSetText(self.title_type,type);
}

static void SetCursor(void)
{
    if(self.game_count<=0) return;
    if(!self.first_menu_load) ds_sfx_play(DS_SFX_CLICK2);
    if(self.item_selector_animation) {
        TSU_AnimationComplete((Animation *)self.item_selector_animation,(Drawable *)self.item_selector);
        TSU_LogXYMoverDestroy(&self.item_selector_animation);
    }
    if(self.item_selector) {
        TSU_AppSubRemoveRectangle(self.app->tsunami,self.item_selector);
        TSU_RectangleDestroy(&self.item_selector);
    }
    NextRect r=NextGameRect(menu_data.menu_type,self.menu_cursel);
    Color fill={.16f,next_cyan.r,next_cyan.g,next_cyan.b};
    self.item_selector=TSU_RectangleCreateWithBorder(PVR_LIST_TR_POLY,0,0,r.w,r.h,
        &fill,ML_SELECTED-1,2,&next_cyan,0);
    Vector v={r.x,r.y+r.h,ML_SELECTED-1,1};
    TSU_DrawableSetTranslate((Drawable *)self.item_selector,&v);
    TSU_AppSubAddRectangle(self.app->tsunami,self.item_selector);
    self.item_selector_animation=TSU_LogXYMoverCreate(v.x,v.y);
    TSU_DrawableAnimAdd((Drawable *)self.item_selector,(Animation *)self.item_selector_animation);
}

static void RemoveViewTextPlane(bool remove_image_banner)
{
	self.show_cover_game = false;

	if (self.img_cover_game_background != NULL)
	{
		TSU_DrawableSetFinished((Drawable *)self.img_cover_game_background);
	}

	if (self.img_cover_game_rectangle != NULL)
	{
		TSU_DrawableSetFinished((Drawable *)self.img_cover_game_rectangle);
	}	

	for (int i = 0; i < MAX_BUTTONS; i++)
	{
		if (self.item_button[i] != NULL)
		{
			TSU_DrawableSetFinished((Drawable *)self.item_button[i]);
		}
	}

	if (self.img_cover_game != NULL && remove_image_banner)
	{
		if (self.animation_cover_game != NULL)
		{
			TSU_AnimationComplete((Animation *)self.animation_cover_game, (Drawable *)self.img_cover_game);
		}

		if (self.fadeout_cover_animation != NULL)
		{
			TSU_AnimationComplete((Animation *)self.fadeout_cover_animation, (Drawable *)self.img_cover_game);
		}		

		TSU_DrawableSetFinished((Drawable *)self.img_cover_game);
	}
	
	if (self.menu_options_rectangle != NULL)
	{
		TSU_DrawableSetFinished((Drawable *)self.menu_options_rectangle);
	}

	if (self.game_list_rectangle != NULL)
	{
		TSU_DrawableSetFinished((Drawable *)self.game_list_rectangle);
	}

	if (self.page_label != NULL)
	{
		TSU_DrawableSetFinished((Drawable *)self.page_label);		
	}

	if (self.total_label != NULL)
	{
		TSU_DrawableSetFinished((Drawable *)self.total_label);		
	}

	TSU_DrawableSubRemoveFinished((Drawable *)self.scene_ptr);
	thd_pass();

	if (self.img_cover_game_background != NULL)
	{
		TSU_BannerDestroy(&self.img_cover_game_background);
		TSU_TextureDestroy(&self.texture_cover_game_background);
	}

	if (self.img_cover_game_rectangle != NULL)
	{
		TSU_RectangleDestroy(&self.img_cover_game_rectangle);
	}	

	for (int i = 0; i < MAX_BUTTONS; i++)
	{
		if (self.item_button[i] != NULL)
		{
			TSU_ItemMenuDestroy(&self.item_button[i]);
		}
	}

	if (self.img_cover_game != NULL && remove_image_banner)
	{
		if (self.animation_cover_game != NULL)
		{
			TSU_FadeToDestroy(&self.animation_cover_game);
		}

		if (self.fadeout_cover_animation != NULL)
		{
			TSU_FadeOutDestroy(&self.fadeout_cover_animation);
		}		

		TSU_BannerDestroy(&self.img_cover_game);
		TSU_TextureDestroy(&self.texture_cover_game);
	}

	if (self.menu_options_rectangle != NULL)
	{
		TSU_RectangleDestroy(&self.menu_options_rectangle);
	}

	if (self.game_list_rectangle != NULL)
	{
		TSU_RectangleDestroy(&self.game_list_rectangle);
	}

	if (self.page_label != NULL)
	{
		TSU_LabelDestroy(&self.page_label);
	}

	if (self.total_label != NULL)
	{
		TSU_LabelDestroy(&self.total_label);
	}
}

static void RefreshTotal(void)
{
    const char *view=menu_data.menu_type==MT_PLANE_TEXT ? "List" :
        menu_data.menu_type==MT_IMAGE_TEXT_64_5X2 ? "Compact" : "Gallery";
    char text[180];
    snprintf(text,sizeof(text),"%d games  |  Page %d / %d  |  %s%s%s",
        menu_data.games_array_ptr_count,self.pages>0?self.current_page:0,self.pages>0?self.pages:0,view,
        menu_data.category[0]?"  |  ":"",menu_data.category);
    if(library.status) {
        TSU_LabelSetText(library.status,text);
        TSU_DrawableSetSize((Drawable *)library.status,592,13);
    }
}

static void GamesApp_OnMouseOverEvent(Drawable *drawable, uint object_type, int id)
{
	switch(TSU_InputEventStateGetGlobalWindowState())
	{
		case SA_GAMES_MENU:
		{
			self.over_drawable_ptr = drawable;
			self.over_object_type = object_type;
			break;
		}

		default:
			break;
	}
}

static void DeselectAllActionButtons(void) {}

static ItemMenu* CreateActionButton(const char *button_file, float x, float y, float width, float height)
{
	ItemMenu *item_menu = NULL;
	char image_path[NAME_MAX] = {};
	snprintf(image_path, NAME_MAX, "%s/%s/%s", GetDefaultDir(menu_data.current_dev), "apps/games_menu/images", button_file);

	if (FileExists(image_path))
	{	
		item_menu = TSU_ItemMenuCreateImage(image_path
							, width
							, height
							, PVR_LIST_TR_POLY							
							, false
							, PVR_TXRLOAD_DMA);

		Vector vector_position = {x, y, ML_ITEM + 3, 1};
		TSU_DrawableTranslate((Drawable *)item_menu, &vector_position);

		Color color_unselected = { 1, 1.0f, 1.0f, 1.0f };
		Color color_selected = { 1.0f, 1.0f, 1.0f, 0.35f };
		
		TSU_ItemMenuSetColorUnselected(item_menu, &color_unselected);
		TSU_ItemMenuSetImageColor(item_menu, &color_selected);
		TSU_ItemMenuSetTextColor(item_menu, &color_selected);
		TSU_DrawableEventSetOnMouseOver((Drawable *)item_menu, GamesApp_OnMouseOverEvent);
	}

	return item_menu;
}

static void CreateInfoButton(uint8 button_index, const char *button_file, const char *text, float x, float y)
{
	if(self.item_button[button_index] == NULL)
	{
		char *image_path = (char *)malloc(NAME_MAX);
		snprintf(image_path, NAME_MAX, "%s/%s/%s", GetDefaultDir(menu_data.current_dev), "apps/games_menu/images", button_file);

		if (FileExists(image_path))
		{
			Color color = {1, 1.0f, 1.0f, 1.0f};
			Vector vector_translate = { x, y, ML_ITEM + 3, 1};
			self.item_button[button_index] = TSU_ItemMenuCreate(image_path, 32, 32, PVR_LIST_TR_POLY, text, self.menu_font, 15, 0, 0, false, 0);
			TSU_ItemMenuSetTranslate(self.item_button[button_index], &vector_translate);
			TSU_LabelSetTint(TSU_ItemMenuGetLabel(self.item_button[button_index]), &color);

			const Vector *text_position = TSU_DrawableGetTranslate((Drawable *)TSU_ItemMenuGetLabel(self.item_button[button_index]));
			Vector new_position = { text_position->x, text_position->y, text_position->z, text_position->w };
			new_position.x -= 5;
			TSU_LabelSetTranslate(TSU_ItemMenuGetLabel(self.item_button[button_index]), &new_position);
			TSU_LabelSetFixWidth(TSU_ItemMenuGetLabel(self.item_button[button_index]), false);

			Color color_unselected = { 1, 1.0f, 1.0f, 1.0f };
			Color color_selected = { 1.0f, 1.0f, 1.0f, 0.35f };
			
			TSU_ItemMenuSetColorUnselected(self.item_button[button_index], &color_unselected);
			TSU_ItemMenuSetTextColor(self.item_button[button_index], &color_selected);
			TSU_DrawableEventSetOnMouseOver((Drawable *)self.item_button[button_index], GamesApp_OnMouseOverEvent);
		}

		free(image_path);
	}
}

static void AddInfoButtons()
{
	for (int i = 0; i < MAX_BUTTONS; i++)
	{
		if (self.item_button[i] != NULL)
		{
			TSU_AppSubAddItemMenu(self.app->tsunami, self.item_button[i]);
		}
	}
}

static void CreateViewTextPlane(void)
{
    self.game_list_rectangle=NextPanel((NextRect){24,96,316,288},&next_panel,ML_BACKGROUND+1);
    self.img_cover_game_rectangle=NextPanel((NextRect){356,96,260,288},&next_panel,ML_BACKGROUND+1);
}

static void CalculatePages()
{
	self.pages = (menu_data.games_array_ptr_count > 0 ? ceil((float)menu_data.games_array_ptr_count / (float)menu_data.menu_option.max_page_size) : 0);
}

static bool LoadPage(bool change_view, uint8 direction)
{
	bool loaded = false;
	char game_cover_path[NAME_MAX];
	char name[NAME_MAX];
	char *game_cover_path_tmp = NULL;

	if (self.pages == -1)
	{
		if (RetrieveGames())
		{
			menu_data.games_array_ptr_count = menu_data.games_array_count;
			menu_data.games_array_ptr = (GameItemStruct **)&menu_data.games_array;
			timer_ms_gettime(&self.scan_covers_start_time, NULL);
			CalculatePages();
		}
		
		for (int imenu = 1; imenu <= MAX_MENU; imenu++)
		{
			RetrieveCovers(menu_data.current_dev, imenu);
		}
	}
	else
	{
		if (change_view)
		{
			CalculatePages();
		}		
	}

	if (self.pages > 0)
	{
        NextRemoveLabel(&library.empty);
		if (menu_data.last_game_played_index >= 0)
		{
			self.current_page = ceil((float)(menu_data.last_game_played_index + 1) / (float)menu_data.menu_option.max_page_size);
		}
		
		if (self.current_page < 1)
		{
			self.current_page = self.pages;
		}
		else if (self.current_page > self.pages)
		{
			self.current_page = 1;
		}

		if (self.current_page != self.previous_page || change_view)
		{
			if (!change_view && !self.first_menu_load)
				ds_sfx_play(DS_SFX_CLICK);

			for (int i = 0; i < MAX_SIZE_ITEMS; i++)
			{
				if (self.item_game_animation[i] != NULL)
				{
					TSU_AnimationComplete((Animation *)self.item_game_animation[i], (Drawable *)self.item_game[i]);
				}

				if (self.item_game[i] != NULL)
				{
					TSU_DrawableSetFinished((Drawable *)self.item_game[i]);
				}
			}

			TSU_DrawableSubRemoveFinished((Drawable *)self.scene_ptr);
			thd_pass();

			for (int i = 0; i < MAX_SIZE_ITEMS; i++)
			{
				if (self.item_game_animation[i] != NULL)
				{
					TSU_LogXYMoverDestroy(&self.item_game_animation[i]);
				}

				if (self.item_game[i] != NULL)
				{
					TSU_ItemMenuDestroy(&self.item_game[i]);
				}
			}

			if (change_view || self.first_menu_load)
			{				
				RemoveViewTextPlane(true);
				if (menu_data.menu_type == MT_PLANE_TEXT)
				{					
					CreateViewTextPlane();
				}
				else
				{
					self.game_list_rectangle = TSU_RectangleCreate(PVR_LIST_OP_POLY, 24, 392, 592, 296, &next_panel, ML_BACKGROUND + 1, 0);
					TSU_AppSubAddRectangle(self.app->tsunami, self.game_list_rectangle);
				}
			}

			RefreshTotal();
			int cover_menu_type = 0;
			int column = 0;
			int page = 0;
			int game_index = 0;
			GameItemStruct *game_ptr = NULL;
			ItemMenu *item_selected = NULL;
			self.game_count = 0;
			Vector vectorTranslate = {0, 480, ML_ITEM, 1};

			for (int icount = ((self.current_page - 1) * menu_data.menu_option.max_page_size); icount < menu_data.games_array_ptr_count; icount++)
			{
				page = ceil((float)(icount + 1) / (float)menu_data.menu_option.max_page_size);

				if (page < self.current_page)
					continue;
				else if (page > self.current_page)
					break;

				self.game_count++;
				game_ptr = GetGamePtrByIndex(icount);
				game_index = game_ptr->game_index_tmp >= 0 ? game_ptr->game_index_tmp : icount;

				column = floor((float)(self.game_count - 1) / (float)menu_data.menu_option.size_items_column);				
				
				if (menu_data.menu_type != MT_PLANE_TEXT)
				{
					game_cover_path_tmp = NULL;
                    game_cover_path[0]=0;
					if (CheckCover(game_index, menu_data.menu_type) == SC_EXISTS)
					{
						cover_menu_type = menu_data.menu_type;
					}
					else
					{
						if (CheckCover(game_index, MT_PLANE_TEXT) == SC_DEFAULT)
							cover_menu_type = menu_data.menu_type;
						else
							cover_menu_type = MT_PLANE_TEXT;
					}

					if (GetGameCoverPath(game_index, &game_cover_path_tmp, cover_menu_type))
					{
						strcpy(game_cover_path, game_cover_path_tmp);
						free(game_cover_path_tmp);
					}
				}

				memset(name, 0, sizeof(name));
				if (game_ptr->is_folder_name)
				{
					strcpy(name, game_ptr->folder_name);
				}
				else
				{
					strncpy(name, game_ptr->game, strlen(game_ptr->game) - 4);
				}


                NextRect r=NextGameRect(menu_data.menu_type,self.game_count-1);
                ItemMenu *item_menu;
                Vector position={r.x+12,r.y+25,ML_ITEM,1};
                if(menu_data.menu_type==MT_PLANE_TEXT) {
                    item_menu=TSU_ItemMenuCreateLabel(name,self.menu_font,18,r.w-24,20);
                    Vector origin={0,0,2,1};
                    TSU_LabelSetTranslate(TSU_ItemMenuGetLabel(item_menu),&origin);
                } else {
                    int compact=menu_data.menu_type==MT_IMAGE_TEXT_64_5X2;
                    int size=compact?64:128;
                    item_menu=TSU_ItemMenuCreate(game_cover_path,size,size,
                        ContainsCoverType(game_index,cover_menu_type,IT_JPG)?PVR_LIST_OP_POLY:PVR_LIST_TR_POLY,
                        name,self.menu_font,compact?16:14,compact?210:176,18,false,PVR_TXRLOAD_DMA);
                    position.x=r.x+(compact?36:r.w/2);
                    position.y=r.y+(compact?35:64);
                    Vector image_origin={0,0,2,1};
                    TSU_DrawableSetTranslate((Drawable *)TSU_ItemMenuGetBanner(item_menu),&image_origin);
                    Vector caption={compact?38:-88,compact?6:80,2,1};
                    TSU_LabelSetTranslate(TSU_ItemMenuGetLabel(item_menu),&caption);
                }
                self.item_game[self.game_count-1]=item_menu;
                TSU_ItemMenuSetColorUnselected(item_menu,&next_white);
                TSU_ItemMenuSetTextColor(item_menu,&next_cyan);
                TSU_ItemMenuSetTranslate(item_menu,&position);
                TSU_ItemMenuSetItemIndex(item_menu,game_index);
                TSU_ItemMenuSetItemValue(item_menu,name);
                /* Retain animation ownership expected by the existing exit path. */
                self.item_game_animation[self.game_count-1]=TSU_LogXYMoverCreate(position.x,position.y);
                TSU_ItemMenuAnimAdd(item_menu,(Animation *)self.item_game_animation[self.game_count-1]);

				if (self.first_menu_load)
				{
					if ((self.game_count == 1 && menu_data.last_game_played_index == -1) 
						|| menu_data.last_game_played_index == icount)
					{
						item_selected = self.item_game[self.game_count - 1];
						menu_data.last_game_played_index = -1;
						self.menu_cursel = self.game_count - 1;
					}
					else
					{
						TSU_ItemMenuSetSelected(item_menu, false, false);
					}
				}
				
				TSU_AppSubAddItemMenu(self.app->tsunami, item_menu);
			}

			if (self.first_menu_load)
			{
				if (self.game_count > 0)
				{
					if (item_selected == NULL)
					{
						self.menu_cursel = 0;
						item_selected = self.item_game[0];
					}
				}
				
				if (item_selected)
				{
					int game_index = TSU_ItemMenuGetItemIndex(item_selected);
					memset(name, 0, sizeof(name));

					if (menu_data.games_array[game_index].is_folder_name)
					{
						strcpy(name, menu_data.games_array[game_index].folder_name);
					}
					else
					{
						strncpy(name, menu_data.games_array[game_index].game, strlen(menu_data.games_array[game_index].game) - 4);
					}

					TSU_ItemMenuSetSelected(item_selected, true, false);
					SetTitle(game_index, name, true);
					SetTitleType(GetFullGamePathByIndex(game_index)
						, CheckGdiOptimized(game_index));

					SetCursor();
					ShowCover(game_index);
					PlayCDDA(game_index);
				}
			}

			menu_data.last_game_played_index = -1;
			loaded = true;
			self.first_menu_load = false;
		}
	}
    else {
        self.game_count=0;
        SetTitle(-1,"Your library is empty",true);
        if(!library.empty) {
            char message[512];
            snprintf(message,sizeof(message),"Add disc images to:\n%s\n\nThen use Settings to rebuild the game cache.",GetGamesPath(menu_data.current_dev));
            library.empty=NextLabel(message,17,40,160,544,&next_muted);
        }
        RefreshTotal();
    }

	return loaded;
}

static void ReloadPage()
{
	if (menu_data.games_array_ptr_count > 0)
	{
		CalculatePages();
		self.first_menu_load = true;
		int game_index = TSU_ItemMenuGetItemIndex(self.item_game[self.menu_cursel]);
		menu_data.last_game_played_index = -1;
		
		if (menu_data.category[0] != '\0')
		{
			menu_data.last_game_played_index = 0;
			GameItemStruct *game_ptr = NULL;
			for (int icount = 0; icount < menu_data.games_array_ptr_count; icount++)
			{
				game_ptr = GetGamePtrByIndex(icount);

				if (game_ptr->game_index_tmp == game_index)
				{
					menu_data.last_game_played_index = icount;
					break;
				}
			}
		}
		else
		{
			menu_data.last_game_played_index = game_index;
		}

		LoadPage(false, DMD_NONE);
	}
}

static void InitMenu()
{
	TSU_InputEventStateSetGlobalWindowState(SA_GAMES_MENU);

	self.scene_ptr = TSU_AppGetScene(self.app->tsunami);

	char font_path[NAME_MAX];
	memset(font_path, 0, sizeof(font_path));
	snprintf(font_path, sizeof(font_path), "%s/%s", GetDefaultDir(menu_data.current_dev), "apps/games_menu/fonts/default.txf");
	self.menu_font = TSU_FontCreate(font_path, PVR_LIST_TR_POLY);

	SetMenuType(menu_data.menu_type);
	self.exit_app = false;
	self.title = NULL;
	self.title_type = NULL;
	self.game_index_selected = -1;

	Vector vector = {0, 0, 10, 1};
	TSU_AppSetTranslate(self.app->tsunami, &vector);
	TSU_AppSetBg(self.app->tsunami, menu_data.background_color.r, menu_data.background_color.g, menu_data.background_color.b);

	if (LoadScannedCover())
	{
		CleanIncompleteCover();
	}
	else 
	{
		memset(&menu_data.cover_scanned_app, 0, sizeof(CoverScannedStruct));
	}

    CreateMainView();
	self.current_page = 1;
	LoadPage(false, DMD_NONE);
}

static void RemoveAll()
{
    NextDestroyChrome();
	TSU_AnimationComplete((Animation *)self.title_animation, (Drawable *)self.title);
	TSU_AnimationComplete((Animation *)self.title_type_animation, (Drawable *)self.title_type);
	TSU_AnimationComplete((Animation *)self.item_selector_animation, (Drawable *)self.item_selector);

	if (self.animation_cover_game != NULL && self.img_cover_game != NULL)
	{
		TSU_AnimationComplete((Animation *)self.animation_cover_game, (Drawable *)self.img_cover_game);
	}


	TSU_DrawableSetFinished((Drawable *)self.main_box);
	TSU_DrawableSetFinished((Drawable *)self.area_rectangle);	
	TSU_DrawableSetFinished((Drawable *)self.title_rectangle);
	TSU_DrawableSetFinished((Drawable *)self.title_background_rectangle);	
	TSU_DrawableSetFinished((Drawable *)self.title_type_rectangle);	
	TSU_DrawableSetFinished((Drawable *)self.menu_options_rectangle);
	TSU_DrawableSetFinished((Drawable *)self.game_list_rectangle);
	TSU_DrawableSetFinished((Drawable *)self.page_label);
	TSU_DrawableSetFinished((Drawable *)self.total_label);
	TSU_DrawableSetFinished((Drawable *)self.item_selector);
	TSU_DrawableSetFinished((Drawable *)self.title);
	TSU_DrawableSetFinished((Drawable *)self.title_type);
	TSU_DrawableSetFinished((Drawable *)self.action_left_button);
	TSU_DrawableSetFinished((Drawable *)self.action_right_button);
	TSU_DrawableSetFinished((Drawable *)self.action_view_button);

	if (self.img_cover_game != NULL)
	{
		TSU_DrawableSetFinished((Drawable *)self.img_cover_game);
	}	
	
	if (self.img_cover_game_background != NULL)
	{
		TSU_DrawableSetFinished((Drawable *)self.img_cover_game_background);
	}

	if (self.img_cover_game_rectangle != NULL)
	{
		TSU_DrawableSetFinished((Drawable *)self.img_cover_game_rectangle);
	}
	
	for (int i = 0; i < MAX_BUTTONS; i++)
	{
		if (self.item_button[i] != NULL)
		{
			TSU_DrawableSetFinished((Drawable *)self.item_button[i]);
		}
	}

	for (int i = 0; i < MAX_SIZE_ITEMS; i++)
	{
		if (self.item_game_animation[i] != NULL)
		{
			TSU_AnimationComplete((Animation *)self.item_game_animation[i], (Drawable *)self.item_game[i]);
		}
	}

	SystemMenuRemoveAll();
	TSU_DrawableSubRemoveFinished((Drawable *)self.scene_ptr);
	thd_pass();
}

static bool ExitAnimationsFinished(void)
{
	bool has_items = false;

	for (int i = 0; i < MAX_SIZE_ITEMS; i++)
	{
		if (self.item_game[i] == NULL)
		{
			continue;
		}

		has_items = true;

		if (!TSU_DrawableIsFinished((Drawable *)self.item_game[i]))
		{
			return false;
		}
	}

	return has_items || self.game_count == 0;
}

static void StartExit()
{
	StopCDDA();
	StopShowCover();
	RemoveAll();
	ds_sfx_play(DS_SFX_CLICK);

	float y = 1.0f;
	self.game_index_selected = -1;
	menu_data.finished_menu = false;

	for (int i = 0; i < MAX_SIZE_ITEMS; i++)
	{
		if (self.item_game[i] != NULL)
		{
			if (!self.exit_app && TSU_ItemMenuIsSelected(self.item_game[i]))
			{
				self.game_index_selected = TSU_ItemMenuGetItemIndex(self.item_game[i]);
				strcpy(self.item_value_selected, GetFullGamePathByIndex(self.game_index_selected));
				self.device_selected = menu_data.games_array[TSU_ItemMenuGetItemIndex(self.item_game[i])].device;

				if (menu_data.menu_type == MT_PLANE_TEXT)
				{
					float w, h;
					char *label_text = (char *)malloc(NAME_MAX);
					memset(label_text, 0, NAME_MAX);

					strcpy(label_text, TSU_ItemMenuGetLabelText(self.item_game[i]));
					label_text = Trim(label_text);

					TSU_FontSetSize(self.menu_font, 18);
					TSU_FontGetTextSize(self.menu_font, label_text, &w, &h);
					self.run_animation = TSU_LogXYMoverCreate((640/2 - w/2), (480 - 84) / 2);

					free(label_text);
				}
				else if (TSU_ItemMenuHasTextAndImage(self.item_game[i]))
				{
					self.run_animation = TSU_LogXYMoverCreate((640 - (84 + 64)) / 2, (480 - 84) / 2);
				}
				else
				{
					self.run_animation = TSU_LogXYMoverCreate((640 - 84) / 2, (480 - 84) / 2);
				}

				self.exit_trigger_list[i] = TSU_DeathCreate(NULL);
				TSU_TriggerAdd((Animation *)self.run_animation, (Trigger *)self.exit_trigger_list[i]);
				TSU_DrawableAnimAdd((Drawable *)self.item_game[i], (Animation *)self.run_animation);
			}
			else
			{
				self.exit_animation_list[i] = TSU_ExpXYMoverCreate(0, y + .10, 0, 500);
				self.exit_trigger_list[i] = TSU_DeathCreate(NULL);

				TSU_TriggerAdd((Animation *)self.exit_animation_list[i], (Trigger *)self.exit_trigger_list[i]);
				TSU_DrawableAnimAdd((Drawable *)self.item_game[i], (Animation *)self.exit_animation_list[i]);
			}
		}
	}

	self.wait_to_exit_app = true;
	while (!menu_data.finished_menu)
	{
		thd_pass();
	}
	self.wait_to_exit_app = false;

	RemoveEvent(do_menu_video_event);
	do_menu_video_event = NULL;
	RemoveEvent(do_menu_control_event);
	do_menu_control_event = NULL;

	MenuExitHelper(NULL);
}

static void GamesApp_SystemMenuInputEvent(int type, int key)
{
	if (type != EvtKeypress)
		return;

	mutex_lock(&change_page_mutex);
	SystemMenuResetMouseOver();

	switch (key)
	{
		case KeyStart:
		{	
			if (StateSystemMenu())
			{
				TSU_InputEventStateSetGlobalWindowState(SA_GAMES_MENU);
				ExitSystemMenuClick(NULL);
			}
		}
		break;

		default:
		{			
			if (StateSystemMenu())
			{
				SystemMenuInputEvent(type, key);
			}
		}
		break;
	}

	mutex_unlock(&change_page_mutex);
}

static void GamesApp_PresetMenuInputEvent(int type, int key)
{
	if (type != EvtKeypress)
		return;

	mutex_lock(&change_page_mutex);
	PresetMenuResetMouseOver();

	switch (key)
	{
		case KeyStart:
		case KeyMiscY:
		{	
			if (StatePresetMenu())
			{
				HidePresetMenu();
				TSU_InputEventStateSetGlobalWindowState(SA_GAMES_MENU);
			}
		}
		break;

		default:
		{			
			if (StatePresetMenu())
			{
				PresetMenuInputEvent(type, key);
			}
		}
		break;
	}

	mutex_unlock(&change_page_mutex);
}

static void GamesApp_ScanCoverInputEvent(int type, int key)
{
    if(type!=EvtKeypress || (key!=KeyCancel && key!=KeySelect)) return;
    if(!menu_data.artwork_done) {
        if(key==KeyCancel) {
            menu_data.stop_load_pvr_cover=true;
            SetMessageScan("%s","Stopping after this disc...");
        }
        return;
    }
    mutex_lock(&change_page_mutex);
    StopScanCovers();
    HideCoverScan();
    for(int i=0;i<self.game_count;++i) LoadCover(self.item_game[i],i);
    TSU_InputEventStateSetGlobalWindowState(SA_GAMES_MENU);
    timer_ms_gettime(&self.scan_covers_start_time,NULL);
    mutex_unlock(&change_page_mutex);
}

static void GamesApp_OptimizeCoverInputEvent(int type, int key)
{
	if (type != EvtKeypress)
		return;
	
	mutex_lock(&change_page_mutex);

	StopOptimizeCovers();
	HideOptimizeCoverPopup();
	TSU_InputEventStateSetGlobalWindowState(SA_GAMES_MENU);
	timer_ms_gettime(&self.scan_covers_start_time, NULL);
	
	mutex_unlock(&change_page_mutex);
}

static void GamesApp_ControlInputEvent(int type, int key, int state_app)
{
	if (type != EvtKeypress)
		return;

	switch (state_app)
	{
		case SA_CONTROL + ASYNC_CONTROL_ID: //ASYNC
		{
			AsyncInputEvent(type, key);
			break;
		}

		case SA_CONTROL + OS_CONTROL_ID: //OS
		{
			OSInputEvent(type, key);
			break;
		}

		case SA_CONTROL + LOADER_CONTROL_ID: //LOADER
		{
			LoaderInputEvent(type, key);
			break;
		}

		case SA_CONTROL + BOOT_CONTROL_ID: //BOOT
		{
			BootInputEvent(type, key);
			break;
		}
		
		case SA_CONTROL + MEMORY_CONTROL_ID: //MEMORY
		{
			MemoryInputEvent(type, key);
			break;
		}

		case SA_CONTROL + CUSTOM_MEMORY_CONTROL_ID: //CUSTOM MEMORY
		{
			CustomMemoryInputEvent(type, key);
			break;
		}

		case SA_CONTROL + HEAP_CONTROL_ID: //HEAP
		{
			HeapInputEvent(type, key);
			break;
		}	

		case SA_CONTROL + CDDA_SOURCE_CONTROL_ID: //CDDA SOURCE
		{
			CDDASourceInputEvent(type, key);
			break;
		}	

		case SA_CONTROL + CDDA_DESTINATION_CONTROL_ID: //CDDA DESTINATION
		{
			CDDADestinationInputEvent(type, key);
			break;
		}	

		case SA_CONTROL + CDDA_POSITION_CONTROL_ID: //CDDA POSITION
		{
			CDDAPositionInputEvent(type, key);
			break;
		}	

		case SA_CONTROL + CDDA_CHANNEL_CONTROL_ID: //CDDA CHANNEL
		{
			CDDAChannelInputEvent(type, key);
			break;
		}

		case SA_CONTROL + PATCH_ADDRESS1_CONTROL_ID: //PATCH ADDRESS 1
		{
			PatchAddress1InputEvent(type, key);
			break;
		}

		case SA_CONTROL + PATCH_VALUE1_CONTROL_ID: //PATCH VALUE 1
		{
			PatchValue1InputEvent(type, key);
			break;
		}

		case SA_CONTROL + PATCH_ADDRESS2_CONTROL_ID: //PATCH ADDRESS 2
		{
			PatchAddress2InputEvent(type, key);
			break;
		}

		case SA_CONTROL + PATCH_VALUE2_CONTROL_ID: //PATCH VALUE 2
		{
			PatchValue2InputEvent(type, key);
			break;
		}

		case SA_CONTROL + ALTERBOOT_CONTROL_ID: //ALTER BOOT
		{
			AlterBootInputEvent(type, key);
			break;
		}

		case SA_CONTROL + SCREENSHOT_CONTROL_ID: //SCREENSHOT
		{
			ScreenshotInputEvent(type, key);
			break;
		}

		case SA_CONTROL + VMU_CONTROL_ID: //VMU
		{
			VMUInputEvent(type, key);
			break;
		}

		case SA_CONTROL + VMUSELECTOR_CONTROL_ID: //VMU OPTION
		{
			VMUSelectorInputEvent(type, key);
			break;
		}

		case SA_CONTROL + SHORTCUT_SIZE_CONTROL_ID: //SHORTCUT SIZE
		{
			ShortcutSizeInputEvent(type, key);
			break;
		}

		case SA_CONTROL + SHORTCUT_ROTATE_CONTROL_ID: //SHORTCUT ROTATE
		{
			ShortcutRotateInputEvent(type, key);
			break;
		}

		case SA_CONTROL + SHORTCUT_NAME_CONTROL_ID: //SHORTCUT NAME
		{
			ShortcutNameInputEvent(type, key);
			break;
		}

		case SA_CONTROL + SHORTCUT_DONTSHOWNAME_CONTROL_ID: //SHORTCUT DONT SHOW NAME
		{
			ShortcutDontShowNameInputEvent(type, key);
			break;
		}

		case SA_CONTROL + CATEGORY_CONTROL_ID: //SYSTE MENU CATEGORY
		{
			CategoryInputEvent(type, key);
			break;			
		}

		case SA_CONTROL + THEME_CONTROL_ID: //SYSTEM MENU THEME
		{
			ThemeInputEvent(type, key);
			break;
		}

		case SA_CONTROL + BACKGROUND_COLOR_CONTROL_ID: //SYSTEM MENU BACKGROUND COLOR
		{
			BackgroundColorInputEvent(type, key);
			break;
		}

		case SA_CONTROL + BORDER_COLOR_CONTROL_ID: //SYSTEM MENU CATEGORY BORDER COLOR
		{
			BorderColorInputEvent(type, key);
			break;
		}

		case SA_CONTROL + TITLE_COLOR_CONTROL_ID: //SYSTEM MENU TITLE BACKGROUND COLOR
		{
			TitleColorInputEvent(type, key);
			break;
		}

		case SA_CONTROL + BODY_COLOR_CONTROL_ID: //SYSTEM MENU BODY COLOR
		{
			BodyColorInputEvent(type, key);
			break;
		}

		case SA_CONTROL + AREA_COLOR_CONTROL_ID: //SYSTEM MENU AREA COLOR
		{
			AreaColorInputEvent(type, key);
			break;
		}

		case SA_CONTROL + CONTROL_TOP_COLOR_CONTROL_ID: //SYSTEM MENU TOP CONTROL COLOR
		{
			ControlTopColorInputEvent(type, key);
			break;
		}

		case SA_CONTROL + CONTROL_BODY_COLOR_CONTROL_ID: //SYSTEM MENU BODY CONTROL COLOR
		{
			ControlBodyColorInputEvent(type, key);
			break;
		}

		case SA_CONTROL + CONTROL_BOTTOM_COLOR_CONTROL_ID: //SYSTEM MENU BOTTOM CONTROL COLOR
		{
			ControlBottomColorInputEvent(type, key);
			break;
		}
		
		default:
			break;
	}
}

static void GamesApp_InputEvent(int type, int key)
{
	if (type != EvtKeypress)
		return;
	
	mutex_lock(&change_page_mutex);
	bool skip_cursor = false;
	bool is_playing = menu_data.ffplay && menu_data.ffplay_is_playing();
	float speed_cursor = 7.0f;
	timer_ms_gettime(&self.scan_covers_start_time, NULL);

	if (menu_data.menu_type == MT_PLANE_TEXT)
	{
		speed_cursor = 6.5;
	}
	else if (is_playing)
	{
		goto SkipCommand;
	}

	menu_data.ffmpeg_played = false;

	if(self.game_count<=0 && key!=KeyStart && key!=KeyCancel && key!=KeyMiscX) {
        mutex_unlock(&change_page_mutex); return;
    }

    switch (key)
	{
		case KeyStart:
		{	
			if (self.app->tsunami != NULL)
			{
				if (is_playing)
					menu_data.ffplay_shutdown();

				TSU_InputEventStateSetGlobalWindowState(SA_SYSTEM_MENU);
				ShowSystemMenu();
			}
			skip_cursor = true;			
		}
		break;

		case KeyMiscY:
		{
			if (menu_data.games_array_count > 0)
			{
				if (is_playing)
					menu_data.ffplay_shutdown();

				TSU_InputEventStateSetGlobalWindowState(SA_PRESET_MENU);
				ShowPresetMenu(TSU_ItemMenuGetItemIndex(self.item_game[self.menu_cursel]));
			}
			skip_cursor = true;
		}
		break;

		// X: CHANGE VISUALIZATION
		case KeyMiscX:
		{
			StopShowCover();

			int real_cursel = (self.current_page - 1) * menu_data.menu_option.max_page_size + self.menu_cursel + 1;
			SetMenuType(menu_data.menu_type >= MT_IMAGE_128_4X3 ? MT_PLANE_TEXT : menu_data.menu_type + 1);
			self.current_page = ceil((float)real_cursel / (float)menu_data.menu_option.max_page_size);
			self.menu_cursel = real_cursel - (self.current_page - 1) * menu_data.menu_option.max_page_size - 1;

			if (LoadPage(true, DMD_NONE))
			{
			}
		}
		break;

		case KeyCancel:
		{
			self.exit_app = true;
			skip_cursor = true;
			mutex_unlock(&change_page_mutex);
			StartExit();
			return;
		}
		break;

		// LEFT TRIGGER
		case KeyPgup:
		{
			self.current_page--;
			self.menu_cursel = 0;
			if (LoadPage(false, DMD_RIGHT))
			{
			}
		}
		break;

		// RIGHT TRIGGER
		case KeyPgdn:
		{
			self.current_page++;
			self.menu_cursel = 0;
			if (LoadPage(false, DMD_LEFT))
			{		
			}
		}
		break;

		case KeyUp:
		{
			self.menu_cursel--;
			
			if (menu_data.menu_type == MT_IMAGE_TEXT_64_5X2)
			{
				if (self.menu_cursel < 0)
				{
					self.menu_cursel += self.game_count;
				}
			}
			else if (menu_data.menu_type == MT_PLANE_TEXT)
			{
				if (self.menu_cursel < 0)
				{
					self.current_page--;
					LoadPage(false, DMD_DOWN);
					self.menu_cursel = self.game_count - 1;
				}
			}
			else
			{
				if ((self.menu_cursel + 1) % menu_data.menu_option.size_items_column == 0)
				{
					self.menu_cursel += menu_data.menu_option.size_items_column;
				}

				if (self.menu_cursel < 0)
				{
					self.menu_cursel = 0;
				}
				else if (self.menu_cursel >= self.game_count)
				{
					self.menu_cursel = self.game_count - 1;
				}
			}
		}

		break;

		case KeyDown:
		{
			self.menu_cursel++;

			if (menu_data.menu_type == MT_IMAGE_TEXT_64_5X2)
			{
				if (self.menu_cursel >= self.game_count)
				{
					self.menu_cursel -= self.game_count;
				}
			}
			else if (menu_data.menu_type == MT_PLANE_TEXT)
			{
				if (self.menu_cursel >= self.game_count)
				{
					self.current_page++;
					self.menu_cursel = 0;
					LoadPage(false, DMD_UP);
				}
			}
			else
			{
				if (self.menu_cursel % menu_data.menu_option.size_items_column == 0)
				{
					self.menu_cursel -= menu_data.menu_option.size_items_column;
				}

				if (self.menu_cursel >= self.game_count)
				{
					self.menu_cursel = self.game_count - 1;
				}
			}
		}

		break;

		case KeyLeft:

			if (menu_data.menu_type == MT_PLANE_TEXT)
			{
				self.current_page--;
				self.menu_cursel = 0;
				if (LoadPage(false, DMD_RIGHT))
				{
				}
			}
			else
			{
				if (self.game_count <= menu_data.menu_option.size_items_column)
				{
					self.menu_cursel = 0;

					if (menu_data.change_page_with_pad)
					{
						self.current_page--;
						self.menu_cursel = 0;
						if (LoadPage(false, DMD_RIGHT))
						{
						}
					}
				}
				else
				{
					self.menu_cursel -= menu_data.menu_option.size_items_column;

					if (self.menu_cursel < 0)
					{
						if (menu_data.change_page_with_pad)
						{
							self.current_page--;
							self.menu_cursel = 0;
							if (LoadPage(false, DMD_RIGHT))
							{
							}
						}
						else
						{
							self.menu_cursel += self.game_count;
						}
					}
				}
			}

			break;

		case KeyRight:
			if (menu_data.menu_type == MT_PLANE_TEXT)
			{
				self.current_page++;
				self.menu_cursel = 0;
				if (LoadPage(false, DMD_LEFT))
				{
				}
			}
			else
			{
				if (self.game_count <= menu_data.menu_option.size_items_column)
				{
					self.menu_cursel = self.game_count - 1;
					
					if (menu_data.change_page_with_pad)
					{
						self.current_page++;
						self.menu_cursel = 0;
						if (LoadPage(false, DMD_LEFT))
						{
						}
					}
				}
				else
				{
					self.menu_cursel += menu_data.menu_option.size_items_column;

					if (self.menu_cursel >= self.game_count)
					{
						if (menu_data.change_page_with_pad)
						{
							self.current_page++;
							self.menu_cursel = 0;
							if (LoadPage(false, DMD_LEFT))
							{
							}
						}
						else
						{
							self.menu_cursel -= self.game_count;
						}
					}
				}
			}

			break;

		case KeySelect:
		{
			mutex_unlock(&change_page_mutex);
			StartExit();
			return;
		}
		break;

		default:
			{
			}
			break;
	}

	SkipCommand:
	if (!skip_cursor)
	{	
		self.game_changed = true;
		
		if (key != KeyMiscX || is_playing)
			StopCDDA();
		
		for (int i = 0; i < self.game_count; i++)
		{
			if (self.item_game[i] != NULL)
			{
				if (i == self.menu_cursel)
				{
					TSU_ItemMenuSetSelected(self.item_game[i], true, false);
					SetTitle(TSU_ItemMenuGetItemIndex(self.item_game[i]), TSU_ItemMenuGetItemValue(self.item_game[i]), true);

					SetTitleType(GetFullGamePathByIndex(TSU_ItemMenuGetItemIndex(self.item_game[i]))
						, CheckGdiOptimized(TSU_ItemMenuGetItemIndex(self.item_game[i])));

					SetCursor();

					TSU_LogXYMoverSetFactor(self.item_selector_animation, speed_cursor);

					ShowCover(TSU_ItemMenuGetItemIndex(self.item_game[i]));

					if (key != KeyMiscX)
						PlayCDDA(TSU_ItemMenuGetItemIndex(self.item_game[i]));
				}
				else
				{
					TSU_ItemMenuSetSelected(self.item_game[i], false, false);
				}
			}
		}
	}

	mutex_unlock(&change_page_mutex);
}

static void WriteLaunchReport(const char *stage)
{
    char report[1536], path[NAME_MAX];
    const isoldr_info_t *info = self.isoldr;
    int length = snprintf(report, sizeof(report),
        "Games Menu 1.0.2 / TPMJB\nGame: %s\nStage: %s\n",
        self.item_value_selected, stage);
    if(info && length > 0 && length < (int)sizeof(report)) {
        int extra = snprintf(report + length, sizeof(report) - length,
            "Loader: %08lx\nDevice: %s partition %lu\n"
            "Executable: %s bytes: %lu\nType: %lu mode: %lu\n"
            "DMA: %lu async: %lu altread: %lu\n"
            "CDDA: %08lx VMU: %lu IRQ: %lu\nHeap: %08lx low-level: %s\n"
            "Executable CRC: %08lx\n",
            (unsigned long)self.addr, info->fs_dev, info->fs_part,
            info->exec.file, info->exec.size, info->exec.type, info->boot_mode,
            info->use_dma, info->emu_async, info->alt_read, info->emu_cdda,
            info->emu_vmu, info->use_irq, info->heap,
            info->syscalls ? "on" : "off", info->boot_crc32);
        if(extra < 0) return;
        length += extra;
    }
    if(length <= 0 || length >= (int)sizeof(report) ||
       snprintf(path, sizeof(path), "%s/apps/games_menu/last-launch.txt", getenv("PATH")) >= (int)sizeof(path))
        return;
    file_t fd = fs_open(path, O_CREAT | O_TRUNC | O_WRONLY);
    if(fd != FILEHND_INVALID) {
        int ok = fs_write(fd, report, length) == length;
        if(fs_close(fd) < 0) ok = 0;
        if(!ok) ds_printf("DS_WARNING: Could not finish saving the launch report.\n");
    }
}

static int LoadPreset()
{
	if (menu_data.preset == NULL || menu_data.preset->game_index != self.game_index_selected)
	{
		if (menu_data.preset != NULL)
		{
			free(menu_data.preset);
		}

		menu_data.preset = LoadPresetGame(self.game_index_selected, false);
	}

	if ((self.isoldr = ParsePresetToIsoldr(self.game_index_selected, menu_data.preset)) == NULL)
	{
		return 0;
	}

	char memory[12];
	memset(memory, 0, sizeof(memory));

	if (strcasecmp(menu_data.preset->memory, "0x8c") == 0)
	{
		snprintf(memory, sizeof(memory), "%s%s", menu_data.preset->memory, menu_data.preset->custom_memory);
	}
	else
	{
		strcpy(memory, menu_data.preset->memory);
	}

	self.addr = strtoul(memory, NULL, 16);

	if (menu_data.preset->emu_vmu)
	{
		GenerateVMUFile(self.item_value_selected, menu_data.preset->vmu_mode, menu_data.preset->emu_vmu);
	}

    if(self.isoldr->exec.type == BIN_TYPE_WINCE)
        self.addr = ISOLDR_DEFAULT_ADDR_MIN;

    const char *game_path = GetFullGamePathByIndex(self.game_index_selected);
    int checked = self.isoldr->image_type == IMAGE_TYPE_ROM_NAOMI || self.isoldr->bleem ?
        0 : isoldr_check_boot(self.isoldr, game_path);
    WriteLaunchReport(checked < 0 ? isoldr_get_last_error() :
                      "Executable check passed; handoff not yet attempted");
    if(checked < 0) return 0;

	return 1;
}

static void FreeAppData()
{
    NextDestroyChrome();
	mutex_destroy(&change_page_mutex);	

	for (int i = 0; i < MAX_SIZE_ITEMS; i++)
	{
		if (self.item_game[i] != NULL)
		{
			if (!self.exit_app && TSU_ItemMenuIsSelected(self.item_game[i]) && self.run_animation != NULL)
			{
				TSU_AnimationComplete((Animation *)self.run_animation, (Drawable *)self.item_game[i]);			
			}
			else if (self.exit_animation_list[i] != NULL)
			{
				TSU_AnimationComplete((Animation *)self.exit_animation_list[i], (Drawable *)self.item_game[i]);
			}

			TSU_DrawableSetFinished((Drawable *)self.item_game[i]);
		}
	}	

	TSU_DrawableSubRemoveFinished((Drawable *)self.scene_ptr);
	thd_pass();

	for (int i = 0; i < MAX_SIZE_ITEMS; i++)
	{
		if (self.item_game[i] != NULL)
		{
			if (TSU_ItemMenuIsSelected(self.item_game[i]) && self.run_animation != NULL)
			{
				TSU_LogXYMoverDestroy(&self.run_animation);
			}
			else if (self.exit_animation_list[i] != NULL)
			{
				TSU_ExpXYMoverDestroy(&self.exit_animation_list[i]);
			}

			if (self.item_game_animation[i] != NULL)
			{
				TSU_LogXYMoverDestroy(&self.item_game_animation[i]);
			}

			if (self.exit_trigger_list[i] != NULL)
			{
				TSU_DeathDestroy(&self.exit_trigger_list[i]);
			}

			if (self.item_game[i] != NULL)
			{
				TSU_ItemMenuDestroy(&self.item_game[i]);
			}
		}
	}

	if (self.main_box != NULL)
	{
		TSU_BoxDestroy(&self.main_box);
	}

	if (self.area_rectangle != NULL)
	{
		TSU_RectangleDestroy(&self.area_rectangle);
	}

	if (self.title_rectangle != NULL)
	{
		TSU_RectangleDestroy(&self.title_rectangle);
	}

	if (self.title_background_rectangle != NULL)
	{
		TSU_RectangleDestroy(&self.title_background_rectangle);
	}

	if (self.title_type_rectangle != NULL)
	{
		TSU_RectangleDestroy(&self.title_type_rectangle);
	}

	if (self.menu_options_rectangle != NULL)
	{
		TSU_RectangleDestroy(&self.menu_options_rectangle);
	}

	if (self.game_list_rectangle != NULL)
	{
		TSU_RectangleDestroy(&self.game_list_rectangle);
	}

	if (self.action_left_button != NULL)
	{
		TSU_ItemMenuDestroy(&self.action_left_button);
	}

	if (self.action_right_button != NULL)
	{
		TSU_ItemMenuDestroy(&self.action_right_button);
	}

	if (self.action_view_button != NULL)
	{
		TSU_ItemMenuDestroy(&self.action_view_button);
	}

	if (self.page_label != NULL)
	{
		TSU_LabelDestroy(&self.page_label);
	}

	if (self.total_label != NULL)
	{
		TSU_LabelDestroy(&self.total_label);		
	}

	if (self.item_selector != NULL)
	{
		TSU_RectangleDestroy(&self.item_selector);
	}

	if (self.title != NULL)
	{
		TSU_LabelDestroy(&self.title);
	}

	if (self.title_animation != NULL)
	{
		TSU_LogXYMoverDestroy(&self.title_animation);
	}

	if (self.title_type != NULL)
	{
		TSU_LabelDestroy(&self.title_type);
	}

	if (self.title_type_animation != NULL)
	{
		TSU_LogXYMoverDestroy(&self.title_type_animation);
	}

	if (self.item_selector_animation != NULL)
	{
		TSU_LogXYMoverDestroy(&self.item_selector_animation);
	}

	if (self.animation_cover_game != NULL)
	{
		TSU_FadeToDestroy(&self.animation_cover_game);		
	}

	if (self.fadeout_cover_animation != NULL)
	{
		TSU_FadeOutDestroy(&self.fadeout_cover_animation);		
	}

	if (self.img_cover_game != NULL)
	{
		TSU_BannerDestroy(&self.img_cover_game);
		TSU_TextureDestroy(&self.texture_cover_game);
	}

	if (self.img_cover_game_background != NULL)
	{
		TSU_BannerDestroy(&self.img_cover_game_background);
		TSU_TextureDestroy(&self.texture_cover_game_background);
	}

	for (int i = 0; i < MAX_BUTTONS; i++)
	{
		if (self.item_button[i] != NULL)
		{
			TSU_ItemMenuDestroy(&self.item_button[i]);
		}
	}

	if (self.img_cover_game_rectangle != NULL)
	{
		TSU_RectangleDestroy(&self.img_cover_game_rectangle);
	}

	DestroySystemMenu();
	DestroyPresetMenu();

	TSU_FontDestroy(&self.menu_font);
	self.scene_ptr = NULL;

	DestroyMenuData();
}

/* Returns whether the menu was released. A successful handoff never returns. */
static bool PlayGame(void)
{
	bool menu_released = false;

	if (self.item_value_selected[0] != '\0')
	{
		menu_data.last_device = self.device_selected;
		if (menu_data.games_array[self.game_index_selected].is_folder_name)
		{
			strcpy(menu_data.last_game, menu_data.games_array[self.game_index_selected].folder_name);
		}
		else
		{
			strcpy(menu_data.last_game, menu_data.games_array[self.game_index_selected].game);
		}

		ds_printf("DS_GAMES: Run: %s\n", self.item_value_selected);

		if (LoadPreset() == 1)
		{
			ds_printf("DS_GAMES: Preset loaded.\n");
			SaveMenuConfig();
			FreeAppData();
            menu_released = true;
			isoldr_exec(self.isoldr, self.addr);
		}
	}

    const char *error = isoldr_get_last_error();
    if(!error || !error[0]) error = "Game launch returned without an error message.";
    WriteLaunchReport(error);
    ds_printf("DS_ERROR: Game launch failed before handoff.\n%s\n", error);
	return menu_released;
}

static void PostOptimizer()
{
	HideOptimizeCoverPopup();
	TSU_InputEventStateSetGlobalWindowState(SA_GAMES_MENU);
	menu_data.optimize_game_cover_thread = NULL;
}

static void PostLoadPVRCover(bool new_cover)
{
    (void)new_cover;
    FinishCoverScan();
    /* Keep the thread handle until dismissal joins it. */
    menu_data.artwork_done=true;
}

static void ResetMouseOver()
{
	self.over_drawable_ptr = NULL;
	self.over_object_type = 0;
	self.last_over_drawable_ptr = NULL;
}

static void ClearMouseOverDispatch(void)
{
	switch(TSU_InputEventStateGetGlobalWindowState())
	{
		case SA_GAMES_MENU:
			self.over_drawable_ptr = NULL;
			self.over_object_type = 0;
			break;

		case SA_PRESET_MENU:
			PresetMenuClearMouseOver();
			break;

		case SA_SYSTEM_MENU:
			SystemMenuClearMouseOver();
			break;

		default:
			break;
	}
}

static bool IsActionButton(Drawable *drawable) { (void)drawable; return false; }

static void DoMenuMouseMotionHandler(SDL_Event *event)
{
    int state=TSU_InputEventStateGetGlobalWindowState();
    if(state==SA_PRESET_MENU) { PresetMenuOnMouseOver(); return; }
    if(state==SA_SYSTEM_MENU) { SystemMenuOnMouseOver(); return; }
    if(state!=SA_GAMES_MENU) return;
    int x=event->motion.x, y=event->motion.y;
    if(x==library.mouse_x && y==library.mouse_y) return;
    library.mouse_x=x; library.mouse_y=y;
    NextFocus(NextActionAt(x,y));
    if(library.focus>=0) return;
    for(int i=0;i<self.game_count;++i) {
        if(i==self.menu_cursel || !NextInside(NextGameRect(menu_data.menu_type,i),x,y)) continue;
        mutex_lock(&change_page_mutex);
        self.game_changed=true;
        StopCDDA();
        TSU_ItemMenuSetSelected(self.item_game[self.menu_cursel],false,false);
        self.menu_cursel=i;
        ItemMenu *item=self.item_game[i];
        int index=TSU_ItemMenuGetItemIndex(item);
        TSU_ItemMenuSetSelected(item,true,false);
        SetTitle(index,TSU_ItemMenuGetItemValue(item),true);
        SetTitleType(GetFullGamePathByIndex(index),CheckGdiOptimized(index));
        SetCursor(); ShowCover(index); PlayCDDA(index);
        mutex_unlock(&change_page_mutex);
        break;
    }
}

static void StateAppInpuEvent(int state_app, int type, int key)
{
	switch (state_app)
	{

        case SA_GAMES_MENU:
        {
            if(key==KeyStart || (library.focus>=0 && key!=KeySelect)) {
                NextFocus(NextFocusKey(library.focus,key));
                break;
            }
            if(key==KeySelect && library.focus>=0) {
                int action=library.focus;
                NextFocus(-1);
                switch(action) {
                    case NEXT_PLAY: GamesApp_InputEvent(type,KeySelect); break;
                    case NEXT_PRESET: GamesApp_InputEvent(type,KeyMiscY); break;
                    case NEXT_SCAN:
                        self.game_changed=true;
                        StopShowCover(); StopCDDA();
                        ScanMissingCoversClick(NULL);
                        break;
                    case NEXT_VIEW: GamesApp_InputEvent(type,KeyMiscX); break;
                    case NEXT_SETTINGS: GamesApp_InputEvent(type,KeyStart); break;
                    case NEXT_EXIT: GamesApp_InputEvent(type,KeyCancel); break;
                }
            } else GamesApp_InputEvent(type,key);
            break;
        }

		case SA_SYSTEM_MENU:
			GamesApp_SystemMenuInputEvent(type, key);
			break;

		case SA_PRESET_MENU:
			GamesApp_PresetMenuInputEvent(type, key);
			break;

		case SA_SCAN_COVER:
			GamesApp_ScanCoverInputEvent(type, key);
			break;

		case SA_OPTIMIZE_COVER:
			GamesApp_OptimizeCoverInputEvent(type, key);
			break;

		default:
			if (state_app >= SA_CONTROL) {
				GamesApp_ControlInputEvent(type, key, state_app);
			}
			break;
	}
}

static void DoMenuControlHandler(void *ds_event, void *param, int action)
{
	SDL_Event *event = (SDL_Event *) param;

	(void)ds_event;

	if(action != EVENT_ACTION_UPDATE) {
		return;
	}

    int duplicate=library.mouse_duplicate;
    library.mouse_duplicate=0;
    if(duplicate && event->type==duplicate && event->button.button==library.mouse_button) return;
    if(event->type==SDL_JOYBUTTONDOWN || event->type==SDL_JOYBUTTONUP) {
        if(event->jbutton.button==SDL_DC_A || event->jbutton.button==SDL_DC_B) {
            library.mouse_duplicate=event->type==SDL_JOYBUTTONDOWN?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;
            library.mouse_button=event->jbutton.button==SDL_DC_A?SDL_BUTTON_LEFT:SDL_BUTTON_RIGHT;
        }
    }

	switch(event->type) {

		case SDL_MOUSEMOTION:
			ClearMouseOverDispatch();
			TSU_AppDoMouse(self.app->tsunami, event->motion.x, event->motion.y);
			DoMenuMouseMotionHandler(event);
			break;

        case SDL_MOUSEBUTTONUP: {
            int state=TSU_InputEventStateGetGlobalWindowState();
            if(event->button.button==SDL_BUTTON_RIGHT) {
                StateAppInpuEvent(state,EvtKeypress,KeyCancel);
            } else if(event->button.button==SDL_BUTTON_LEFT) {
                int x=event->button.x,y=event->button.y;
                if(state==SA_GAMES_MENU) {
                    int action=NextActionAt(x,y);
                    if(action>=0) {
                        NextFocus(action);
                        StateAppInpuEvent(state,EvtKeypress,KeySelect);
                    } else {
                        for(int i=0;i<self.game_count;++i) {
                            if(i==self.menu_cursel && NextInside(NextGameRect(menu_data.menu_type,i),x,y)) {
                                NextFocus(-1);
                                StateAppInpuEvent(state,EvtKeypress,KeySelect);
                                break;
                            }
                        }
                    }
                } else if(state==SA_SCAN_COVER) {
                    if(NextInside((NextRect){80,300,480,40},x,y))
                        StateAppInpuEvent(state,EvtKeypress,menu_data.artwork_done?KeySelect:KeyCancel);
                } else StateAppInpuEvent(state,EvtKeypress,KeySelect);
            }
            break;
        }

		case SDL_JOYBUTTONDOWN: {
			switch(event->jbutton.button) {
				case SDL_DC_B: // B
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyCancel);
					break;

				case SDL_DC_A: // A				
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeySelect);
					break;

				case SDL_DC_Y: // Y
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyMiscY);
					break;

				case SDL_DC_X: // X
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyMiscX);
					break;

				case SDL_DC_Z: // Z
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyPgup);
					break;

				case SDL_DC_C: // C
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyPgdn);
					break;

				case SDL_DC_START: // START
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyStart);
					break;

				case SDL_DC_L:
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyPgup);
					break;

				case SDL_DC_R:
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyPgdn);
					break;

				default:
					break;
			}
		}
		break;

		case SDL_JOYHATMOTION: {
			if (event->jhat.hat) { // skip second d-pad
				break;
			}

			switch(event->jhat.value) {
				case SDL_HAT_UP: // KEY UP
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyUp);
					break;

				case SDL_HAT_DOWN: // KEY DOWN
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyDown);
					break;

				case SDL_HAT_LEFT: // KEY LEFT
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyLeft);
					break;

				case SDL_HAT_RIGHT: // KEY RIGHT
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyRight);
					break;

				default:
					break;
			}
		}
		break;

		case SDL_KEYDOWN: {
			switch (event->key.keysym.sym) {
				case SDLK_UP:
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyUp);
					break;
				
				case SDLK_DOWN:
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyDown);
					break;

				case SDLK_RIGHT:
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyRight);
					break;

				case SDLK_LEFT:
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyLeft);
					break;

				case SDLK_PAGEUP:
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyPgup);
					break;
				
				case SDLK_PAGEDOWN:
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyPgdn);
					break;

				case SDLK_RETURN:
				case SDLK_KP_ENTER:
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeySelect);
					break;

				case SDLK_BACKSPACE:
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyCancel);
					break;

				case SDLK_RMETA:
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyStart);
					break;
				
				case SDLK_SPACE:
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyMiscY);
					break;

				case SDLK_TAB:
					StateAppInpuEvent(TSU_InputEventStateGetGlobalWindowState(), EvtKeypress, KeyMiscX);
					break;

				case SDLK_F4:
					if (  event->key.keysym.mod & KMOD_ALT &&
						!(event->key.keysym.mod & (KMOD_CTRL | KMOD_SHIFT))) {
						self.exit_app = true;
						StartExit();
					}
					break;

				default:
					break;
			}
			
		}
		break;

		default:
			break;
	}
}

static void GamesApp_DrawOpaquePolyEvent()
{
}

static void GamesApp_DrawTransparentPolyEvent()
{
	SDL_DS_Blit_Cursor();
	
}

static void DoMenuVideoHandler(void *ds_event, void *param, int action)
{
	if (action != EVENT_ACTION_RENDER)
	{
		return;
	}

	if (!self.wait_to_exit_app)
	{
		if (menu_data.menu_type != MT_PLANE_TEXT
			&& menu_data.ffplay && menu_data.ffplay_is_playing())
		{
			return;
		}

		return;
	}

	if (!menu_data.finished_menu && ExitAnimationsFinished())
	{
		menu_data.finished_menu = true;
	}
}

static void* MenuExitHelper(void *params)
{
    (void)params;
	if (self.app != NULL && self.app->tsunami != NULL)
	{
		if (!self.exit_app)
		{
            bool menu_released = PlayGame();
            if (!menu_released)
                FreeAppData();
            /* isoldr_exec performs its own successful handoff. Reaching here
             * is failure: keep DreamShell running and show the actual error. */
            EnableScreen();
            GUI_Enable();
            OpenMainApp();
            ShowConsole();
		}
		else
		{
			FreeAppData();
			OpenMainApp();
		}
	}

	return NULL;
}

static void CreateMainView(void) { NextCreateChrome(); }

static void RefreshMainView(void)
{
    CreateMainView();
    RefreshTotal();
}

void GamesApp_Init(App_t *app)
{
	srand(time(NULL));
	mutex_init((mutex_t *)&change_page_mutex, MUTEX_TYPE_NORMAL);

	memset(&self, 0, sizeof(self));

	if (app->args != NULL)
	{
		self.have_args = true;
	}
	else
	{
		self.have_args = false;
	}

	self.app = app;
	self.app->thd = NULL;
	self.sector_size = 2048;
	self.pages = -1;
	self.first_menu_load = true;

	memset(self.item_value_selected, 0, sizeof(self.item_value_selected));

	for (int i = 0; i < MAX_BUTTONS; i++)
	{
		self.item_button[i] = NULL;
	}

	self.run_animation = NULL;
	for (int i = 0; i < MAX_SIZE_ITEMS; i++)
	{
		self.item_game[i] = NULL;
		self.item_game_animation[i] = NULL;
		self.exit_animation_list[i] = NULL;
		self.exit_trigger_list[i] = NULL;
	}

	if (app->tsunami != NULL)
	{
		TSU_InputEventStateSetGlobalWindowState(SA_GAMES_MENU);

		TSU_AppSetDrawOpaquePolyEvent(app->tsunami, GamesApp_DrawOpaquePolyEvent);
		TSU_AppSetDrawTransparentPolyEvent(app->tsunami, GamesApp_DrawTransparentPolyEvent);

		self.scene_ptr = TSU_AppGetScene(app->tsunami);

		CreateMenuData(&SetMessageScan, &SetMessageOptimizer, &PostLoadPVRCover, &PostOptimizer);

        if(!strcmp(GetNameCurrentTheme(),DEFAULT_THEME)) SetTheme(NEXT_THEME);
		InitMenu();

		CreateSystemMenu(app->tsunami, self.scene_ptr, self.menu_font, self.menu_font, &RefreshMainView, &ReloadPage);
		CreatePresetMenu(app->tsunami, self.scene_ptr, self.menu_font, self.menu_font);
	}
}

void GamesApp_Open(App_t *app)
{
	(void)app;

	if (self.app == NULL || self.app->tsunami == NULL)
	{
		return;
	}

	SDL_DC_EmulateMouse(SDL_TRUE);

	do_menu_control_event = AddEvent
	(
		"GamesControl_Event",
		EVENT_TYPE_INPUT,
		EVENT_PRIO_DEFAULT,
		DoMenuControlHandler,
		NULL
	);

	do_menu_video_event = AddEvent
	(
		"GamesVideo_Event",
		EVENT_TYPE_VIDEO,
		EVENT_PRIO_DEFAULT,
		DoMenuVideoHandler,
		NULL
	);
}

void GamesApp_Shutdown(App_t *app)
{
	(void)app;

	if (do_menu_control_event != NULL)
	{
		RemoveEvent(do_menu_control_event);
		do_menu_control_event = NULL;
	}

	if (do_menu_video_event != NULL)
	{
		RemoveEvent(do_menu_video_event);
		do_menu_video_event = NULL;
	}

	if (self.isoldr)
	{
		free(self.isoldr);
		self.isoldr = NULL;
	}
}
