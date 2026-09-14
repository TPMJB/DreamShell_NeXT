/* D-pad focus uses screen geometry. It never activates a widget: A's
 * synthesized mouse click remains the single activation path. */
typedef struct { GUI_Widget *widget; SDL_Rect area; } next_target_t;

static SDL_Rect next_area(GUI_Widget *widget) {
    SDL_Rect r = GUI_WidgetGetArea(widget);
    GUI_Widget *p = GUI_WidgetGetParent(widget);
    while(p) {
        SDL_Rect a = GUI_WidgetGetArea(p);
        int type = GUI_WidgetGetType(p);
        r.x += a.x; r.y += a.y;
        if(type == WIDGET_TYPE_CONTAINER || type == WIDGET_TYPE_CARDSTACK) {
            r.x -= GUI_PanelGetXOffset(p);
            r.y -= GUI_PanelGetYOffset(p);
        }
        p = GUI_WidgetGetParent(p);
    }
    return r;
}

static int next_contains(SDL_Rect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x+r.w && y < r.y+r.h;
}

static SDL_Rect next_clip(SDL_Rect a, SDL_Rect b) {
    int x = a.x > b.x ? a.x : b.x, y = a.y > b.y ? a.y : b.y;
    int right = a.x+a.w < b.x+b.w ? a.x+a.w : b.x+b.w;
    int bottom = a.y+a.h < b.y+b.h ? a.y+a.h : b.y+b.h;
    SDL_Rect r = {x, y, right > x ? right-x : 0, bottom > y ? bottom-y : 0};
    return r;
}

static void next_targets(GUI_Widget *w, SDL_Rect clip, next_target_t *targets, int *count) {
    if(!w || (GUI_WidgetGetFlags(w) & (WIDGET_HIDDEN | WIDGET_DISABLED))) return;
    SDL_Rect r = next_clip(next_area(w), clip);
    if(!r.w || !r.h) return;
    int type = GUI_WidgetGetType(w);
    if(w == self.filebrowser || w == self.fw_browser ||
       type == WIDGET_TYPE_BUTTON || type == WIDGET_TYPE_TEXTENTRY) {
        if(*count < 128) {
            targets[*count].widget = w;
            targets[(*count)++].area = r;
        }
    } else if(type == WIDGET_TYPE_CARDSTACK) {
        next_targets(GUI_ContainerGetChild(w, GUI_CardStackGetIndex(w)), r, targets, count);
    } else if(type == WIDGET_TYPE_CONTAINER) {
        for(int i=0; i<GUI_ContainerGetCount(w); ++i)
            next_targets(GUI_ContainerGetChild(w,i), r, targets, count);
    }
}

static void next_pointer(SDL_Rect r) {
    SDL_Event motion;
    memset(&motion, 0, sizeof(motion));
    motion.type = SDL_MOUSEMOTION;
    motion.motion.x = r.x+r.w/2;
    motion.motion.y = r.y+r.h/2;
    SDL_WarpMouse(motion.motion.x, motion.motion.y);
    /* Update hover immediately, before an A press already in the event queue. */
    GUI_ScreenEvent(GUI_GetScreen(), &motion, 0, 0);
}

static void next_focus(next_target_t target, int dy) {
    if(target.widget == self.filebrowser || target.widget == self.fw_browser) {
        GUI_Widget *panel = GUI_FileManagerGetItemPanel(target.widget);
        int count = GUI_ContainerGetCount(panel);
        SDL_Rect frame = next_area(panel);
        if(count) {
            SDL_Rect row = GUI_WidgetGetArea(GUI_FileManagerGetItem(target.widget,0));
            if(row.h) {
                int index = GUI_PanelGetYOffset(panel)/row.h;
                if(dy < 0) index = (GUI_PanelGetYOffset(panel)+frame.h-row.h)/row.h;
                if(index >= count) index = count-1;
                target.area = next_clip(next_area(GUI_FileManagerGetItem(target.widget,index)), frame);
            }
        }
    }
    next_pointer(target.area);
}

/* Browse rows without opening directories or starting background inspection.
 * A selects/opens the highlighted row, exactly as it does with the stick. */
static int next_row(GUI_Widget *fm, int x, int y, int dy) {
    if(!fm || !dy) return 0;
    GUI_Widget *panel = GUI_FileManagerGetItemPanel(fm);
    SDL_Rect frame = next_area(panel);
    int count = GUI_ContainerGetCount(panel);
    if(!count || !next_contains(frame,x,y)) return 0;
    SDL_Rect row = GUI_WidgetGetArea(GUI_FileManagerGetItem(fm,0));
    if(!row.h) return 0;
    int offset = GUI_PanelGetYOffset(panel);
    int index = (y-frame.y+offset)/row.h + dy;
    if(index < 0 || index >= count) return 0;
    int top = index*row.h, new_offset = offset;
    if(top < offset) new_offset = top;
    else if(top+row.h > offset+frame.h) new_offset = top+row.h-frame.h;
    if(new_offset != offset) {
        GUI_PanelSetYOffset(panel,new_offset);
        int extent = count*row.h-frame.h;
        for(int i=0;i<GUI_ContainerGetCount(fm);++i) {
            GUI_Widget *bar = GUI_ContainerGetChild(fm,i);
            if(GUI_WidgetGetType(bar) == WIDGET_TYPE_SCROLLBAR) {
                GUI_Surface *knob = GUI_ScrollBarGetKnobImage(bar);
                int travel = GUI_WidgetGetArea(bar).h-GUI_SurfaceGetHeight(knob);
                GUI_ScrollBarSetVerticalPosition(bar, extent > 0 && travel > 0 ?
                    new_offset*travel/extent : 0);
            }
        }
        GUI_WidgetMarkChanged(fm);
    }
    next_pointer(next_clip(next_area(GUI_FileManagerGetItem(fm,index)),frame));
    return 1;
}

static void next_navigate(int dx, int dy, int region) {
    next_target_t targets[128];
    int count=0, x, y, best=-1, best_score=0x7fffffff;
    SDL_Rect screen={0,0,640,480};
    next_targets(self.app->body,screen,targets,&count);
    SDL_GetMouseState(&x,&y);
    for(int i=0;i<count;++i) {
        if(next_contains(targets[i].area,x,y)) {
            if(!region && (targets[i].widget == self.filebrowser || targets[i].widget == self.fw_browser) &&
               next_row(targets[i].widget,x,y,dy)) return;
            if(targets[i].widget != self.filebrowser && targets[i].widget != self.fw_browser) {
                x=targets[i].area.x+targets[i].area.w/2;
                y=targets[i].area.y+targets[i].area.h/2;
            }
            break;
        }
    }
    for(int i=0;i<count;++i) {
        SDL_Rect r=targets[i].area;
        int cx=r.x+r.w/2, cy=r.y+r.h/2, score;
        if(region) {
            if((region == 1 && cy >= 100) || (region == 2 && cy < 443)) continue;
            score=abs(cx-x);
        } else {
            if(next_contains(r,x,y)) continue;
            int forward=dx ? (cx-x)*dx : (cy-y)*dy;
            if(forward <= 0) continue;
            int cross=dx ? abs(cy-y) : abs(cx-x);
            int aligned=dx ? (y >= r.y && y < r.y+r.h) : (x >= r.x && x < r.x+r.w);
            score=forward+cross*4+(aligned ? 0 : 4096);
        }
        if(score < best_score) { best=i; best_score=score; }
    }
    if(best >= 0) next_focus(targets[best],dy);
}
