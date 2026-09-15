/* The tab strip is one horizontal row above the settings list. */
#ifndef NEXT_SETTINGS_NAV_H
#define NEXT_SETTINGS_NAV_H
static int settings_vertical_focus(int page, int focus, int rows, int step) {
    if(focus < 5) return step > 0 ? 5 : rows + 6;
    if(step < 0 && focus == 5) return page;
    if(step > 0 && focus == rows + 6) return page;
    return focus + step;
}
#endif
