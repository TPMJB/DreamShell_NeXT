/* Drive motion is not a disc change. Keep one result until the media changes
 * or the user explicitly requests another read. */
#ifndef NEXT_GDPLAY_DISC_POLL_H
#define NEXT_GDPLAY_DISC_POLL_H

enum { GDPLAY_POLL_IDLE, GDPLAY_POLL_EMPTY, GDPLAY_POLL_READ };
typedef struct {
    int scanned, empty, disc_type, change_reported;
} gdplay_poll_t;

static int gdplay_poll(gdplay_poll_t *poll, int rv, int status, int type, int request) {
    int stable = status == CD_STATUS_PAUSED || status == CD_STATUS_STANDBY ||
                 status == CD_STATUS_PLAYING;
    if(rv == ERR_OK && (status == CD_STATUS_OPEN || status == CD_STATUS_NO_DISC)) {
        int report = !poll->empty || request;
        poll->empty = 1;
        poll->scanned = 0;
        poll->disc_type = -1;
        poll->change_reported = 0;
        return report ? GDPLAY_POLL_EMPTY : GDPLAY_POLL_IDLE;
    }
    if(request || (rv == ERR_DISC_CHG && !poll->change_reported)) {
        poll->empty = 0;
        poll->scanned = 1;
        poll->disc_type = rv == ERR_OK && stable ? type : -1;
        poll->change_reported = rv == ERR_DISC_CHG;
        return GDPLAY_POLL_READ;
    }
    /* Seeking, spin-down, busy/retry states and failed status polls retain
     * the current result. In particular, a read error cannot start a loop. */
    if(rv != ERR_OK || !stable) return GDPLAY_POLL_IDLE;
    poll->change_reported = 0;
    int read = !poll->scanned || (poll->disc_type >= 0 && type >= 0 &&
                                poll->disc_type != type);
    poll->empty = 0;
    poll->scanned = 1;
    if(type >= 0) poll->disc_type = type;
    return read ? GDPLAY_POLL_READ : GDPLAY_POLL_IDLE;
}
#endif
