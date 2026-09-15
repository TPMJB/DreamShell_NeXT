"""Games NeXT action geometry, controller routing and whole-library artwork scans."""
import unittest
from test_iso_loader_next import ROOT, compile_run

class GamesNextTests(unittest.TestCase):
    def test_games_scan_defaults_match_ripper_on_each_device(self):
        source=(ROOT/'applications/games_menu/modules/menu.c').read_text()
        resolver=source[source.index('void SetGamesPath('):source.index('const char *GetDeviceDir(')]
        support=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "applications/game_paths.h"
#define NAME_MAX 256
#define IDE_PATH "/ide"
#define SD_PATH "/sd"
#define CD_PATH "/cd"
#define IDE_GAMES_PATH NEXT_IDE_GAMES_PATH
#define SD_GAMES_PATH NEXT_SD_GAMES_PATH
#define CD_GAMES_PATH "/cd/games"
static struct {int current_dev,ide,sd,cd;char games_path[256],games_path_sd[256];} menu_data;
static int ide_custom,sd_custom;
static const char *GetDeviceDir(int dev) {return dev==1?IDE_PATH:SD_PATH;}
static int DirExists(const char *path) {
    return !strcmp(path,NEXT_IDE_GAMES_PATH) || !strcmp(path,NEXT_SD_GAMES_PATH) ||
        (ide_custom && !strcmp(path,"/ide/custom")) || (sd_custom && !strcmp(path,"/sd/custom"));
}
'''
        cases=r'''
int main(void) {
    menu_data.ide=1;menu_data.sd=1;menu_data.current_dev=1;
    SetGamesPath(NEXT_GAMES_FOLDER);
    assert(!strcmp(menu_data.games_path,NEXT_IDE_GAMES_PATH));
    assert(!strcmp(menu_data.games_path_sd,NEXT_SD_GAMES_PATH));
    ide_custom=1;SetGamesPath("custom");
    assert(!strcmp(menu_data.games_path,"/ide/custom"));
    assert(!strcmp(menu_data.games_path_sd,NEXT_SD_GAMES_PATH));
    ide_custom=0;sd_custom=1;SetGamesPath("custom");
    assert(!strcmp(menu_data.games_path,NEXT_IDE_GAMES_PATH));
    assert(!strcmp(menu_data.games_path_sd,"/sd/custom"));
    menu_data.ide=0;menu_data.current_dev=2;SetGamesPath(NEXT_GAMES_FOLDER);
    assert(!strcmp(menu_data.games_path,NEXT_SD_GAMES_PATH));
    menu_data.ide=1;menu_data.sd=0;menu_data.current_dev=1;SetGamesPath(NEXT_GAMES_FOLDER);
    assert(!strcmp(menu_data.games_path,NEXT_IDE_GAMES_PATH));
    return 0;
}
'''
        compile_run(support+resolver+cases,flags=('-I',str(ROOT)))

    def test_visible_actions_controller_and_view_bounds(self):
        layout=(ROOT/'applications/games_menu/modules/next_layout.h').read_text()
        module=(ROOT/'applications/games_menu/modules/module.c').read_text()
        router=module[module.index('static void StateAppInpuEvent('):module.index('static void DoMenuControlHandler(')]
        support=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
enum {MT_PLANE_TEXT=1,MT_IMAGE_TEXT_64_5X2,MT_IMAGE_128_4X3};
enum {KeyStart,KeyCancel,KeyUp,KeyDown,KeyLeft,KeyRight,KeySelect,KeyMiscX,KeyMiscY};
enum {SA_GAMES_MENU,SA_SYSTEM_MENU,SA_PRESET_MENU,SA_SCAN_COVER,SA_OPTIMIZE_COVER,SA_CONTROL};
static struct {int focus;} library={-1};
static struct {bool game_changed;} self;
static int scans,stops,lastkey=-1;
static void NextFocus(int f) {library.focus=f;}
static void GamesApp_InputEvent(int t,int k) {(void)t;lastkey=k;}
#define GamesApp_SystemMenuInputEvent GamesApp_InputEvent
#define GamesApp_PresetMenuInputEvent GamesApp_InputEvent
#define GamesApp_ScanCoverInputEvent GamesApp_InputEvent
#define GamesApp_OptimizeCoverInputEvent GamesApp_InputEvent
static void GamesApp_ControlInputEvent(int t,int k,int s) {(void)t;(void)k;(void)s;}
static void StopShowCover(void) {++stops;}
static void StopCDDA(void) {++stops;}
static void ScanMissingCoversClick(void *p) {(void)p;++scans;}
'''
        cases=r'''
static void key(int k) {StateAppInpuEvent(SA_GAMES_MENU,1,k);}
int main(void) {
    for(int i=0;i<NEXT_ACTION_COUNT;++i) {
        NextRect r=next_actions[i];
        assert(r.x>=24 && r.x+r.w<=616 && r.y==416 && r.y+r.h==452);
        assert(NextActionAt(r.x,r.y)==i && NextActionAt(r.x+r.w-1,r.y+r.h-1)==i);
        assert(NextActionAt(r.x+r.w,r.y)==-1);
    }
    for(int mode=1;mode<=3;++mode) {
        for(int i=0;i<NextRows(mode)*NextColumns(mode);++i) {
            NextRect a=NextGameRect(mode,i);
            assert(a.x>=24 && a.x+a.w<=616 && a.y>=96 && a.y+a.h<=392);
            for(int j=0;j<i;++j) {
                NextRect b=NextGameRect(mode,j);
                assert(a.x>=b.x+b.w || b.x>=a.x+a.w || a.y>=b.y+b.h || b.y>=a.y+a.h);
            }
        }
    }
    key(KeySelect);assert(lastkey==KeySelect && scans==0); /* A plays from the library */
    key(KeyStart);assert(library.focus==NEXT_PLAY);
    key(KeyRight);key(KeyRight);assert(library.focus==NEXT_SCAN);
    key(KeySelect);assert(scans==1 && stops==2 && library.focus==-1);
    key(KeyStart);key(KeyLeft);assert(library.focus==NEXT_EXIT);
    key(KeyCancel);assert(library.focus==-1 && lastkey==KeySelect); /* B leaves toolbar, not app */
    key(KeyStart);key(KeyLeft);key(KeyLeft);key(KeySelect);assert(lastkey==KeyStart); /* Settings */
    key(KeyStart);key(KeyDown);key(KeySelect);assert(lastkey==KeySelect);
    return 0;
}
'''
        compile_run(support+layout+router+cases)

    def test_scan_all_preserves_existing_and_retries_after_cancel(self):
        source=(ROOT/'applications/games_menu/modules/menu.c').read_text()
        worker=source[source.index('void *LoadPVRCoverThread('):source.index('static int AppCompareGames(')]
        support=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_MENU 3
#define MT_PLANE_TEXT 1
#define SC_WITHOUT_SEARCHING 0
#define SC_DEFAULT 1
#define SC_EXISTS 2
#define CSE_EXISTS 1
static struct {
 int scan_count,last_game_index,last_game_status;
 char last_game_scanned[64];
} checkpoint;
static struct {
 bool rescan_covers,stop_load_pvr_cover,finished_menu;
 int artwork_total,artwork_checked,artwork_extracted,artwork_existing,artwork_unavailable;
 int games_array_count,current_dev;
 __typeof__(checkpoint) cover_scanned_app;
 struct {int exists_cover[MAX_MENU];bool check_pvr;} games_array[5];
 void (*send_message_scan)(const char *,const char *);
 void (*post_pvr_cover)(bool);
} menu_data;
static int exists[5],attempts[5],visits[5],cancel_after=-1,callbacks;
static bool new_cover;
static void RetrieveCovers(int d,int m) {(void)d;assert(m>=1 && m<=MAX_MENU);}
static bool GetCoverName(int i,char **s) {free(*s);*s=malloc(20);sprintf(*s,"game%d",i);return true;}
static int CheckCover(int i,int m) {(void)m;++visits[i];return exists[i]?SC_EXISTS:SC_DEFAULT;}
static bool ExtractPVRCover(int i) {assert(menu_data.games_array[i].check_pvr);++attempts[i];if(i==4)return false;exists[i]=1;return true;}
static void message(const char *f,const char *s) {(void)f;(void)s;}
static void post(bool c) {++callbacks;new_cover=c;}
static void SaveScannedCover(void) {if(menu_data.artwork_checked==cancel_after)menu_data.stop_load_pvr_cover=true;}
static void reset_run(void) {
 menu_data.stop_load_pvr_cover=false;memset(visits,0,sizeof(visits));
 menu_data.send_message_scan=message;menu_data.post_pvr_cover=post;
 menu_data.games_array_count=5;menu_data.cover_scanned_app.last_game_index=5;
}
'''
        cases=r'''
int main(void) {
 exists[0]=exists[3]=1;reset_run();
 LoadPVRCoverThread(NULL);
 assert(callbacks==1 && new_cover && menu_data.artwork_checked==5);
 assert(menu_data.artwork_existing==2 && menu_data.artwork_extracted==2 && menu_data.artwork_unavailable==1);
 for(int i=0;i<5;++i)assert(visits[i]==1);
 assert(!attempts[0] && !attempts[3]);
 reset_run();LoadPVRCoverThread(NULL);
 assert(!new_cover && menu_data.artwork_existing==4 && attempts[4]==2); /* retries unavailable */
 exists[1]=exists[2]=0;cancel_after=2;reset_run();LoadPVRCoverThread(NULL);
 assert(menu_data.artwork_checked==2 && !visits[2] && !visits[4]);
 cancel_after=-1;reset_run();LoadPVRCoverThread(NULL);
 assert(menu_data.artwork_checked==5 && menu_data.artwork_extracted==1);
 for(int i=0;i<5;++i)assert(visits[i]==1); /* no stale checkpoint skips */
 menu_data.games_array_count=0;LoadPVRCoverThread(NULL);
 assert(!menu_data.artwork_total && !menu_data.artwork_checked && !new_cover);
 return 0;
}
'''
        compile_run(support+worker+cases)
