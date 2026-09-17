/* DreamShell NeXT Network. Original (C) 2024-2025 SWAT.
 * Native dashboard and verified preferences (C) 2026 contributors. */
#include "ds.h"
#include <kos/net.h>
#include "../../maintenance_ui.h"
DEFAULT_MODULE_EXPORTS(app_network);

static struct {
    NetworkSettings_t draft, saved;
    int options, ethernet, modem, ftp, http, keep, owned_ftp, owned_http;
    char ip[20], mask[20], gateway[20], device[64], folder[MA_PATH];
} self;
static int dirty(void) { return memcmp(&self.draft, &self.saved, sizeof(self.draft)); }
static void address(char *out, const uint8_t *a) {
    snprintf(out, 20, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
}
static int load_module(const char *name) {
    if(GetModuleByName(name)) return 1;
    const char *root = getenv("PATH"); char path[MA_PATH];
    if(!root) return 0;
    int n = snprintf(path, sizeof(path), "%s/modules/%s.klf", root, name);
    return n > 0 && n < (int)sizeof(path) && OpenModule(path) != NULL;
}
static void read_status(void) {
    self.ethernet = self.modem = 0;
    snprintf(self.ip, sizeof(self.ip), "0.0.0.0");
    snprintf(self.mask, sizeof(self.mask), "0.0.0.0");
    snprintf(self.gateway, sizeof(self.gateway), "0.0.0.0");
    snprintf(self.device, sizeof(self.device), "No active interface");
    if(net_default_dev && (net_default_dev->flags & NETIF_RUNNING)) {
        self.modem = !strncmp(net_default_dev->name, "ppp", 3); self.ethernet = !self.modem;
        snprintf(self.device, sizeof(self.device), "%s", net_default_dev->name);
        address(self.ip, net_default_dev->ip_addr); address(self.mask, net_default_dev->netmask);
        address(self.gateway, net_default_dev->gateway);
    }
    self.ftp = GetModuleByName("ftpd") && dsystem("ftpd -q") == CMD_OK;
    self.http = GetModuleByName("httpd") && dsystem("httpd -q") == CMD_OK;
}
static void refresh(void) {
    if(self.options) {
        ma_row(0, "Connect at startup:  %s", self.draft.startup_connect_eth ? "Ethernet" : self.draft.startup_connect_ppp ? "Dial-up" : "Off");
        ma_row(1, "Sync time at startup (NTP):  %s", self.draft.startup_ntp ? "On" : "Off");
        ma_row(2, "Keep this app's servers running on exit:  %s", self.keep ? "On" : "Off");
        ma_row(3, "FTP start folder:  %s", ma_tail(self.folder, 52));
        ma_row(4, "Dial-up:  %s", self.modem ? "Disconnect modem" : "Connect modem...");
        ma_row(5, "Save startup preferences%s", dirty() ? "  [unsaved]" : "");
        ma_text(ui.detail[0], "Startup preferences are saved explicitly. Background servers is a session option.");
        ma_text(ui.detail[1], "Dial-up uses the existing PPP profile: 1111111 / dream / dreamcast.");
    } else {
        ma_row(0, "Interface:  %s", self.device);
        ma_row(1, "IPv4 address:  %s", self.ip);
        ma_row(2, "Subnet mask:  %s", self.mask);
        ma_row(3, "Gateway:  %s", self.gateway);
        ma_row(4, "FTP server:  %s", self.ftp ? "Started  /  A to stop" : "Stopped  /  A to start");
        ma_row(5, "HTTP server:  %s", self.http ? "Started  /  A to stop" : "Stopped  /  A to start");
        ma_text(ui.detail[0], self.ftp ? "FTP: ftp://%s:21  /  guest access" : "FTP is stopped. Options chooses the initial folder for your FTP client.", self.ip);
        ma_text(ui.detail[1], self.http ? "HTTP: http://%s:80  /  K-UI web controls" : "Interface status does not establish Internet or DNS reachability.", self.ip);
    }
    GUI_LabelSetText(GUI_ButtonGetCaption(ui.actions[0]), self.ethernet ? "Disconnect Ethernet" : "Connect Ethernet");
}
static void refresh_status(void) {
    read_status(); refresh();
    ma_status((self.ethernet || self.modem) ?
        (!strcmp(self.ip, "0.0.0.0") ? "Interface is up; no IPv4 address yet. Try Refresh status." : "Interface is up. Address and service status refreshed.") :
        "No active network interface. Connect Ethernet or choose dial-up.", 0);
}
static int saved_file(const char *path, int exact, const Settings_t *wanted) {
    Settings_t value; file_t fd = fs_open(path, O_RDONLY);
    if(fd == FILEHND_INVALID) return -1;
    int valid = (!exact || fs_total(fd) == sizeof(value)) &&
        ma_read_exact(fd, &value, sizeof(value)) == 0 && value.version == DS_SETTIGS_VERSION;
    if(fs_close(fd) < 0) valid = 0;
    return valid ? !memcmp(&value, wanted, sizeof(value)) : -1;
}
static int saved_matches(const Settings_t *wanted) {
    char path[MA_PATH];
    for(int i = 0; i < 8; i++) {
        maple_device_t *vmu = maple_enum_type(i, MAPLE_FUNC_MEMCARD); if(!vmu) break;
        snprintf(path, sizeof(path), "/vmu/%c%c/DSCONFIG.CFG", vmu->port + 'A', vmu->unit + '0');
        int match = saved_file(path, 0, wanted); if(match >= 0) return match;
    }
    const char *root = getenv("PATH"); if(!root) return 0;
    snprintf(path, sizeof(path), "%s/DSCONFIG.CFG", root);
    return saved_file(path, 1, wanted) == 1;
}
static void save(void) {
    Settings_t old = *GetSettings(), candidate = old; candidate.network = self.draft;
    ma_busy(1); ma_status("Saving startup preferences...", 0); SetSettings(&candidate);
    if(!SaveSettings() || !saved_matches(&candidate)) {
        SetSettings(&old); ma_status("Save could not be verified. Check VMU space and storage.", 1);
    } else {
        self.saved = self.draft; ma_status("Startup preferences saved and verified.", 0);
    }
    ma_busy(0); refresh();
}
static void stop_owned(void) {
    /* Services already active before this app are never stopped on exit. */
    if(self.keep) return;
    if(self.owned_ftp) {
        dsystemf("ftpd -t -d \"%s\"", self.folder);
        self.owned_ftp = 0;
    }
    if(self.owned_http) { dsystem("httpd -t"); self.owned_http = 0; }
    /* Server modules stay resident: their legacy backends own service threads. */
}
static void command(int action) {
    if(action == 9) { self.draft = self.saved; OpenMainApp(); return; }
    read_status();
    if((action == 1 || action == 2) && (self.ftp || self.http)) {
        ma_status("Stop FTP and HTTP before changing the connection.", 1); refresh(); return;
    }
    if(action == 1 && self.modem) { ma_status("Disconnect dial-up before starting Ethernet.", 1); return; }
    if(action == 2 && self.ethernet) { ma_status("Disconnect Ethernet before starting dial-up.", 1); return; }
    if((action == 3 || action == 4) && !(self.ethernet || self.modem)) {
        ma_status("Connect a network interface before starting a server.", 1); return;
    }
    ma_busy(1); ma_status("Applying network action. Waiting for the network driver...", 0);
    ma_note(action == 2 ? "Dial-up may take over a minute. Wait for the connection attempt to finish." : "Connection and server commands finish before other controls are enabled.");
    thd_sleep(50); int rc = CMD_ERROR;
    if(action == 1) rc = dsystem(self.ethernet ? "net --shutdown" : "net --init");
    else if(action == 2 && load_module("ppp")) rc = dsystem(self.modem ? "ppp --shut" : "ppp --init");
    else if(action == 3 && load_module("ftpd")) {
        if(self.ftp && !self.owned_ftp) {
            ma_status("This FTP server was started elsewhere. Manage it from its owner.", 1); ma_busy(0); return;
        }
        if(!*self.folder || !DirExists(self.folder)) {
            ma_status("Choose a mounted FTP start folder in Options.", 1); ma_busy(0); return;
        }
        rc = dsystemf("ftpd %s -p 21 -d \"%s\"", self.ftp ? "-t" : "-s", self.folder);
        if(rc == CMD_OK) self.owned_ftp = !self.ftp;
    } else if(action == 4 && load_module("httpd")) {
        rc = dsystem(self.http ? "httpd -t" : "httpd -s -p 80");
        if(rc == CMD_OK) self.owned_http = !self.http;
    }
    /* HTTP start returns success even on bind failure; re-query actual state. */
    int before = action == 1 ? self.ethernet : action == 2 ? self.modem : action == 3 ? self.ftp : self.http;
    thd_sleep(50); read_status();
    int after = action == 1 ? self.ethernet : action == 2 ? self.modem : action == 3 ? self.ftp : self.http;
    int ok = rc == CMD_OK && before != after;
    ma_status(ok ? "Network action completed. Status refreshed." : "Action failed or state did not change. Check connection / module / port.", !ok);
    ma_note(""); ma_busy(0); refresh();
}
static void picked(int purpose, const char *path) {
    (void)purpose;
    /* The console parser accepts quoted paths; reject command delimiters. */
    if(strpbrk(path, "\"\\\r\n;")) { ma_status("This folder name is unsupported by the FTP command parser.", 1); return; }
    snprintf(self.folder, sizeof(self.folder), "%s", path); refresh(); ma_status("FTP start folder selected. It applies the next time FTP starts.", 0); ma_note(path);
}
static void row(int index, int step) {
    if(!self.options) {
        if(index == 4 || index == 5) {
            int action = index == 4 ? 3 : 4;
            if(index == 4 ? self.ftp : self.http) command(action);
            else ma_ask(action, index == 4 ? "Start FTP server?" : "Start HTTP server?",
                index == 4 ? "FTP allows guest read/write access to mounted files.\nThe selected folder is only the starting directory.\nUse a trusted local network.\n\nStart the server on port 21?" :
                "The HTTP server exposes K-UI web controls.\nUse a trusted local network.\n\nStart the server on port 80?");
        }
        return;
    }
    if(index == 0) {
        int m = self.draft.startup_connect_eth ? 1 : self.draft.startup_connect_ppp ? 2 : 0;
        m = (m + step + 3) % 3; self.draft.startup_connect_eth = m == 1; self.draft.startup_connect_ppp = m == 2;
    } else if(index == 1) self.draft.startup_ntp = !self.draft.startup_ntp;
    else if(index == 2) self.keep = !self.keep;
    else if(index == 3) {
        if(self.ftp) { ma_status("Stop FTP before changing its root folder.", 1); return; }
        ma_browse(0, 1, self.folder); return;
    } else if(index == 4) {
        if(self.modem) command(2);
        else ma_ask(2, "Connect dial-up?", "Uses the existing PPP defaults:\nNumber 1111111 / user dream / password dreamcast.\n\nThe driver may take over a minute to return.\nCustom dial-up profiles still use the console.\n\nStart this connection?");
        return;
    } else if(index == 5) { save(); return; }
    refresh(); ma_status(dirty() ? "Startup preferences changed. Choose Save to keep them." : "Session options updated.", 0);
}
static void action(int index) {
    if(index == 0) command(1);
    else if(index == 1) refresh_status();
    else { self.options = !self.options; refresh(); }
}
static void back(void) {
    if(dirty()) ma_ask(9, "Discard startup edits?", "Startup preferences have not been saved.\nReturn to the menu without saving them?");
    else OpenMainApp();
}
void NetworkApp_Init(App_t *app) {
    memset(&self, 0, sizeof(self)); ma_init(app, "NextNetworkInput");
    self.draft = self.saved = GetSettings()->network;
    snprintf(self.folder, sizeof(self.folder), "%s", ma_default_folder());
    ui.row = row; ui.action = action; ui.confirm = command; ui.picked = picked; ui.back = back;
    ui.opened = refresh_status; ui.closed = stop_owned;
    refresh_status();
}
MAINTENANCE_CALLBACKS(NetworkApp)
