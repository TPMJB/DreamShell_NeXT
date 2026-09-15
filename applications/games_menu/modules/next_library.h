/* Native Tsunami chrome. Owned separately so every view retains its actions. */
#include "next_layout.h"

static const Color next_navy = {1, .047f, .078f, .122f};
static const Color next_panel = {1, .075f, .125f, .188f};
static const Color next_cyan = {1, .18f, .83f, .94f};
static const Color next_white = {1, .93f, .96f, .99f};
static const Color next_muted = {1, .57f, .68f, .77f};
static struct {
    Rectangle *panels[3], *buttons[NEXT_ACTION_COUNT];
    Label *brand, *heading, *owner, *url, *status, *hint, *empty;
    Label *labels[NEXT_ACTION_COUNT];
    int focus, mouse_x, mouse_y, mouse_duplicate, mouse_button;
} library;

static Label *NextLabel(const char *text, int size, int x, int y, int width, const Color *color) {
    Label *label=TSU_LabelCreate(self.menu_font,text,size,false,false,width>0);
    Vector v={x,y,ML_ITEM+3,1};
    TSU_LabelSetTranslate(label,&v);
    TSU_LabelSetTint(label,color);
    if(width>0) TSU_DrawableSetSize((Drawable *)label,width,size);
    TSU_AppSubAddLabel(self.app->tsunami,label);
    return label;
}
static Rectangle *NextPanel(NextRect r, const Color *color, int layer) {
    Rectangle *panel=TSU_RectangleCreate(PVR_LIST_OP_POLY,r.x,r.y+r.h,r.w,r.h,color,layer,0);
    TSU_AppSubAddRectangle(self.app->tsunami,panel);
    return panel;
}
static void NextFocus(int focus) {
    library.focus=focus;
    for(int i=0;i<NEXT_ACTION_COUNT;++i) {
        TSU_DrawableSetTint((Drawable *)library.buttons[i],i==focus?&next_cyan:&next_panel);
        TSU_LabelSetTint(library.labels[i],i==focus?&next_navy:&next_white);
    }
    if(library.hint) TSU_LabelSetText(library.hint,focus<0 ?
        "A Play   X View   Y Game setup   L/R Page   Start Actions" :
        "Left/Right Choose action   A Select   B Return to games");
    if(library.hint) TSU_DrawableSetSize((Drawable *)library.hint,592,12);
}
static void NextRemoveLabel(Label **label) {
    if(*label) {
        TSU_AppSubRemoveLabel(self.app->tsunami,*label);
        TSU_LabelDestroy(label);
    }
}
static void NextRemovePanel(Rectangle **panel) {
    if(*panel) {
        TSU_AppSubRemoveRectangle(self.app->tsunami,*panel);
        TSU_RectangleDestroy(panel);
    }
}
static void NextDestroyChrome(void) {
    for(int i=0;i<3;++i) NextRemovePanel(&library.panels[i]);
    for(int i=0;i<NEXT_ACTION_COUNT;++i) {
        NextRemovePanel(&library.buttons[i]);
        NextRemoveLabel(&library.labels[i]);
    }
    NextRemoveLabel(&library.brand); NextRemoveLabel(&library.heading);
    NextRemoveLabel(&library.owner); NextRemoveLabel(&library.url);
    NextRemoveLabel(&library.status); NextRemoveLabel(&library.hint);
    NextRemoveLabel(&library.empty);
}
static void NextCreateChrome(void) {
    NextDestroyChrome();
    TSU_AppSetBg(self.app->tsunami,next_navy.r,next_navy.g,next_navy.b);
    library.panels[0]=NextPanel((NextRect){0,0,640,480},&next_navy,ML_BACKGROUND);
    library.panels[1]=NextPanel((NextRect){24,88,592,2},&next_cyan,ML_BACKGROUND+1);
    library.panels[2]=NextPanel((NextRect){24,408,592,1},&next_panel,ML_BACKGROUND+1);
    library.brand=NextLabel("DREAMSHELL NeXT",12,24,28,0,&next_cyan);
    library.heading=NextLabel("Games",28,24,57,0,&next_white);
    library.owner=NextLabel("TPMJB",21,535,33,81,&next_cyan);
    library.url=NextLabel("github.com/TPMJB",12,474,54,142,&next_muted);
    library.status=NextLabel("Finding your games...",13,24,402,592,&next_muted);
    library.hint=NextLabel("",12,24,470,592,&next_muted);
    for(int i=0;i<NEXT_ACTION_COUNT;++i) {
        NextRect r=next_actions[i];
        library.buttons[i]=NextPanel(r,&next_panel,ML_ITEM+1);
        library.labels[i]=NextLabel(next_action_names[i],14,r.x+10,r.y+24,r.w-18,&next_white);
    }
    library.mouse_x=library.mouse_y=-1;
    library.mouse_duplicate=0;
    NextFocus(-1);
}
