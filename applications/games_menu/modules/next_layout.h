/* Games NeXT: shared screen geometry and controller focus, in 640x480 units. */
#ifndef GAMES_NEXT_LAYOUT_H
#define GAMES_NEXT_LAYOUT_H

enum { NEXT_PLAY, NEXT_PRESET, NEXT_SCAN, NEXT_VIEW, NEXT_SETTINGS, NEXT_EXIT, NEXT_ACTION_COUNT };
typedef struct { int x, y, w, h; } NextRect;
static const NextRect next_actions[NEXT_ACTION_COUNT] = {
    {24,416,70,36}, {102,416,100,36}, {210,416,126,36},
    {344,416,68,36}, {420,416,104,36}, {532,416,84,36}
};
static const char *const next_action_names[NEXT_ACTION_COUNT] = {
    "Play", "Game setup", "Scan artwork", "View", "Settings", "Exit"
};
static int NextInside(NextRect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x+r.w && y < r.y+r.h;
}
static int NextActionAt(int x, int y) {
    for(int i=0; i<NEXT_ACTION_COUNT; ++i)
        if(NextInside(next_actions[i],x,y)) return i;
    return -1;
}
static int NextRows(int mode) {
    return mode == MT_PLANE_TEXT ? 8 : mode == MT_IMAGE_TEXT_64_5X2 ? 4 : 2;
}
static int NextColumns(int mode) {
    return mode == MT_PLANE_TEXT ? 1 : mode == MT_IMAGE_TEXT_64_5X2 ? 2 : 3;
}
static NextRect NextGameRect(int mode, int index) {
    int column=index/NextRows(mode), row=index%NextRows(mode);
    if(mode == MT_PLANE_TEXT) return (NextRect){24,96+row*36,316,36};
    if(mode == MT_IMAGE_TEXT_64_5X2) return (NextRect){24+column*300,96+row*74,292,70};
    return (NextRect){24+column*200,96+row*150,192,146};
}
/* Start enters/leaves the bar; B and vertical navigation return to games. */
static int NextFocusKey(int focus, int key) {
    if(key == KeyStart) return focus < 0 ? 0 : -1;
    if(focus < 0) return -1;
    if(key == KeyCancel || key == KeyUp || key == KeyDown) return -1;
    if(key == KeyLeft) return (focus+NEXT_ACTION_COUNT-1)%NEXT_ACTION_COUNT;
    if(key == KeyRight) return (focus+1)%NEXT_ACTION_COUNT;
    return focus;
}
#endif
