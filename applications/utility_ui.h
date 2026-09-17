/* Native D-pad focus plus the same analog pointer used by ISO Loader. */
#ifndef NEXT_UTILITY_UI_H
#define NEXT_UTILITY_UI_H

enum { UI_NONE, UI_UP, UI_DOWN, UI_LEFT, UI_RIGHT, UI_OK, UI_BACK,
       UI_X, UI_Y, UI_START };

#include "pointer_input.h"

static int utility_key(SDL_Event *e) {
    utility_pointer_event(e);
    if(e->type == SDL_NOEVENT) return UI_NONE;
    if(e->type == SDL_JOYHATMOTION && e->jhat.hat == 0) {
        if(e->jhat.value) utility_pointer = 0;
        if(e->jhat.value & SDL_HAT_UP) return UI_UP;
        if(e->jhat.value & SDL_HAT_DOWN) return UI_DOWN;
        if(e->jhat.value & SDL_HAT_LEFT) return UI_LEFT;
        if(e->jhat.value & SDL_HAT_RIGHT) return UI_RIGHT;
    }
    if(e->type == SDL_JOYBUTTONDOWN) {
        switch(e->jbutton.button) {
            case SDL_DC_A: return utility_pointer ? UI_NONE : UI_OK;
            case SDL_DC_B: return UI_BACK;
            case SDL_DC_X: return UI_X;
            case SDL_DC_Y: return UI_Y;
            case SDL_DC_START: return UI_START;
        }
    }
    if(e->type == SDL_KEYDOWN) {
        utility_pointer = 0;
        switch(e->key.keysym.sym) {
            case SDLK_UP: return UI_UP;
            case SDLK_DOWN: case SDLK_TAB: return UI_DOWN;
            case SDLK_LEFT: return UI_LEFT;
            case SDLK_RIGHT: return UI_RIGHT;
            case SDLK_RETURN: case SDLK_SPACE: return UI_OK;
            case SDLK_ESCAPE: return UI_BACK;
            case SDLK_F5: return UI_X;
            case SDLK_F6: return UI_Y;
            case SDLK_F10: return UI_START;
            default: break;
        }
    }
    return UI_NONE;
}

static int utility_global_input(SDL_Event *e) {
    return ConsoleIsVisible() || (e->type == SDL_KEYDOWN &&
        (e->key.keysym.sym == SDLK_F1 || e->key.keysym.sym == SDLK_PRINT ||
         (e->key.keysym.mod & (KMOD_CTRL | KMOD_ALT))));
}

static void utility_forward(SDL_Event *e) {
    /* Do not let FileManager's legacy X/Y modifiers disable the pointer or
     * apply a second controller action. Our handlers own those commands. */
    if(e->type == SDL_JOYBUTTONDOWN || e->type == SDL_JOYBUTTONUP ||
       e->type == SDL_JOYHATMOTION || e->type == SDL_JOYAXISMOTION)
        e->type = SDL_NOEVENT;
    /* Idle pointer events must not erase the visible D-pad selection. */
    if(e->type == SDL_MOUSEMOTION && !utility_pointer) e->type = SDL_NOEVENT;
    GUI_ScreenEvent(GUI_GetScreen(), e, 0, 0);
    e->type = SDL_NOEVENT;
}

static inline void utility_dialog(SDL_Event *e, int key) {
    int sym = key == UI_OK ? SDLK_RETURN :
        key == UI_BACK || key == UI_START ? SDLK_ESCAPE :
        key == UI_UP || key == UI_LEFT ? SDLK_LEFT :
        key == UI_DOWN || key == UI_RIGHT ? SDLK_RIGHT : 0;
    if(sym) {
        SDL_Event move;
        memset(&move, 0, sizeof(move));
        move.type = SDL_KEYDOWN; move.key.keysym.sym = sym;
        GUI_ScreenEvent(GUI_GetScreen(), &move, 0, 0);
        e->type = SDL_NOEVENT;
    } else utility_forward(e);
}

static void utility_open(Event_t *event) {
    if(!event) return;
    GUI_DisableInput();
    utility_pointer_open();
    GUI_ScreenSetJoySelectState(GUI_GetScreen(), 0);
    SetEventActive(event, 1);
}

static void utility_close(Event_t *event) {
    if(event) SetEventActive(event, 0);
    GUI_ScreenSetJoySelectState(GUI_GetScreen(), 1);
    GUI_EnableInput();
}

static void utility_remove(Event_t **event) {
    LockVideo();
    if(*event) RemoveEvent(*event);
    *event = NULL;
    UnlockVideo();
}
#endif
