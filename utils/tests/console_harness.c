/* Executes the actual console C implementation against local files and a mock drive. */
#include "console_shim/ds.h"
#include "../../applications/gd_ripper/modules/module.c"
#include <assert.h>
#include <dirent.h>
static const char *mount_root;
static const char *ide_mount_root, *pc_mount_root;
static DIR *dirs[1024];
static const char *map_path(const char *p) {
    static char mapped[2048];
    if (mount_root && !strncmp(p,"/sd",3) && (!p[3] || p[3]=='/')) { snprintf(mapped,sizeof(mapped),"%s%s",mount_root,p+3); return mapped; }
    if (ide_mount_root && !strncmp(p,"/ide",4) && (!p[4] || p[4]=='/')) { snprintf(mapped,sizeof(mapped),"%s%s",ide_mount_root,p+4); return mapped; }
    if (pc_mount_root && !strncmp(p,"/pc",3) && (!p[3] || p[3]=='/')) { snprintf(mapped,sizeof(mapped),"%s%s",pc_mount_root,p+3); return mapped; }
    return p;
}

static GUI_Widget widgets[128];
static char widget_names[128][64];
static GUI_Widget *focused;
static int screen_events;
static int nw, drive_fd = -1, read_calls, injected, fault, clicks;
static uint32_t mode = 2352;
static int auto_test;
static int legacy_fs, reject_append, fail_crc, mode_failures;
static uint32_t drive_base = 45150;
static uint64_t clock_ms = 1000, read_bytes;
static unsigned recovery_pass, recovered_count, stop_after;
static int recovery_fault, full_thread;
static int scan_fault, scan_injected, scan_map_fd = -1;
static uint8_t *scan_buffer;
static size_t scan_buffer_size;
GUI_Widget *host_widget(const char *name) {
    for (int i=0;i<nw;++i) if (!strcmp(widget_names[i],name)) return &widgets[i];
    assert(nw < 128); snprintf(widget_names[nw],64,"%s",name); return &widgets[nw++];
}
SDL_Rect GUI_FontGetTextSize(void *f, const char *s) { (void)f; return (SDL_Rect){0,0,strlen(s)*9,16}; }
GUI_Widget *GUI_ButtonGetCaption(GUI_Widget *w) { return w; }
GUI_Screen *GUI_GetScreen(void) { return host_widget("screen"); }
GUI_Widget *GUI_ScreenGetFocusWidget(GUI_Screen *s) { (void)s; return focused; }
void GUI_ScreenEvent(GUI_Screen *s,const SDL_Event *e,int x,int y) {(void)s;(void)e;(void)x;(void)y;screen_events++;}
void GUI_ScreenSetJoySelectState(GUI_Screen *s,int v) {(void)s;(void)v;}
void GUI_WidgetClicked(GUI_Widget *w,int x,int y) {(void)w;(void)x;(void)y;clicks++;}
void GUI_LabelSetText(GUI_Widget *w,const char *t) {if(w) snprintf(w->text,sizeof(w->text),"%s",t);}
void GUI_LabelSetTextColor(GUI_Widget *w,int r,int g,int b) {(void)w;(void)r;(void)g;(void)b;}
void GUI_WidgetSetEnabled(GUI_Widget *w,int v) {if(w){if(v)w->flags&=~WIDGET_DISABLED;else w->flags|=WIDGET_DISABLED;}}
int GUI_WidgetGetState(GUI_Widget *w) {return w?w->state:0;}
void GUI_WidgetSetState(GUI_Widget *w,int v) {if(w)w->state=v;}
int GUI_WidgetGetFlags(GUI_Widget *w) {return w?w->flags:WIDGET_DISABLED;}
void GUI_WidgetSetFlags(GUI_Widget *w,int v) {if(w)w->flags|=v;}
void GUI_WidgetClearFlags(GUI_Widget *w,int v) {if(w)w->flags&=~v;}
void GUI_TextEntrySetText(GUI_Widget *w,const char *t) {GUI_LabelSetText(w,t);}
const char *GUI_TextEntryGetText(GUI_Widget *w) {return w?w->text:"";}
void GUI_ProgressBarSetPosition(GUI_Widget *w,double v) {(void)w;(void)v;}
void GUI_CardStackShowIndex(GUI_Widget *w,int i) {(void)w;(void)i;}
void GUI_EnableInput(void) {}
void GUI_DisableInput(void) {}
void SDL_DC_EmulateMouse(SDL_bool v) {(void)v;}
int OpenMainApp(void) {return 0;}
int ConsoleIsVisible(void) {return 0;}
Event_t *AddEvent(const char *n,int t,int p,Event_func *f,void *a) {(void)n;(void)t;(void)p;(void)f;(void)a;return NULL;}
int RemoveEvent(Event_t *e) {(void)e;return 0;}
int SetEventActive(Event_t *e,int a) {(void)e;(void)a;return 0;}
uint64_t timer_ms_gettime64(void) {return clock_ms;}
kthread_t *thd_create(int d,void *(*f)(void*),void *a) {(void)d;(void)f;(void)a;static kthread_t thread;return &thread;}
int thd_join(kthread_t *t,void **r) {(void)t;(void)r;abort();}
void thd_sleep(unsigned ms) {clock_ms+=ms;if(auto_test && clock_ms>=10000)self.shutdown=1;}
void thd_pass(void) {
    clock_ms++;
    if (scan_fault == 3 && scan_buffer && !scan_injected++)
        scan_buffer[scan_buffer_size - 16] ^= 1;
}
void ds_printf(const char *f,...) {(void)f;}
const char *lib_get_name(void) {return "fixture";}
void GetAppPath(char *b,size_t n,const char *f) {(void)f;snprintf(b,n,"/tmp");}
int FileExists(const char *p) {struct stat st;return !stat(map_path(p),&st)&&S_ISREG(st.st_mode);}
int DirExists(const char *p) {struct stat st;return !stat(map_path(p),&st)&&S_ISDIR(st.st_mode);}
file_t fs_open(const char *p,int f) {
    /* DC-SWAT/FatFs before 80e27b7 masks O_APPEND into the access mode,
     * and before 89f59cc writable non-truncating opens use FA_CREATE_NEW.
     * Translate that behavior explicitly; Linux O_APPEND has a different bit. */
    if((legacy_fs || reject_append) && (f&O_APPEND)) {errno=EINVAL;return -1;}
    if(legacy_fs && (f&(O_WRONLY|O_RDWR)) && !(f&O_TRUNC)) f|=O_CREAT|O_EXCL;
    if(fail_crc && strlen(p)>4 && !strcmp(p+strlen(p)-4,".crc") && (f&(O_WRONLY|O_RDWR))) {
        errno=ENOSPC;return -1;
    }
    int fd = open(map_path(p),f&~O_DIR,0600);
    if ((f&O_DIR) && fd >= 0 && fd < 1024) dirs[fd] = fdopendir(dup(fd));
    if (scan_fault && strstr(p,".suspect")) scan_map_fd = fd;
    return fd;
}
ssize_t fs_total(file_t f) {struct stat st;return fstat(f,&st)?-1:st.st_size;}
ssize_t fs_read(file_t f,void *p,size_t n) {
    off_t offset = lseek(f,0,SEEK_CUR);
    if (scan_fault == 5 && n == 2352) { errno = EIO; return -1; }
    ssize_t rv=read(f,p,n);
    if(rv>0)read_bytes+=rv;
    if (scan_fault && n == 2352*16 && rv > 0) {
        scan_buffer = p; scan_buffer_size = rv;
        if (scan_fault == 1 && !scan_injected++) {
            const uint8_t overwrite[] = {0x60,0x2c,0x25,0x8c,0x03,0x00,0x74,0x0c};
            memcpy((uint8_t *)p + rv - 16, overwrite, sizeof(overwrite));
        }
    }
    if (scan_fault == 2 && offset <= 15*2352+2336 && offset+rv > 15*2352+2336)
        ((uint8_t *)p)[15*2352+2336-offset] ^= 1;
    if (scan_fault == 6 && n == 2352) self.rip_active = 0;
    return rv;
}
ssize_t fs_write(file_t f,const void *p,size_t n) {
    if (scan_fault == 4 && f == scan_map_fd && scan_buffer && !scan_injected++)
        scan_buffer[scan_buffer_size - 16] ^= 1;
    if (recovery_fault == 4 && n == 2352 && recovery_pass &&
            lseek(f,0,SEEK_CUR) % 2352 == 0 && !injected++)
        return write(f, p, 77); /* A torn in-place sector write. */
    if ((fault == 4 || fault == 5) && n == 2352 * 16 && !injected++) {
        if (fault == 5) {
            ssize_t rv = write(f,p,n);
            ((uint8_t *)p)[100] ^= 1; /* Mutation after the last byte was copied. */
            return rv;
        }
        ((uint8_t *)p)[100] ^= 1; /* Mutation while the storage call owns the buffer. */
    }
    return write(f,p,n);
}
int fs_close(file_t f) {if(f>=0 && f<1024 && dirs[f]){closedir(dirs[f]);dirs[f]=NULL;} return close(f);}
off_t fs_seek(file_t f,off_t o,int w) {return lseek(f,o,w);}
int fs_complete(file_t f,ssize_t *n) {*n=0;return fsync(f);}
int fs_unlink(const char *p) {return unlink(p);}
int fs_mkdir(const char *p) {return mkdir(map_path(p),0700);}
const dirent_t *fs_readdir(file_t f) {
    static dirent_t out;
    if(f<0 || f>=1024 || !dirs[f])return NULL;
    struct dirent *ent = readdir(dirs[f]); if(!ent)return NULL;
    struct stat st; if(fstatat(f,ent->d_name,&st,0))return NULL;
    snprintf(out.name,sizeof(out.name),"%s",ent->d_name);
    out.size = S_ISDIR(st.st_mode) ? -1 : st.st_size;
    out.attr = S_ISDIR(st.st_mode) ? O_DIR : 0;
    return &out;
}
int cdrom_get_status(int *s,int *t) {*s=auto_test && clock_ms>=6000 && clock_ms<7000 ? CD_STATUS_OPEN : CD_STATUS_STANDBY;*t=CD_GDROM;return ERR_OK;}
int cdrom_change_datatype(cd_read_sec_part_t p,int t,int size) {
    (void)p;(void)t;
    if(size==2352 && mode_failures>0) {mode_failures--;return ERR_SYS;}
    mode=size<0?2048:(unsigned)size;return ERR_OK;
}
uint32_t cdrom_locate_data_track(cd_toc_t *t) {(void)t;return 45150;}
int cdrom_exec_cmd_timed(cd_cmd_code_t c,void *p,uint32_t timeout) {
    (void)timeout;
    if (full_thread && c == CD_CMD_GETTOC2) {
        cd_cmd_toc_params_t *req = p;
        memset(req->buffer, 0, sizeof(*req->buffer));
        req->buffer->first = req->buffer->last = (req->area == CD_AREA_HIGH ? 3 : 1) << 16;
        req->buffer->entry[req->area == CD_AREA_HIGH ? 2 : 0] = (4u << 28) | drive_base;
        req->buffer->leadout_sector = drive_base + 32 + (req->area == CD_AREA_LOW ? 150 : 0);
        return ERR_OK;
    }
    if(c!=CD_CMD_PIOREAD)return ERR_OK;
    cd_read_params_t *req=p;read_calls++;
    if(fault==2 && req->start_sec<=45151 && req->start_sec+req->num_sec>45151)return ERR_SYS;
    if(fault==3 && req->start_sec>=600)return ERR_SYS;
    size_t n=req->num_sec*mode;
    if (mode == 2048) {
        for (size_t i=0; i<req->num_sec; ++i)
            if(pread(drive_fd,(uint8_t*)req->buffer+i*2048,2048,
                (off_t)(req->start_sec-drive_base+i)*2352+16)!=2048)return ERR_SYS;
    } else if(pread(drive_fd,req->buffer,n,(off_t)(req->start_sec-drive_base)*mode)!=(ssize_t)n)return ERR_SYS;
    if(auto_test) {gd_ripper_StartRip(NULL);assert(!self.request);}
    if(fault==1 && !injected++){((uint8_t*)req->buffer)[100]^=1;}
    return ERR_OK;
}

static void setup(void) {
    static App_t app={.state=APP_STATE_OPENED};
    self.app=&app;self.use_bin=true;self.max_attempts=3;self.rip_active=1;
    self.start_time=timer_ms_gettime64();
    self.message=host_widget("message");self.track_label=host_widget("track-label");
    self.gname=host_widget("gname-text");self.num_read=host_widget("num-read");
    self.start_btn=host_widget("start_btn");self.cancel_btn=host_widget("cancel_btn");
    self.advanced_btn=host_widget("advanced-btn");self.exit_btn=host_widget("exit-btn");
    self.verify_btn=host_widget("verify-btn");self.browse_btn=host_widget("browse-btn");
    self.bad=host_widget("bad_btn");self.use_bin_btn=host_widget("use_bin_btn");
    self.edc_btn=host_widget("edc-btn");
    self.recover_btn=host_widget("recover-btn");
}

static int mock_recovery_read(void *data, uint8_t *buffer, uint32_t fad) {
    (void)data;
    if (recovery_fault == 1 && fad == 45151) { read_calls++; return 1; }
    if (recovery_fault == 2 && fad == 45151 && recovery_pass < 3) { read_calls++; return 1; }
    if (recovery_fault == 5) return -1;
    int rv = timed_cdrom_read(buffer, fad, 1);
    if (recovery_fault == 3 && read_calls % 2) buffer[100] ^= 1; /* Disagreeing audio. */
    return rv == ERR_OK ? 0 : 1;
}

static void mock_recovery_progress(void *data, uint32_t pass, uint32_t fad,
        uint32_t remaining, bool recovered) {
    (void)data; (void)fad; (void)remaining;
    recovery_pass = pass;
    if (recovered && ++recovered_count == stop_after) self.rip_active = 0;
}

int main(int argc,char **argv) {
    if(argc<2)return 2;
    setup();
    if (!strcmp(argv[1], "default-destination") || !strcmp(argv[1], "device-destination")) {
        const char *device = argc>3 ? argv[3] : "sd";
        if(!strcmp(device,"sd") || !strcmp(device,"both")) mount_root=argv[2];
        if(!strcmp(device,"ide") || !strcmp(device,"both")) ide_mount_root=argv[2];
        if(!strcmp(device,"pc")) pc_mount_root=argv[2];
        static App_t app = {.state = APP_STATE_OPENED};
        gd_ripper_Init(&app,NULL);
        if(!strcmp(argv[1],"device-destination")) {
            char name[32]; snprintf(name,sizeof(name),"device-%s",device);
            gd_ripper_Destination(host_widget(name));
            printf("%s|%d\n",self.folders.path,self.folders.valid); return 0;
        }
        int rv=gd_prepare_destination(self.selected_path);
        printf("%s|%d|%d\n",self.selected_path,rv,DirExists(self.selected_path));
        return 0;
    }
    if (!strcmp(argv[1], "input-once")) {
        SDL_Event e = {.type = SDL_KEYDOWN}; e.key.keysym.sym = SDLK_a;
        focused = self.gname; input_event(NULL,&e,EVENT_ACTION_UPDATE);
        assert(screen_events == 1 && e.type == SDL_NOEVENT);
        focused = NULL; e.type = SDL_MOUSEBUTTONUP;
        input_event(NULL,&e,EVENT_ACTION_UPDATE);
        assert(screen_events == 2 && e.type == SDL_NOEVENT);
        puts("ok"); return 0;
    }
    if (!strcmp(argv[1], "folders")) {
        mount_root = argv[2]; strcpy(self.selected_path,"/sd");
        self.destination_path = host_widget("destination-path");
        gd_ripper_ShowFileBrowser(NULL);
        assert(self.folders.count == 7 && self.folders.total == 19);
        assert(!strcmp(self.folders.names[0],"00 dump"));
        char first[NAME_MAX]; strcpy(first,self.folders.names[0]);
        gd_ripper_Folder(host_widget("folder-next")); assert(self.folders.offset == 7);
        gd_ripper_Folder(host_widget("folder-next")); assert(self.folders.offset == 14 && self.folders.count == 5);
        gd_ripper_Folder(host_widget("folder-prev")); assert(self.folders.offset == 7);
        gd_ripper_Folder(host_widget("folder-prev")); assert(!strcmp(first,self.folders.names[0]));
        gd_ripper_Folder(host_widget("folder-0")); assert(selected_is_dump());
        gd_ripper_FileBrowserConfirm(NULL);
        assert(!strcmp(self.selected_path,"/sd") && !strcmp(self.chosen_name,"00 dump"));
        gd_ripper_Gamename(); assert(!strcmp(GUI_TextEntryGetText(self.gname),"00 dump"));
        gd_ripper_ShowFileBrowser(NULL);
        gd_ripper_Folder(host_widget("folder-1"));
        assert(!strcmp(self.folders.path,"/sd/01 empty") && !self.folders.count);
        gd_ripper_ShowMainPage(NULL); assert(!strcmp(self.selected_path,"/sd"));
        gd_ripper_ShowFileBrowser(NULL); gd_ripper_Folder(host_widget("folder-up"));
        assert(!strcmp(self.folders.path,"/sd"));
        gd_ripper_Destination(host_widget("device-ide")); assert(!self.folders.valid);
        gd_ripper_FileBrowserConfirm(NULL); assert(!strcmp(self.selected_path,"/sd"));
        assert(!folder_root("/sdcard") && !folder_root("/cd"));
        puts("ok"); return 0;
    }
    if (!strcmp(argv[1], "replace-crc")) {
        printf("%08x\n", gd_crc_replace(strtoul(argv[2],NULL,16), strtoul(argv[3],NULL,16),
            strtoul(argv[4],NULL,16), strtoull(argv[5],NULL,10))); return 0;
    }
    if (!strcmp(argv[1], "recover")) {
        gd_recovery_result_t result;
        drive_fd = open(argv[2], O_RDONLY); assert(drive_fd >= 0);
        uint32_t count = fs_total(drive_fd)/2352;
        recovery_fault = atoi(argv[4]); stop_after = atoi(argv[5]);
        int rv = gd_recover_track(argv[3], 3, 45150, count, atoi(argv[6]), true,
            atoi(argv[7]), &self.rip_active, mock_recovery_read, mock_recovery_progress, NULL, &result);
        printf("%d|%u|%u|%08x|%d|%llu|%s\n", rv, result.recovered, result.remaining, result.crc,
            read_calls, (unsigned long long)read_bytes, result.error ? result.error : "OK");
        close(drive_fd); return 0;
    }
    if (!strcmp(argv[1], "recovery-status")) {
        self.track_count = 1;
        self.tracks[0] = (track_info_t){.track_num=3, .start_lba=45150,
            .sector_count=32, .type=atoi(argv[3]), .filename="track03.bin"};
        scan_fault = argc > 4 ? atoi(argv[4]) : 0;
        int rv = count_recovery_targets(argv[2]);
        if (rv == CMD_OK) show_recovery_prompt();
        printf("%d|%u|%u|%u|%d|%s|%s|%s|%d\n", rv,
            self.recovery_totals.flagged, self.recovery_totals.recovered,
            self.recovery_totals.remaining, self.recovery_totals.pending,
            host_widget("recovery-count")->text, host_widget("recovery-history")->text,
            host_widget("recovery-start")->text, read_calls);
        return 0;
    }
    if (!strcmp(argv[1], "recovery-live-counts")) {
        self.speed_label = host_widget("speed-label");
        self.time_label = host_widget("time-label");
        self.recovery_totals = (gd_recovery_status_t){.flagged=8, .recovered=2, .remaining=6, .pending=true};
        self.recovery_tracks[0] = (gd_recovery_status_t){.flagged=3, .recovered=2, .remaining=1, .pending=true};
        recovery_context_t context = {3, 4, 0, 0, &self.recovery_tracks[0]};
        recovery_progress(&context, 0, 45151, 3, false);
        assert(self.recovery_totals.remaining == 6); /* Reconciliation must not inflate it. */
        recovery_progress(&context, 1, 45151, 1, false);
        recovery_progress(&context, 1, 45151, 0, true);
        assert(self.recovery_totals.remaining == 5 && self.recovery_totals.recovered == 3);
        assert(!strcmp(self.speed_label->text, "Total: 5 unresolved"));
        assert(!strcmp(self.time_label->text, "3 / 8 recovered"));
        puts("ok"); return 0;
    }
    if (!strcmp(argv[1], "thread")) {
        full_thread = 1;
        drive_fd = open(argv[2], O_RDONLY); assert(drive_fd >= 0);
        snprintf(self.rip_destination,sizeof(self.rip_destination),"%s",argv[3]);
        snprintf(self.rip_name,sizeof(self.rip_name),"fixture");
        snprintf(self.database_path,sizeof(self.database_path),"%s/redump.db",argv[3]);
        self.recovery_mode = true; self.advanced = true; fault = atoi(argv[4]);
        gd_ripper_thread(atoi(argv[5]) ? (void*)1 : NULL);
        printf("%d|%d|%s|%s\n", self.recovery_prompt, self.page,
            self.failure_stage ? self.failure_stage : "OK", self.track_label->text);
        close(drive_fd); return 0;
    }
    if(!strcmp(argv[1],"transition")) {
        char path[NAME_MAX];
        legacy_fs=!strcmp(argv[4],"legacy");reject_append=!strcmp(argv[4],"append");
        fail_crc=!strcmp(argv[4],"crc");fault=!strcmp(argv[4],"audio-read")?3:0;
        mode_failures=!strcmp(argv[4],"mode")?2:0;
        drive_base=150;drive_fd=open(argv[3],O_RDONLY);assert(drive_fd>=0);
        strcpy(self.sync_mount,"/sd");
        int rv=check_storage(argv[2]);
        if(rv==CMD_OK) {
            snprintf(self.log_path,sizeof(self.log_path),"%s/rip.log",argv[2]);
            assert(rip_log("Transition fixture start")==CMD_OK);
            self.track_count=2;self.last_track=2;self.total_sectors=826;
            self.tracks[0]=(track_info_t){.track_num=1,.start_lba=150,.sector_count=300,.type=4,.filename="track01.bin"};
            self.tracks[1]=(track_info_t){.track_num=2,.start_lba=600,.sector_count=526,.type=0,.filename="track02.raw"};
            rv=process_tracks(argv[2],path);
        }
        printf("%d|%llu|%d|%s|%s\n",rv,(unsigned long long)self.processed_sectors,read_calls,
            self.failure_stage?self.failure_stage:"OK",self.failure_detail);
        close(drive_fd);return 0;
    }
    if(!strcmp(argv[1],"sector")) {
        uint8_t data[2352];FILE *f=fopen(argv[2],"rb");assert(f);
        assert(fread(data,1,sizeof(data),f)==sizeof(data));fclose(f);
        printf("%u\n",gd_check_sector(data,strtoul(argv[3],NULL,10)));return 0;
    }
    if(!strcmp(argv[1],"journal")) {
        uint64_t bytes=0;uint32_t crc=0;
        bool ok=gd_crc_restore(argv[2],gd_crc_tag(3,45150,32,2352),strtoull(argv[3],NULL,10),2352,&bytes,&crc);
        printf("%d %llu %08x\n",ok,(unsigned long long)bytes,crc);return 0;
    }
    if(!strcmp(argv[1],"verify")) {
        gd_verify_summary_t summary;
        scan_fault = argc > 5 ? atoi(argv[5]) : 0;
        gd_verify_result_t rv=gd_verify_dump_ex(argv[2],argv[3],false,&self.rip_active,NULL,NULL,&summary,atoi(argv[4]),true);
        printf("%d %u %llu\n",rv,summary.suspect_sectors,(unsigned long long)read_bytes);return 0;
    }
    if(!strcmp(argv[1],"identity")) {
        drive_fd=open(argv[2],O_RDONLY);assert(drive_fd>=0);
        printf("%d\n",check_disc_identity(argv[3],atoi(argv[4]),CD_GDROM));close(drive_fd);return 0;
    }
    if(!strcmp(argv[1],"detect")) {
        drive_fd=open(argv[2],O_RDONLY);assert(drive_fd>=0);
        auto_test=1;self.worker=(kthread_t*)1;self.rip_active=0;service_thread(NULL);
        assert(read_calls==2);assert(self.disc_header_valid);assert(self.disc_ready);
        puts(self.gname->text);close(drive_fd);return 0;
    }
    if(!strcmp(argv[1],"controls")) {
        self.worker=(kthread_t*)1;self.disc_ready=true;
        self.busy=1;gd_ripper_CancelRip(NULL);assert(!self.rip_active);assert(self.busy);
        queue_operation(1);assert(!self.request); /* A second operation cannot overlap stopping. */
        self.busy=0;refresh_controls();select_page(0);assert(self.focus==0);
        focus_step(1);assert(self.focus==2); /* Disabled Stop is skipped. */
        SDL_Event event={.type=SDL_JOYBUTTONDOWN};event.jbutton.button=SDL_DC_A;
        input_event(NULL,&event,EVENT_ACTION_UPDATE);assert(clicks==1);
        puts("ok");return 0;
    }
    if(!strcmp(argv[1],"recovery-controls")) {
        self.worker=(kthread_t*)1; self.disc_ready=true; self.recovery_prompt=true;
        strcpy(self.recovery_name,"original_disc"); strcpy(self.recovery_destination,"/sd");
        strcpy(self.selected_path,"/ide"); GUI_TextEntrySetText(self.gname,"new_disc");
        select_page(3); focus_step(1); assert(self.focus==1);
        queue_operation(3);
        assert(self.request==3 && self.busy);
        assert(!strcmp(self.rip_name,"original_disc") && !strcmp(self.rip_destination,"/sd"));
        assert(self.recovery_mode && self.use_bin && self.advanced && !self.zero_fill);
        puts("ok");return 0;
    }
    if(!strcmp(argv[1],"rip") || !strcmp(argv[1],"firstpass")) {
        drive_fd=open(argv[2],O_RDONLY);assert(drive_fd>=0);
        self.advanced=atoi(argv[4]);fault=atoi(argv[5]);self.total_sectors=fs_total(drive_fd)/2352;
        self.recovery_mode=!strcmp(argv[1],"firstpass");
        int rv=rip_sec(3,45150,self.total_sectors,4,argv[3]);
        printf("%d %d %llu %08x %llu\n",rv,read_calls,(unsigned long long)self.processed_sectors,
            self.current_crc,(unsigned long long)read_bytes);close(drive_fd);return 0;
    }
    return 2;
}
