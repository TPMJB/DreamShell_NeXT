/* Shared stick pointer / native D-pad arbitration for SDL applications. */
#ifndef KUI_POINTER_INPUT_H
#define KUI_POINTER_INPUT_H

static int utility_pointer, utility_native_a, utility_mouse_event, utility_mouse_button;

static void utility_pointer_event(SDL_Event *e) {
    /* SDL queues the emulated mouse event directly after its A/B event. */
    int duplicate = utility_mouse_event, button = utility_mouse_button;
    utility_mouse_event = 0;
    if(duplicate && e->type == duplicate && e->button.button == button) {
        e->type = SDL_NOEVENT;
        return;
    }
    if(e->type == SDL_MOUSEMOTION && (e->motion.xrel || e->motion.yrel))
        utility_pointer = 1;
    /* Dreamcast SDL axes are -128..127. Triggers do not move the pointer. */
    if(e->type == SDL_JOYAXISMOTION && e->jaxis.axis < 2 &&
       (e->jaxis.value < -12 || e->jaxis.value > 12)) utility_pointer = 1;
    if((e->type == SDL_JOYHATMOTION && e->jhat.hat == 0 && e->jhat.value) ||
       e->type == SDL_KEYDOWN) utility_pointer = 0;
    if(e->type == SDL_JOYBUTTONDOWN && e->jbutton.button == SDL_DC_A)
        utility_native_a = !utility_pointer;
    if((e->type == SDL_JOYBUTTONDOWN || e->type == SDL_JOYBUTTONUP) &&
       (e->jbutton.button == SDL_DC_B ||
        (e->jbutton.button == SDL_DC_A && utility_native_a))) {
        utility_mouse_event = e->type == SDL_JOYBUTTONDOWN ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        utility_mouse_button = e->jbutton.button == SDL_DC_A ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
    }
    if(e->type == SDL_MOUSEMOTION && !utility_pointer) e->type = SDL_NOEVENT;
}

static void utility_pointer_open(void) {
    utility_pointer = utility_native_a = utility_mouse_event = 0;
    SDL_DC_EmulateMouse(SDL_TRUE);
}
#endif
