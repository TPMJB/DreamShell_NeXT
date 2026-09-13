/* Run the production input dispatcher and keyboard together, in both app load orders. */
#include "input_shim/ds.h"
#include "../../src/events.c"
#include "../../modules/vkb/vkb.c"

static GUI_Widget field = {.limit=120,.refs=1,.type=WIDGET_TYPE_TEXTENTRY}, *focus;
static SDL_PixelFormat format;
static SDL_Surface screen = {.format=&format,.w=640,.h=480};
static uint64_t now;
static int app_calls, gui_calls, blur_calls, mutations, console_visible, console_keys;
static int font_count, surface_count, fail_font;
static unsigned next_id;
static FILE *drawing;
struct _TTF_Font { int size; };
Item_list_t *listMake(void) { return calloc(1,sizeof(Item_list_t)); }
Item_t *listGetItemFirst(Item_list_t *l) {return l->first;}
Item_t *listGetItemNext(Item_t *i) {return i->next;}
Item_t *listGetItemById(Item_list_t *l,uint32_t id) { for(Item_t *i=l->first;i;i=i->next)if(i->id==id)return i; return NULL; }
Item_t *listGetItemByName(Item_list_t *l,const char *name) { for(Item_t *i=l->first;i;i=i->next)if(!strcmp(i->name,name))return i; return NULL; }
Item_t *listAddItem(Item_list_t *l,int type,const char *n,void *data,size_t size) {
    (void)type;(void)size;Item_t *i=calloc(1,sizeof(*i));*i=(Item_t){++next_id,data,n,l->first};l->first=i;return i;
}
void listRemoveItem(Item_list_t *l,Item_t *item,listFreeItemFunc *f) {
    Item_t **p=&l->first;while(*p && *p!=item)p=&(*p)->next;assert(*p);*p=item->next;f(item->data);free(item);
}
void listDestroy(Item_list_t *l,listFreeItemFunc *f) {while(l->first)listRemoveItem(l,l->first,f);free(l);}
void LockVideo(void) {}
void UnlockVideo(void) {}
void ScreenChanged(void) {}
int ScreenUpdated(void) {return 1;}
SDL_Surface *GetScreen(void) {return &screen;}
uint64_t timer_ms_gettime64(void) {return now;}
void ds_printf(const char *s,...) {(void)s;}
GUI_Screen *GUI_GetScreen(void) {return &field;}
GUI_Widget *GUI_ScreenGetFocusWidget(GUI_Screen *s) {(void)s;return focus;}
int GUI_ScreenGetJoySelectState(GUI_Screen *s) {(void)s;return 0;}
int GUI_WidgetGetType(GUI_Widget *w) {return w->type;}
const char *GUI_TextEntryGetText(GUI_Widget *w) {return w->text;}
void GUI_TextEntrySetText(GUI_Widget *w,const char *s) {if(strlen(s)<=w->limit){strcpy(w->text,s);mutations++;}}
void GUI_WidgetClicked(GUI_Widget *w,int x,int y) {(void)x;(void)y;assert(w==focus);focus=NULL;blur_calls++;}
void GUI_ObjectIncRef(GUI_Object *w) {w->refs++;}
int GUI_ObjectDecRef(GUI_Object *w) {return --w->refs;}
ConsoleInformation *GetConsole(void) {static ConsoleInformation c={&screen,0};return &c;}
int ConsoleIsVisible(void) {return console_visible;}
SDL_Event *CON_Events(SDL_Event *e) {console_keys++;return e;}
void SDL_DC_EmulateMouse(SDL_bool v) {(void)v;}
TTF_Font *TTF_OpenFont(const char *p,int size) {(void)p;if(fail_font)return NULL;TTF_Font *f=calloc(1,sizeof(*f));f->size=size;font_count++;return f;}
void TTF_CloseFont(TTF_Font *f) {free(f);font_count--;}
int TTF_SizeText(TTF_Font *f,const char *s,int *w,int *h) { *w=strlen(s)*f->size/2;*h=f->size;return 0; }
SDL_Surface *TTF_RenderText_Blended(TTF_Font *f,const char *s,SDL_Color c) {
    (void)c;SDL_Surface *out=calloc(1,sizeof(*out));out->format=&format;TTF_SizeText(f,s,&out->w,&out->h);surface_count++;
    if(drawing)fprintf(drawing,"T %d %s\n",f->size,s);
    return out;
}
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags,int w,int h,int depth,Uint32 r,Uint32 g,Uint32 b,Uint32 a) {
    (void)flags;(void)depth;(void)r;(void)g;(void)b;(void)a;
    SDL_Surface *s=calloc(1,sizeof(*s));s->format=&format;s->w=w;s->h=h;surface_count++;return s;
}
void SDL_FreeSurface(SDL_Surface *s) {free(s);surface_count--;}
Uint32 SDL_MapRGB(const SDL_PixelFormat *f,Uint8 r,Uint8 g,Uint8 b) {(void)f;return (r<<16)|(g<<8)|b;}
int SDL_FillRect(SDL_Surface *s,SDL_Rect *r,Uint32 color) {
    (void)s;if(drawing)fprintf(drawing,"R %d %d %d %d %06x\n",r?r->x:0,r?r->y:0,r?r->w:608,r?r->h:296,color);return 0;
}
void SDL_GetClipRect(SDL_Surface *s,SDL_Rect *r) {(void)s;*r=(SDL_Rect){0,0,608,296};}
SDL_bool SDL_SetClipRect(SDL_Surface *s,const SDL_Rect *r) {(void)s;(void)r;return SDL_TRUE;}
int SDL_UpperBlit(SDL_Surface *a,SDL_Rect *b,SDL_Surface *c,SDL_Rect *d) {(void)a;(void)b;(void)c;if(drawing)fprintf(drawing,"B %d %d\n",d->x,d->y);return 0;}
static void gui(void *a,void *p,int b) {(void)a;(void)b;(void)p;gui_calls++;}
static void app(void *a,void *p,int b) {(void)a;(void)b;(void)p;app_calls++;}
static void dispatch(SDL_Event e) {ProcessInputEvents(&e);}
static void button(int key) {SDL_Event e={.type=SDL_JOYBUTTONDOWN};e.jbutton.button=key;dispatch(e);}
static void physical(int sym,int ch) {SDL_Event e={.type=SDL_KEYDOWN};e.key.keysym.sym=sym;e.key.keysym.unicode=ch;dispatch(e);}
static void open_field(const char *s) {strcpy(field.text,s);focus=&field;dispatch((SDL_Event){.type=DS_SHOW_VKB_EVENT});assert(vkb.visible);}
int main(int argc,char **argv) {
    assert(argc>=2);setenv("PATH","resources",1);assert(!InitEvents());
    AddEvent("GUI_Input",EVENT_TYPE_INPUT,EVENT_PRIO_DEFAULT,gui,NULL);
    assert(!VirtKeyboardInit());assert(!VirtKeyboardInit());
    AddEvent("AppInput",EVENT_TYPE_INPUT,EVENT_PRIO_DEFAULT,app,NULL); /* Newer than keyboard. */
    if(!strcmp(argv[1],"typing")) {
        open_field("");button(SDL_DC_A);assert(!strcmp(field.text,"q") && mutations==1);
        dispatch((SDL_Event){.type=SDL_JOYBUTTONUP});
        dispatch((SDL_Event){.type=SDL_JOYHATMOTION});assert(!strcmp(field.text,"q"));
        physical(SDLK_z,'z');assert(!strcmp(field.text,"qz") && mutations==2);
        assert(!app_calls && !gui_calls);
        physical(SDLK_LEFT,0);physical(SDLK_x,'x');assert(!strcmp(field.text,"qxz"));
        physical(SDLK_DELETE,0);assert(!strcmp(field.text,"qx"));
        physical(SDLK_BACKSPACE,0);assert(!strcmp(field.text,"q"));
        button(SDL_DC_Y);button(SDL_DC_A);assert(!strcmp(field.text,"qQ"));
        activate(KEY_PAGE);button(SDL_DC_A);assert(!strcmp(field.text,"qQ!"));
        button(SDL_DC_START);assert(!vkb.visible && !focus && blur_calls==1 && field.refs==1);
        physical(SDLK_z,'z');assert(app_calls==1 && gui_calls==1);
        open_field("original");physical(SDLK_a,'a');button(SDL_DC_B);assert(!strcmp(field.text,"original") && blur_calls==2);
    } else if(!strcmp(argv[1],"capacity")) {
        field.limit=4;open_field("123");physical(SDLK_a,'a');physical(SDLK_b,'b');
        assert(!strcmp(field.text,"123a") && !strcmp(vkb.text,"123a") && vkb.cursor==4);
        physical(SDLK_HOME,0);physical(SDLK_DELETE,0);physical(SDLK_z,'z');assert(!strcmp(field.text,"z23a"));
        VirtKeyboardHide();assert(field.refs==1);
    } else if(!strcmp(argv[1],"navigation")) {
        open_field("");SDL_Event e={.type=SDL_JOYHATMOTION};e.jhat.value=SDL_HAT_RIGHT;dispatch(e);
        assert(vkb.selected==1);now=349;keyboard_draw(NULL,NULL,EVENT_ACTION_RENDER);assert(vkb.selected==1);
        now=350;keyboard_draw(NULL,NULL,EVENT_ACTION_RENDER);assert(vkb.selected==2);
        e.jhat.value=0;dispatch(e);now=600;keyboard_draw(NULL,NULL,EVENT_ACTION_RENDER);assert(vkb.selected==2);
        for(int i=0;i<200;i++){navigate(i%4+1);assert(vkb.selected>=0 && vkb.selected<VKB_KEYS);}
        assert(!field.text[0]);
        for(int i=0;i<VKB_KEYS;i++){Key *k=&vkb.keys[i];assert(k->rect.x>=0 && k->rect.x+k->rect.w<=608 && k->rect.y+k->rect.h<=296);}
        SDL_Event m={.type=SDL_MOUSEBUTTONDOWN};m.button.button=SDL_BUTTON_LEFT;m.button.x=30;m.button.y=254;dispatch(m);
        m.type=SDL_MOUSEBUTTONUP;dispatch(m);assert(!strcmp(field.text,"q"));
    } else if(!strcmp(argv[1],"lifecycle")) {
        fail_font=1;focus=&field;VirtKeyboardShow();assert(!vkb.visible && !font_count && !surface_count);fail_font=0;
        open_field("saved");focus=NULL;physical(SDLK_a,'a');assert(!vkb.visible && field.refs==1 && !strcmp(field.text,"saved"));
        VirtKeyboardShutdown();assert(!font_count && !surface_count);
        assert(!VirtKeyboardInit()); /* Now overlay is newer than the app. */
        open_field("");button(SDL_DC_A);assert(!strcmp(field.text,"q"));VirtKeyboardHide();
        focus=NULL;console_visible=1;VirtKeyboardShow();physical(SDLK_a,'a');physical(SDLK_b,'b');
        button(SDL_DC_B);assert(!console_keys);
        VirtKeyboardShow();physical(SDLK_a,'a');button(SDL_DC_START);assert(console_keys==2);
        console_visible=0;open_field("shutdown");ShutdownEvents();
    } else if(!strcmp(argv[1],"preview")) {
        drawing=stdout;open_field("ELEMENTAL_GIMMICK_GEAR");keyboard_draw(NULL,NULL,EVENT_ACTION_RENDER);drawing=NULL;
    } else return 2;
    VirtKeyboardShutdown();assert(!font_count && !surface_count && field.refs==1);
    ShutdownEvents();puts("passed");return 0;
}
