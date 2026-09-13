/* Executes the production launcher with a recording graphics backend and POSIX
 * files. The font metrics come from the shipped TXF, not a desktop substitute.
 * No Dreamcast timing/graphics emulation is claimed. */
#include <assert.h>
#include <dirent.h>
#include <math.h>
#include <ds.h>
#include <tsunami/tsunami.h>
#include "../../applications/launch_app/modules/module.c"
#include "../../applications/launch_app/modules/items.c"
#include "../../applications/launch_app/modules/scene.c"
#include "../../applications/launch_app/modules/manage.c"

static Font font;
static Drawable *draws[512];
static App_t apps[64], home;
static Item_t nodes[64];
static Item_list_t app_list;
static int app_count, textures, peak_textures, loads, opened, deleted, lock_depth, sleep_left, stale_load;
static uint64_t now = 1000;
static DIR *directories[32];
static DSApp host_scene;
static Event_t input;

static void Register(Drawable *d) { int i; for(i=0;i<512;i++) if(!draws[i]) { draws[i]=d; return; } assert(0); }
static void Unregister(Drawable *d) { int i; for(i=0;i<512;i++) if(draws[i]==d) draws[i]=NULL; }
static Drawable *NewDraw(int kind) { Drawable *d=calloc(1,sizeof(*d)); assert(d); d->kind=kind;d->alpha=1;d->color=(Color){1,1,1,1};return d; }
void *host_drawable(const char *name) { int i;for(i=0;i<512;i++)if(draws[i]&&!strcmp(draws[i]->name,name))return draws[i];return NULL; }
Font *host_font(const char *name) { (void)name;return &font; }

file_t fs_open(const char *path,int flags) { int i;if(flags&O_DIR){for(i=0;i<32;i++)if(!directories[i]){directories[i]=opendir(path);return directories[i]?10000+i:-1;}return -1;}return open(path,flags); }
int fs_close(file_t f) { if(f>=10000){int r=closedir(directories[f-10000]);directories[f-10000]=NULL;return r;}return close(f); }
ssize_t fs_read(file_t f,void *p,size_t n) { return read(f,p,n); }
const dirent_t *fs_readdir(file_t f) { static dirent_t e;struct dirent *d=readdir(directories[f-10000]);if(!d)return NULL;snprintf(e.name,sizeof(e.name),"%s",d->d_name);e.attr=d->d_type==DT_DIR;return &e; }
int fs_unlink(const char *p) { (void)p;deleted++;return 0; }
int FileExists(const char *p) { struct stat s;return !stat(p,&s)&&S_ISREG(s.st_mode); }
int DirExists(const char *p) { struct stat s;return !stat(p,&s)&&S_ISDIR(s.st_mode); }
int RemoveDirectory(const char *p,int n) { (void)p;(void)n;return 1; }
int RemoveApp(App_t *a) { Item_t **n=&app_list.first;while(*n){if((*n)->data==a){*n=(*n)->next;deleted++;return 1;}n=&(*n)->next;}return 0; }
void GetAppPath(char *out,size_t n,const char *file) { char *s;snprintf(out,n,"%s",file);s=strrchr(out,'/');if(s)*s=0; }
void relativeFilePath_wb(char *out,const char *file,const char *rel) { char p[NAME_MAX];GetAppPath(p,sizeof(p),file);snprintf(out,NAME_MAX,"%s/%s",p,rel); }
Item_list_t *GetAppList(void) {return &app_list;}
Item_t *listGetItemFirst(Item_list_t *l) {return l->first;}
Item_t *listGetItemNext(Item_t *i) {return i->next;}
App_t *GetAppById(int id) {int i;for(i=0;i<app_count;i++)if(apps[i].id==id)return &apps[i];return NULL;}
App_t *GetAppByName(const char *name) {int i;for(i=0;i<app_count;i++)if(!strcmp(apps[i].name,name))return &apps[i];return NULL;}
int OpenApp(App_t *a,const char *args) {(void)args;assert(!lock_depth);opened=a->id;return 1;}
int LuaDo(int mode,const char *p,void *state) {(void)mode;(void)p;(void)state;return 0;}
void *GetLuaState(void) {return NULL;}
int dsystem_script(const char *p) {(void)p;return 0;}
void ds_printf(const char *fmt,...) {(void)fmt;}
void ds_sfx_play(int id) {(void)id;}
void LockVideo(void) {assert(lock_depth==0);lock_depth++;}
void UnlockVideo(void) {assert(lock_depth==1);lock_depth--;}
int pvr_wait_ready(void) {return 0;}
void SDL_DS_Blit_Cursor(void) {}
void SDL_DC_EmulateMouse(SDL_bool b) {(void)b;}
uint64_t timer_ms_gettime64(void) {return now;}
time_t rtc_unix_secs(void) {return 0;}
kthread_t *thd_create(int d,void *(*fn)(void *),void *arg) {(void)d;(void)fn;(void)arg;return NULL;}
int thd_join(kthread_t *t,void **v) {(void)t;(void)v;return 0;}
void thd_sleep(int ms) {now+=ms;if(--sleep_left<=0)home.state=0;}
Event_t *AddEvent(const char *n,int t,int p,Event_func *fn,void *a) {(void)n;(void)t;(void)p;(void)a;input.fn=fn;return &input;}
int RemoveEvent(Event_t *e) {e->fn=NULL;return 0;}

Texture *TSU_TextureCreateFromFile(const char *p,bool alpha,bool flip,unsigned flags) {
 unsigned char h[24];Texture *t;int f;(void)alpha;(void)flip;(void)flags;
 loads++;if(strstr(p,"broken"))return NULL;
 f=open(p,O_RDONLY);if(f<0)return NULL;assert(read(f,h,24)==24);close(f);
 t=calloc(1,sizeof(*t));assert(t);t->w=(h[16]<<24)|(h[17]<<16)|(h[18]<<8)|h[19];t->h=(h[20]<<24)|(h[21]<<16)|(h[22]<<8)|h[23];
 snprintf(t->path,sizeof(t->path),"%s",p);textures++;if(textures>peak_textures)peak_textures=textures;
 if(stale_load){stale_load=0;LockVideo();MoveFocus(1);UnlockVideo();}
 return t;
}
void TSU_TextureDestroy(Texture **t) {if(*t){free(*t);*t=NULL;textures--;assert(textures>=0);}}
int TSU_TextureGetW(Texture *t) {return t?t->w:0;}
int TSU_TextureGetH(Texture *t) {return t?t->h:0;}
Banner *TSU_BannerCreate(int l,Texture *t) {Banner *b;(void)l;if(!t)return NULL;b=NewDraw(2);b->texture=t;return b;}
void TSU_BannerDestroy(Banner **b) {if(*b){Unregister(*b);free(*b);*b=NULL;}}
void TSU_BannerSetSize(Banner *b,float w,float h) {b->w=w;b->h=h;}
void TSU_BannerSetTexture(Banner *b,Texture *t) {b->texture=t;}
void TSU_AppSubAddBanner(DSApp *s,Banner *b) {(void)s;Register(b);}
void TSU_AppSubRemoveBanner(DSApp *s,Banner *b) {(void)s;Unregister(b);}
Label *TSU_LabelCreate(Font *f,const char *s,int size,bool c,bool smear,bool fix) {Label *l=NewDraw(1);(void)f;(void)c;(void)smear;(void)fix;l->size=size;TSU_LabelSetText(l,s);return l;}
void TSU_LabelDestroy(Label **l) {if(*l){Unregister(*l);free(*l);*l=NULL;}}
void TSU_LabelSetText(Label *l,const char *s) {if(l)snprintf(l->text,sizeof(l->text),"%s",s);}
void TSU_LabelGetSize(Label *l,float *w,float *h) {
 const unsigned char *s=(const unsigned char *)l->text;float left=0,right=0,up=0,down=0;size_t i;
 for(i=0;s[i];i++){int c=s[i];if(c==' '||!font.exists[c]){right+=0.1f+l->size/2.0f;continue;}
 if(i==0)left=font.left[c]*l->size;
 right+=(0.1f+font.right[c])*l->size;
 if(font.up[c]*l->size>up)up=font.up[c]*l->size;
 if(font.down[c]*l->size>down)down=font.down[c]*l->size;
 }*w=left+right;*h=up+down;
}
void TSU_LabelSetTint(Label *l,const Color *c) {l->color=*c;}
void TSU_AppSubAddLabel(DSApp *s,Label *l) {(void)s;Register(l);}
void TSU_AppSubRemoveLabel(DSApp *s,Label *l) {(void)s;Unregister(l);}
void TSU_DrawableSetTranslate(Drawable *d,const Vector *v) {d->pos=*v;}
void TSU_DrawableSetAlpha(Drawable *d,float a) {d->alpha=a;}
void TSU_AppSetDrawTransparentPolyEvent(DSApp *s,void (*fn)(void)) {(void)s;(void)fn;}
void TSU_DialogHide(Dialog *d) {if(d)d->visible=0;}
void TSU_DialogShow(Dialog *d,const char *s) {d->visible=1;snprintf(d->text,sizeof(d->text),"%s",s);}
int TSU_DialogIsVisible(Dialog *d) {return d&&d->visible;}
void TSU_DialogSetFocus(Dialog *d,int f) {d->focus=f;}
void TSU_DialogMoveFocus(Dialog *d,int dir) {d->focus=dir<0?0:1;}
void TSU_DialogActivateFocused(Dialog *d) {if(d->focus==0)LaunchAppDeleteConfirm(d);else LaunchAppDeleteCancel(d);}
void TSU_DialogHandleClick(Dialog *d,int x,int y) {(void)d;(void)x;(void)y;}

static void LoadFont(void) {
 unsigned char b[32],g[12];int i,n,ascent;FILE *f=fopen("resources/fonts/txf/helvetica.txf","rb");assert(f);assert(fread(b,1,32,f)==32);
 memcpy(&n,b+28,4);memcpy(&ascent,b+20,4);
 for(i=0;i<n;i++){unsigned c;assert(fread(g,1,12,f)==12);c=g[0]|(g[1]<<8);if(c>=256)continue;
 font.exists[c]=1;font.left[c]=(int8_t)g[4]/(float)ascent;font.right[c]=((int8_t)g[4]+(int8_t)g[2])/(float)ascent;
 font.up[c]=((int8_t)g[5]+(int8_t)g[3])/(float)ascent;font.down[c]=-(int8_t)g[5]/(float)ascent;}
 fclose(f);
}
static const char *Attr(mxml_node_t *n,const char *name,const char *def) {const char *v=mxmlElementGetAttr(n,name);return v?v:def;}
static Color ParseColor(const char *s) {unsigned r=255,g=255,b=255,a=255;sscanf(s,"#%2x%2x%2x%2x",&r,&g,&b,&a);return(Color){a/255.f,r/255.f,g/255.f,b/255.f};}
static void LoadScreen(void) {
 FILE *f=fopen("applications/launch_app/app.xml","r");mxml_node_t *tree,*body,*n;assert(f);tree=mxmlLoadFile(NULL,f,NULL);fclose(f);assert(tree);
 body=mxmlFindElement(tree,tree,"body",NULL,NULL,MXML_DESCEND);assert(body);
 for(n=body->child;n;n=n->next){Drawable *d;const char *kind;if(n->type!=MXML_ELEMENT)continue;kind=n->value.element.name;
 if(!strcmp(kind,"label")){d=TSU_LabelCreate(&font,Attr(n,"text",""),atoi(Attr(n,"fontsize","16")),false,false,false);}
 else if(!strcmp(kind,"rectangle"))d=NewDraw(0);
 else if(!strcmp(kind,"dialog"))d=NewDraw(3);
 else continue;
 snprintf(d->name,sizeof(d->name),"%s",Attr(n,"name",""));d->pos=(Vector){atof(Attr(n,"x","0")),atof(Attr(n,"y","0")),atof(Attr(n,"z","0")),1};
 d->w=atof(Attr(n,"width","0"));d->h=atof(Attr(n,"height","0"));d->radius=atof(Attr(n,"radius","0"));
 d->color=ParseColor(Attr(n,"color","#FFFFFFFF"));if(d->kind==0)d->pos.y+=d->h;Register(d);
 }mxmlDelete(tree);
}
static void LoadApps(void) {
 DIR *dir=opendir("applications");struct dirent *ent;assert(dir);
 while((ent=readdir(dir))){char path[NAME_MAX];FILE *f;mxml_node_t *tree,*node;App_t *app;
 snprintf(path,sizeof(path),"applications/%s/app.xml",ent->d_name);f=fopen(path,"r");if(!f)continue;
 tree=mxmlLoadFile(NULL,f,NULL);fclose(f);if(!tree)continue;node=mxmlFindElement(tree,tree,"app",NULL,NULL,MXML_DESCEND);assert(node);
 app=&apps[app_count];app->id=app_count+1;snprintf(app->name,sizeof(app->name),"%s",Attr(node,"name",""));snprintf(app->ver,sizeof(app->ver),"%s",Attr(node,"version",""));
 snprintf(app->fn,sizeof(app->fn),"%s",path);relativeFilePath_wb(app->icon,path,Attr(node,"icon",""));
 nodes[app_count].data=app;nodes[app_count].next=app_list.first;app_list.first=&nodes[app_count++];mxmlDelete(tree);
 }
 closedir(dir);
 home=*GetAppByName("Launch App");home.tsunami=&host_scene;home.state=APP_STATE_OPENED;
}
static void Setup(void) {setenv("PATH","resources",1);LoadFont();LoadScreen();LoadApps();LaunchApp_Init(&home);LaunchApp_Open(&home);}
static void Cleanup(void) {int i;home.state=0;LaunchApp_Close(&home);LaunchApp_Shutdown(&home);assert(textures==0);for(i=0;i<512;i++)if(draws[i]){free(draws[i]);draws[i]=NULL;}}
static void Button(int n) {SDL_Event e={0};e.type=SDL_JOYBUTTONDOWN;e.jbutton.button=n;InputHandler(NULL,&e,EVENT_ACTION_UPDATE);}
static void Hat(int n) {SDL_Event e={0};e.type=SDL_JOYHATMOTION;e.jhat.value=n;InputHandler(NULL,&e,EVENT_ACTION_UPDATE);}
static void Mouse(int type,int x,int y) {SDL_Event e={0};e.type=type;if(type==SDL_MOUSEMOTION){e.motion.x=x;e.motion.y=y;}else{e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;}InputHandler(NULL,&e,EVENT_ACTION_UPDATE);}
static void Behavior(void) {
 int initial=self.item_count,last=self.item_count-1,i;float w,h;
 assert(initial>=17);assert(!strcmp(self.items[0].name,"GD Ripper"));assert(self.focused_index==0);
 MoveFocus(1000);assert(self.focused_index==last&&self.first_visible==last-LIST_ROWS+1);MoveFocus(-1000);assert(self.focused_index==0&&self.first_visible==0);
 Hat(SDL_HAT_DOWN);assert(self.focused_index==1);Hat(0);
 assert(!self.mouse_visible);Button(SDL_DC_B);assert(!deleted&&!TSU_DialogIsVisible(self.delete_dialog));
 Button(SDL_DC_X);assert(TSU_DialogIsVisible(self.delete_dialog));assert(self.delete_dialog->focus==1);Button(SDL_DC_A);assert(!deleted);
 Button(SDL_DC_X);Button(SDL_DC_B);assert(!deleted&&!TSU_DialogIsVisible(self.delete_dialog));
 Button(SDL_DC_START);assert(opened==GetAppByName("Settings")->id);
 opened=0;Mouse(SDL_MOUSEBUTTONDOWN,30,LIST_Y+20);Mouse(SDL_MOUSEMOTION,30,LIST_Y+ROW_H+20);Mouse(SDL_MOUSEBUTTONUP,30,LIST_Y+ROW_H+20);assert(!opened);
 Mouse(SDL_MOUSEBUTTONDOWN,30,LIST_Y+20);Mouse(SDL_MOUSEBUTTONUP,30,LIST_Y+20);assert(opened==GetAppByName("GD Ripper")->id);
 Button(SDL_DC_R);assert(self.focused_index==LIST_ROWS);Button(SDL_DC_L);assert(self.focused_index==0);
 Hat(SDL_HAT_DOWN);sleep_left=24;LaunchAppWorker(NULL);home.state=APP_STATE_OPENED;Hat(0);assert(self.focused_index>1&&self.focused_index<8);
 for(i=0;i<self.item_count;i++){SetFocusedIndex(i,0);TSU_LabelGetSize(self.items[i].label,&w,&h);assert(w<=LIST_W-56);for(int j=0;j<4;j++){TSU_LabelGetSize(self.description[j],&w,&h);assert(w<=DETAIL_W);}}
 SetFocusedIndex(2,0);Button(SDL_DC_X);Hat(SDL_HAT_LEFT);Button(SDL_DC_A);assert(deleted==1&&self.item_count==initial-1);
 puts("navigation, button safety, text bounds and removal: passed");
}
static void PngHeader(const char *path,unsigned w,unsigned h) {unsigned char b[24]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82};FILE *f;for(int i=0;i<4;i++){b[19-i]=w>>(8*i);b[23-i]=h>>(8*i);}f=fopen(path,"wb");assert(f);assert(fwrite(b,1,24,f)==24);fclose(f);}
static void Previews(const char *dir) {
 char p[NAME_MAX];int base=textures,before=loads;
 snprintf(p,sizeof(p),"%s/oversized.png",dir);PngHeader(p,4096,4096);assert(!LoadSmallTexture(p,512));assert(loads==before);
 for(int i=0;i<3;i++){snprintf(p,sizeof(p),"%s/%d.png",dir,i);PngHeader(p,256,128);snprintf(self.items[i].preview,NAME_MAX,"%s",p);}
 SetFocusedIndex(0,0);now+=PREVIEW_DELAY_MS+1;ServicePreview(now);assert(textures==base+1);
 SetFocusedIndex(1,0);ServicePreview(now);assert(textures==base+1);now+=PREVIEW_DELAY_MS+1;ServicePreview(now);assert(textures==base+2);
 SetFocusedIndex(2,0);now+=PREVIEW_DELAY_MS+1;ServicePreview(now);assert(textures==base+2);
 before=loads;SetFocusedIndex(1,0);now+=PREVIEW_DELAY_MS+1;ServicePreview(now);assert(loads==before);
 SetFocusedIndex(0,0);now+=PREVIEW_DELAY_MS+1;stale_load=1;ServicePreview(now);assert(self.focused_index==1&&textures==base+2);
 assert(self.preview_banner->texture==self.items[1].icon);
 puts("preview debounce, cache limit, bad headers and stale results: passed");
}
static void Faults(void) {
 int count=self.item_count; float w,h;
 App_t *a=&apps[app_count];
 memset(a,0,sizeof(*a));a->id=app_count+1;
 snprintf(a->name,sizeof(a->name),"Unknown application with a very long display name");
 snprintf(a->fn,sizeof(a->fn),"/missing/app.xml");snprintf(a->icon,sizeof(a->icon),"/missing/icon.png");
 nodes[app_count].data=a;nodes[app_count].next=app_list.first;app_list.first=&nodes[app_count++];
 RebuildAppList();assert(self.item_count==count+1);
 for(int i=0;i<self.item_count;i++)if(self.items[i].app_id==a->id){
  SetFocusedIndex(i,0);assert(!self.items[i].icon);assert(self.items[i].label);
  assert(self.preview_banner->texture==self.fallback_icon);
  assert(!strcmp(self.items[i].description,"Open this installed DreamShell application."));
  for(int j=0;j<2;j++){TSU_LabelGetSize(self.title[j],&w,&h);assert(w<=DETAIL_W);}
  Button(SDL_DC_A);assert(opened==a->id);
 }
 /* Removing every application leaves a usable, safe empty state. */
 app_list.first=NULL;RebuildAppList();assert(self.item_count==0&&self.focused_index==-1);
 Button(SDL_DC_A);Button(SDL_DC_X);Button(SDL_DC_B);Hat(SDL_HAT_DOWN);Hat(0);
 assert(!TSU_DialogIsVisible(self.delete_dialog));
 assert(!strcmp(self.title[0]->text,"No apps found"));
 puts("missing artwork, unknown apps, long names and empty list: passed");
}
static void Lifecycle(void) {
 char selected_file[NAME_MAX];int saved=10;
 SetFocusedIndex(saved,0);snprintf(selected_file,sizeof(selected_file),"%s",self.items[saved].identity);
 Cleanup();LoadScreen();home.state=APP_STATE_OPENED;LaunchApp_Init(&home);LaunchApp_Open(&home);
 assert(self.focused_index==saved);assert(!strcmp(self.items[saved].identity,selected_file));
 puts("selection restored after module unload and reopen: passed");
}
static void JsonString(const char *s) {putchar('"');for(;*s;s++){if(*s=='"'||*s=='\\')putchar('\\');if((unsigned char)*s>=32)putchar(*s);}putchar('"');}
static void Snapshot(void) {
 int first=1;printf("[");for(int i=0;i<512;i++){Drawable *d=draws[i];if(!d||d->alpha<=0||d->kind==3)continue;if(!first)printf(",");first=0;
 printf("{\"kind\":%d,\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"w\":%.3f,\"h\":%.3f,\"size\":%d,\"radius\":%.3f,\"color\":[%.3f,%.3f,%.3f,%.3f],\"text\":",d->kind,d->pos.x,d->pos.y,d->pos.z,d->w,d->h,d->size,d->radius,d->color.r,d->color.g,d->color.b,d->alpha*d->color.a);
 JsonString(d->text);printf(",\"image\":");JsonString(d->texture?d->texture->path:"");printf("}");}puts("]");
}
int main(int argc,char **argv) {
 assert(argc>=2);Setup();
 if(!strcmp(argv[1],"behavior"))Behavior();
 else if(!strcmp(argv[1],"previews")){assert(argc>=3);Previews(argv[2]);}
 else if(!strcmp(argv[1],"lifecycle"))Lifecycle();
 else if(!strcmp(argv[1],"faults"))Faults();
 else if(!strcmp(argv[1],"snapshot")){SetFocusedIndex(argc>2?atoi(argv[2]):0,0);Snapshot();}
 else assert(0);
 Cleanup();return 0;
}
