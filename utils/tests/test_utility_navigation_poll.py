"""Regressions from Settings navigation and the GD Play console video."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]

class UtilityNavigationPollTests(unittest.TestCase):
    def run_c(self,source):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)
            (path/'test.c').write_text(source)
            subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror',
                '-fsanitize=undefined','-I'+str(ROOT),str(path/'test.c'),
                '-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)

    def test_disc_motion_failed_reads_and_manual_refresh(self):
        self.run_c(r'''
#include <assert.h>
#include "utils/tests/console_shim/dc/cdrom.h"
#include "applications/gdplay/modules/disc_poll.h"
int main(void) {
    gdplay_poll_t p={.disc_type=-1};
    /* The video shows a MIL-CD bootloader constantly being initialized again.
     * Reading -> paused -> standby must produce only one metadata read. */
    const int milcd=32;
    assert(gdplay_poll(&p,ERR_OK,CD_STATUS_PAUSED,milcd,1)==GDPLAY_POLL_READ);
    const int motion[]={CD_STATUS_BUSY,CD_STATUS_SEEKING,CD_STATUS_PAUSED,
        CD_STATUS_SCANNING,CD_STATUS_RETRY,CD_STATUS_STANDBY,CD_STATUS_PLAYING};
    for(int cycle=0;cycle<100;cycle++)
        for(unsigned i=0;i<sizeof(motion)/sizeof(*motion);i++)
            assert(gdplay_poll(&p,ERR_OK,motion[i],milcd,0)==GDPLAY_POLL_IDLE);
    /* Busy polls can report an unknown/wrong type. They cannot replace the
     * last known media type or trigger a read storm. */
    assert(gdplay_poll(&p,ERR_OK,CD_STATUS_BUSY,CD_GDROM,0)==GDPLAY_POLL_IDLE);
    assert(gdplay_poll(&p,ERR_OK,CD_STATUS_PAUSED,milcd,0)==GDPLAY_POLL_IDLE);
    for(int i=0;i<10;i++) assert(gdplay_poll(&p,ERR_SYS,-1,-1,0)==GDPLAY_POLL_IDLE);
    assert(gdplay_poll(&p,ERR_OK,CD_STATUS_STANDBY,milcd,1)==GDPLAY_POLL_READ);
    /* Even if that explicit scan fails, leave its error visible until retry. */
    assert(gdplay_poll(&p,ERR_OK,CD_STATUS_PAUSED,milcd,0)==GDPLAY_POLL_IDLE);
    assert(gdplay_poll(&p,ERR_OK,CD_STATUS_OPEN,-1,0)==GDPLAY_POLL_EMPTY);
    assert(gdplay_poll(&p,ERR_OK,CD_STATUS_NO_DISC,-1,0)==GDPLAY_POLL_IDLE);
    assert(gdplay_poll(&p,ERR_OK,CD_STATUS_SEEKING,CD_GDROM,0)==GDPLAY_POLL_IDLE);
    assert(gdplay_poll(&p,ERR_OK,CD_STATUS_PAUSED,CD_GDROM,0)==GDPLAY_POLL_READ);
    assert(gdplay_poll(&p,ERR_OK,CD_STATUS_STANDBY,CD_GDROM,0)==GDPLAY_POLL_IDLE);
    /* A stable media-type change and a latched change indication each cause
     * one fresh read; neither needs the user to reopen the application. */
    assert(gdplay_poll(&p,ERR_OK,CD_STATUS_PAUSED,milcd,0)==GDPLAY_POLL_READ);
    assert(gdplay_poll(&p,ERR_DISC_CHG,-1,-1,0)==GDPLAY_POLL_READ);
    for(int i=0;i<10;i++) assert(gdplay_poll(&p,ERR_DISC_CHG,-1,-1,0)==GDPLAY_POLL_IDLE);
    assert(gdplay_poll(&p,ERR_OK,CD_STATUS_PAUSED,milcd,0)==GDPLAY_POLL_IDLE);
    /* Initial spin-up can be read explicitly without a second read when its
     * stable type first becomes available. */
    p=(gdplay_poll_t){.disc_type=-1};
    assert(gdplay_poll(&p,ERR_OK,CD_STATUS_BUSY,-1,1)==GDPLAY_POLL_READ);
    assert(gdplay_poll(&p,ERR_OK,CD_STATUS_PAUSED,milcd,0)==GDPLAY_POLL_IDLE);
    return 0;
}''')

    def test_up_returns_to_current_tab_and_vertical_order(self):
        self.run_c(r'''
#include <assert.h>
#include "applications/settings/modules/settings_nav.h"
int main(void) {
    const int rows[]={3,5,4,7,7};
    for(int page=0;page<5;page++) {
        /* Up from the first setting returns to the active page, never System
         * merely because it happens to be the fifth button in the XML. */
        assert(settings_vertical_focus(page,5,rows[page],-1)==page);
        assert(settings_vertical_focus(page,page,rows[page],1)==5);
        int focus=page;
        for(int row=0;row<rows[page];row++) {
            focus=settings_vertical_focus(page,focus,rows[page],1);
            assert(focus==5+row);
        }
        focus=settings_vertical_focus(page,focus,rows[page],1);
        assert(focus==5+rows[page]); /* Save */
        focus=settings_vertical_focus(page,focus,rows[page],1);
        assert(focus==6+rows[page]); /* Menu */
        assert(settings_vertical_focus(page,focus,rows[page],1)==page);
        assert(settings_vertical_focus(page,page,rows[page],-1)==focus);
    }
    return 0;
}''')
