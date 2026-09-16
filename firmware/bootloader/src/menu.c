/**
 * DreamShell bootloader menu
 * (c)2011-2026 SWAT <http://www.dc-swat.ru>
 * NeXT recovery controls and configuration (c)2026 TPMJB.
 */
#include "main.h"
#include "fs.h"
#include <fatfs.h>
#include <stdio.h>
#include <string.h>

#define ITEM_MAX 64
#define DEVICE_MAX 16
#define VISIBLE_ITEMS 6

typedef struct {
    char path[BOOT_PATH_MAX], label[80];
    boot_format_t format;
} boot_item_t;
typedef struct {
    boot_item_t items[ITEM_MAX];
    int count, selected;
    boot_config_t config;
    char config_path[BOOT_PATH_MAX], notice[160];
    bool hold;
    uint32 detect_ms;
} inventory_t;
typedef enum { JOB_NONE, JOB_LOAD, JOB_SCAN } job_kind_t;
typedef struct {
    job_kind_t kind;
    bool done, cancel;
    char path[BOOT_PATH_MAX], message[160];
    boot_format_t format;
    boot_stage_t stage;
    boot_image_t image;
    uint32 count, total, load_ms;
} boot_job_t;

static inventory_t inventory, scanned;
static boot_job_t job;
static kthread_t *worker;
static mutex_t job_mutex=MUTEX_INITIALIZER;
static pvr_ptr_t txr_font;
static boot_countdown_t countdown;
static bool details;
static uint32 last_buttons;
static uint64_t repeat_at;

static uint32 buttons(void) {
    maple_device_t *device=maple_enum_type(0,MAPLE_FUNC_CONTROLLER);
    cont_state_t *state=device ? maple_dev_status(device) : NULL;
    return state ? state->buttons : 0;
}

int show_message(const char *fmt, ...) {
    va_list args;
    mutex_lock(&job_mutex);
    va_start(args,fmt);
    int result=vsnprintf(job.message,sizeof(job.message),fmt,args);
    va_end(args);
    mutex_unlock(&job_mutex);
    return result;
}

bool menu_graphics_init(void) {
    /* BIOS font rendering writes individual 16-bit pixels. Build in system
     * RAM, then upload through KOS: direct halfword stores to texture VRAM
     * can corrupt adjacent pixels on real hardware. Explicit ARGB1555 colors
     * and depth also keep the atlas independent of framebuffer settings. */
    const size_t bytes=256*256*sizeof(uint16);
    uint16 *atlas=aligned_alloc(32,bytes);
    if(!atlas) return false;
    txr_font=pvr_mem_malloc(bytes);
    if(!txr_font) { free(atlas); return false; }
    memset(atlas,0,bytes);
    /* Four transparent columns and eight rows prevent filtered glyph bleed. */
    for(unsigned c=32; c<127; ++c)
        bfont_draw_ex(atlas+(c/16)*32*256+(c%16)*16,256,
                      0xffff,0,16,true,c,false,false);
    pvr_txr_load(atlas,txr_font,bytes);
    free(atlas);
    last_buttons=buttons();
    if(last_buttons) countdown.armed=false;
    return true;
}

/* Draw one BIOS glyph (12x24 before scaling). */
static void draw_char(float x1, float y1, float z1, float a, float r,
	float g, float b, int c, float scale) {
	pvr_vertex_t	vert;
	int ix, iy;
	float u1, v1, u2, v2;

	if (c == ' ')
		return;
	
	if(c > ' ' && c < 127) {
	
		ix = (c % 16) * 16;
		iy = (c / 16) * 32;
		u1 = ix * 1.0f / 256.0f;
		v1 = iy * 1.0f / 256.0f;
		u2 = (ix+12) * 1.0f / 256.0f;
		v2 = (iy+24) * 1.0f / 256.0f;

		vert.flags = PVR_CMD_VERTEX;
		vert.x = x1;
		vert.y = y1 + 24 * scale;
		vert.z = z1;
		vert.u = u1;
		vert.v = v2;
		vert.argb = PVR_PACK_COLOR(a, r, g, b);
		vert.oargb = 0;
		pvr_prim(&vert, sizeof(vert));
		
		vert.x = x1;
		vert.y = y1;
		vert.u = u1;
		vert.v = v1;
		pvr_prim(&vert, sizeof(vert));
		
		vert.x = x1 + 12 * scale;
		vert.y = y1 + 24 * scale;
		vert.u = u2;
		vert.v = v2;
		pvr_prim(&vert, sizeof(vert));

		vert.flags = PVR_CMD_VERTEX_EOL;
		vert.x = x1 + 12 * scale;
		vert.y = y1;
		vert.u = u2;
		vert.v = v1;
		pvr_prim(&vert, sizeof(vert));
	}
}

/* draw len chars at string */
static void draw_string(float x, float y, float z, float a, float r, float g,
		float b, char *str, int len, float scale) {
	int i;
	pvr_poly_cxt_t cxt;
	pvr_poly_hdr_t poly;

	pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED,
		256, 256, txr_font, PVR_FILTER_BILINEAR);
	pvr_poly_compile(&poly, &cxt);
	pvr_prim(&poly, sizeof(poly));

	for (i = 0; i < len; i++) {
		draw_char(x, y, z, a, r, g, b, str[i], scale);
		x += 12 * scale;
	}
}

/* draw a box (used by cursor and border, etc) (at 1.0f z coord) */
static void draw_box(float x, float y, float w, float h, float z, float a, float r, float g, float b) {
	pvr_poly_cxt_t	cxt;
	pvr_poly_hdr_t	poly;
	pvr_vertex_t	vert;

	pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
	pvr_poly_compile(&poly, &cxt);
	pvr_prim(&poly, sizeof(poly));

	vert.flags = PVR_CMD_VERTEX;
	vert.x = x;
	vert.y = y + h;
	vert.z = z;
	vert.u = vert.v = 0.0f;
	vert.argb = PVR_PACK_COLOR(a, r, g, b);
	vert.oargb = 0;
	pvr_prim(&vert, sizeof(vert));

	vert.y -= h;
	pvr_prim(&vert, sizeof(vert));

	vert.y += h;
	vert.x += w;
	pvr_prim(&vert, sizeof(vert));

	vert.flags = PVR_CMD_VERTEX_EOL;
	vert.y -= h;
	pvr_prim(&vert, sizeof(vert));
}




static void line(float x, float y, float scale, float r, float g, float b,
                 const char *text, int max_chars) {
    int length=(int)strlen(text);
    if(length>max_chars) length=max_chars;
    draw_string(x,y,101.0f,1.0f,r,g,b,(char *)text,length,scale);
}

static bool core_exists(const char *path) {
    if(!boot_core_path_valid(path)) return false;
    file_t fd=fs_open(path,O_RDONLY);
    if(fd==FILEHND_INVALID) return false;
    return fs_close(fd)==0;
}

static void add_item(inventory_t *out, const char *path) {
    for(int i=0; i<out->count; ++i)
        if(!strcmp(out->items[i].path,path)) return;
    if(!core_exists(path)) return;
    if(out->count==ITEM_MAX) {
        out->hold=true;
        snprintf(out->notice,sizeof(out->notice),"Too many cores; set core_path in boot.cfg");
        return;
    }
    boot_item_t *item=&out->items[out->count++];
    snprintf(item->path,sizeof(item->path),"%s",path);
    item->format=boot_path_format(path);
    const char *filename=strrchr(path,'/');
    const char *device_end=strchr(path+1,'/');
    snprintf(item->label,sizeof(item->label),"%.*s  /  %s",
             (int)(device_end-path-1),path+1,filename+1);
}

static bool read_config(const char *path, inventory_t *out) {
    file_t fd=fs_open(path,O_RDONLY);
    if(fd==FILEHND_INVALID) return false;
    snprintf(out->config_path,sizeof(out->config_path),"%s",path);
    char data[4097];
    ssize_t size=fs_total(fd);
    size_t count=0;
    bool valid=size>=0 && size<=4096;
    while(valid && count<(size_t)size) {
        ssize_t got=fs_read(fd,data+count,(size_t)size-count);
        if(got<=0 || got>size-(ssize_t)count) { valid=false; break; }
        count+=(size_t)got;
    }
    if(valid) {
        char extra;
        if(fs_read(fd,&extra,1)!=0) valid=false;
    }
    if(fs_close(fd)) valid=false;
    unsigned bad_line=valid ? boot_config_parse(data,count,&out->config) : 1;
    if(bad_line) {
        out->hold=true;
        snprintf(out->notice,sizeof(out->notice),
                 "boot.cfg error at line %u; using defaults",bad_line);
    }
    return true;
}

static void scan(inventory_t *out) {
    static const char *normal[]={"/DS/DS_CORE.BIN","/DS_CORE.BIN","/1DS_CORE.BIN",
                                 "/DS/ZDS_CORE.BIN","/ZDS_CORE.BIN"};
    static const char *alternate[]={"/DS/DEBUG_DS_CORE.BIN","/DS/EMU_DS_CORE.BIN"};
    char devices[DEVICE_MAX][16], path[BOOT_PATH_MAX];
    int count=0;
    memset(out,0,sizeof(*out));
    boot_config_defaults(&out->config);
    file_t root=fs_open("/",O_RDONLY|O_DIR);
    if(root==FILEHND_INVALID) {
        out->hold=true;
        strcpy(out->notice,"Cannot list boot devices");
        return;
    }
    const dirent_t *entry;
    while((entry=fs_readdir(root))!=NULL) {
        if(!RootDeviceIsSupported(entry->name) || strlen(entry->name)>=16) continue;
        if(count==DEVICE_MAX) { out->hold=true; break; }
        snprintf(devices[count++],sizeof(devices[0]),"%s",entry->name);
    }
    fs_close(root);

    /* Settings lookup is independent of boot order: SD, then IDE. */
    bool found=false;
    for(int type=0; type<2 && !found; ++type) {
        for(int part=0; part<4 && !found; ++part) {
            char name[16];
            if(part) snprintf(name,sizeof(name),"%s%d",type ? "ide" : "sd",part);
            else snprintf(name,sizeof(name),"%s",type ? "ide" : "sd");
            for(int i=0; i<count && !found; ++i) if(!strcmp(devices[i],name)) {
                snprintf(path,sizeof(path),"/%s/DS/boot.cfg",name);
                found=read_config(path,out);
                if(!found) {
                    snprintf(path,sizeof(path),"/%s/boot.cfg",name);
                    found=read_config(path,out);
                }
            }
        }
    }
    /* Stable sort preserves the original root order for boot_order=auto. */
    for(int i=1; i<count; ++i) {
        char value[16]; strcpy(value,devices[i]);
        int j=i;
        while(j && boot_device_rank(&out->config,devices[j-1])>
                   boot_device_rank(&out->config,value)) {
            strcpy(devices[j],devices[j-1]); --j;
        }
        strcpy(devices[j],value);
    }
    if(*out->config.core_path) add_item(out,out->config.core_path);
    if(*out->config.fallback_path) add_item(out,out->config.fallback_path);
    bool configured_found=out->count>0;
    for(int i=0; i<count; ++i)
        for(unsigned n=0; n<sizeof(normal)/sizeof(*normal); ++n) {
            snprintf(path,sizeof(path),"/%s%s",devices[i],normal[n]);
            add_item(out,path);
        }
    int automatic_count=out->count;
    for(int i=0; i<count; ++i)
        for(unsigned n=0; n<sizeof(alternate)/sizeof(*alternate); ++n) {
            snprintf(path,sizeof(path),"/%s%s",devices[i],alternate[n]);
            add_item(out,path);
        }
    if(!out->count) {
        out->hold=true;
        if(!*out->notice) strcpy(out->notice,"No cores found. Insert SD and press X to rescan.");
    } else if(!automatic_count) {
        out->hold=true;
        if(!*out->notice) strcpy(out->notice,"Choose an alternate core and press A to boot.");
    } else if((*out->config.core_path || *out->config.fallback_path) && !configured_found) {
        out->hold=true;
        if(!*out->notice) strcpy(out->notice,"Configured core not found. Choose a core or edit boot.cfg.");
    }
}

int menu_init(void) {
    scan(&inventory);
    inventory.detect_ms=boot_detect_ms;
    details=inventory.config.diagnostics;
    if(*inventory.notice) show_message("%s",inventory.notice);
    else show_message("Choose a core. A boots; X refreshes devices.");
    boot_countdown_start(&countdown,timer_ms_gettime64(),&inventory.config,
                         start_pressed || inventory.hold,inventory.count>0);
    return 0;
}

static bool progress(boot_stage_t stage, uint32 count, uint32 total, void *arg) {
    bool cancelled;
    mutex_lock(&job_mutex);
    job.stage=stage; job.count=count; job.total=total;
    cancelled=job.cancel;
    mutex_unlock(&job_mutex);
    if(arg && (start_pressed || (buttons() & (CONT_START|CONT_B)))) {
        start_pressed=1;
        return false;
    }
    return !cancelled;
}

static void *run_job(void *arg) {
    (void)arg;
    if(job.kind==JOB_SCAN) {
        uint32 elapsed=boot_detect_devices(true);
        scan(&scanned);
        scanned.detect_ms=elapsed;
        mutex_lock(&job_mutex);
        job.done=true;
        mutex_unlock(&job_mutex);
        return NULL;
    }
    boot_image_t result;
    uint64_t started=timer_ms_gettime64();
    boot_load(job.path,job.format,progress,NULL,&result);
    mutex_lock(&job_mutex);
    job.image=result;
    job.count=result.count; job.total=result.size;
    job.load_ms=(uint32)(timer_ms_gettime64()-started);
    job.done=true;
    mutex_unlock(&job_mutex);
    return NULL;
}

static void reset_job(job_kind_t kind) {
    mutex_lock(&job_mutex);
    memset(&job,0,sizeof(job));
    job.kind=kind;
    mutex_unlock(&job_mutex);
}

static void begin_load(bool foreground) {
    countdown.armed=false;
    if(worker) return;
    if(!inventory.count) {
        show_message("No core selected. Insert SD and press X.");
        return;
    }
    boot_item_t *item=&inventory.items[inventory.selected];
    reset_job(JOB_LOAD);
    snprintf(job.path,sizeof(job.path),"%s",item->path);
    job.format=item->format;
    show_message("Loading core...");
    if(foreground) {
        uint64_t started=timer_ms_gettime64();
        boot_load(job.path,job.format,progress,(void *)1,&job.image);
        job.count=job.image.count; job.total=job.image.size;
        job.load_ms=(uint32)(timer_ms_gettime64()-started);
        if(job.image.error==BOOT_OK) {
            arch_exec(job.image.data,job.image.size);
            free(job.image.data); job.image.data=NULL;
        }
        show_message("%s",boot_error_message(job.image.error));
        job.kind=JOB_NONE;
    } else {
        worker=thd_create(false,run_job,NULL);
        if(!worker) {
            job.kind=JOB_NONE;
            show_message("Cannot start loader. Press A to retry.");
        }
    }
}

void menu_autoboot(void) {
    if(countdown.armed && inventory.config.delay_seconds==0 && !start_pressed)
        begin_load(true);
}

static void finish_job(void) {
    if(!worker) return;
    mutex_lock(&job_mutex);
    bool done=job.done;
    mutex_unlock(&job_mutex);
    if(!done) return;
    thd_join(worker,NULL);
    worker=NULL;
    countdown.armed=false;
    if(job.kind==JOB_SCAN) {
        inventory=scanned;
        boot_detect_ms=inventory.detect_ms;
        details=inventory.config.diagnostics;
        show_message("%s",*inventory.notice ? inventory.notice :
                     "Devices refreshed. Choose a core and press A.");
    } else {
        if(job.cancel && job.image.error==BOOT_OK) {
            free(job.image.data); job.image.data=NULL;
            job.image.error=BOOT_CANCELLED;
        }
        if(job.image.error==BOOT_OK) {
            arch_exec(job.image.data,job.image.size);
            free(job.image.data); job.image.data=NULL;
        }
        show_message("%s",boot_error_message(job.image.error));
    }
    job.kind=JOB_NONE;
}

void menu_update(void) {
    if(start_pressed) countdown.armed=false;
    uint32 current=buttons();
    uint32 pressed=current & ~last_buttons;
    uint64_t now=timer_ms_gettime64();
    last_buttons=current;
    if(pressed & (CONT_DPAD_UP|CONT_DPAD_DOWN)) repeat_at=now+350;
    else if((current & (CONT_DPAD_UP|CONT_DPAD_DOWN)) && now>=repeat_at) {
        pressed |= current & (CONT_DPAD_UP|CONT_DPAD_DOWN);
        repeat_at=now+120;
    }
    bool due=boot_countdown_due(&countdown,now,current!=0);
    if(worker) {
        if(pressed & (CONT_B|CONT_START)) {
            mutex_lock(&job_mutex);
            if(job.kind==JOB_LOAD) job.cancel=true;
            mutex_unlock(&job_mutex);
        }
        if(pressed & CONT_Y) details=!details;
        finish_job();
        return;
    }
    if(pressed & CONT_Y) details=!details;
    if(pressed & CONT_B) {
        details=false;
        show_message("Automatic boot stopped. Choose a core; A boots.");
        return;
    }
    if(pressed & CONT_START) {
        show_message("Boot menu stays open until you press A.");
        return;
    }
    if(pressed & CONT_DPAD_UP)
        if(inventory.count) inventory.selected=(inventory.selected+inventory.count-1)%inventory.count;
    if(pressed & CONT_DPAD_DOWN)
        if(inventory.count) inventory.selected=(inventory.selected+1)%inventory.count;
    if(pressed & CONT_X) {
        reset_job(JOB_SCAN);
        show_message("Scanning devices...");
        worker=thd_create(false,run_job,NULL);
        if(!worker) {
            job.kind=JOB_NONE;
            show_message("Cannot start rescan. Press X to retry.");
        }
        return;
    }
    if((pressed & CONT_A) || due) begin_load(false);
}

void menu_frame(void) {
    if(!txr_font) return;
    char status[160], path[BOOT_PATH_MAX], info[120];
    uint32 count,total,load_ms;
    boot_stage_t stage;
    job_kind_t kind;
    mutex_lock(&job_mutex);
    snprintf(status,sizeof(status),"%s",job.message);
    snprintf(path,sizeof(path),"%s",job.path);
    count=job.count; total=job.total; load_ms=job.load_ms;
    stage=job.stage; kind=job.kind;
    mutex_unlock(&job_mutex);

    const float body=0.75f; /* Integer 9x18 glyphs, including on interlaced TV. */
    draw_box(24,24,592,432,100,1,0.035f,0.075f,0.13f);
    draw_box(24,24,592,3,100.5f,1,0.3f,0.9f,0.86f);
    line(40,36,1,0.9f,0.98f,1,title,46);
    line(40,67,body,0.6f,0.8f,0.85f,"TPMJB  /  github.com/TPMJB/DreamShell_NeXT",62);
    if(countdown.armed) {
        uint64_t now=timer_ms_gettime64();
        unsigned seconds=now<countdown.deadline ? (unsigned)((countdown.deadline-now+999)/1000) : 0;
        snprintf(info,sizeof(info),"Booting in %us - any button opens the menu",seconds);
    } else if(!inventory.count) snprintf(info,sizeof(info),"Waiting for storage with DreamShell installed");
    else snprintf(info,sizeof(info),"%d core%s available",inventory.count,inventory.count==1 ? "" : "s");
    line(40,92,body,0.6f,0.8f,0.85f,info,62);

    int first=inventory.selected>=VISIBLE_ITEMS ? inventory.selected-VISIBLE_ITEMS+1 : 0;
    draw_box(32,118,576,152,100.2f,1,0.055f,0.11f,0.18f);
    if(!inventory.count) {
        line(44,134,1,0.93f,0.97f,1,"Storage not ready",46);
        line(44,173,body,0.8f,0.85f,0.9f,"Insert your SD card, then press X to rescan.",61);
        line(44,197,body,0.8f,0.85f,0.9f,"It should contain DS/DS_CORE.BIN.",61);
        line(44,224,body,0.65f,0.8f,0.85f,"IDE / CF: connect before power-on.",61);
        line(44,246,body,0.65f,0.8f,0.85f,"No files are changed by this screen.",61);
    }
    for(int n=0; n<VISIBLE_ITEMS && first+n<inventory.count; ++n) {
        int index=first+n;
        float y=122+n*24;
        if(index==inventory.selected) draw_box(34,y,572,23,100.5f,1,0.13f,0.29f,0.36f);
        line(44,y+2,body,0.93f,0.97f,1,inventory.items[index].label,61);
    }
    if(inventory.count>VISIBLE_ITEMS) {
        snprintf(info,sizeof(info),"%d / %d",inventory.selected+1,inventory.count);
        line(532,272,0.5f,0.55f,0.8f,0.85f,info,12);
    }
    const char *selected=inventory.count ? inventory.items[inventory.selected].path : "";
    const char *shown=worker && kind==JOB_LOAD ? path : selected;
    line(40,286,0.5f,0.65f,0.9f,0.94f,shown,94);
    if(strlen(shown)>94) line(40,300,0.5f,0.65f,0.9f,0.94f,shown+94,94);
    if(strlen(shown)>188) line(40,314,0.5f,0.65f,0.9f,0.94f,shown+188,94);

    if(details) {
        snprintf(info,sizeof(info),"Detection %lu ms   Last load %lu ms",
                 (unsigned long)inventory.detect_ms,(unsigned long)load_ms);
        line(40,332,0.5f,0.7f,0.8f,0.9f,info,94);
        snprintf(info,sizeof(info),"Last read %lu / %lu bytes",(unsigned long)count,(unsigned long)total);
        line(40,346,0.5f,0.7f,0.8f,0.9f,info,94);
        line(40,360,0.5f,0.55f,0.75f,0.82f,
             *inventory.config_path ? inventory.config_path : "Default settings (no boot.cfg)",94);
    }
    if(worker && kind==JOB_LOAD)
        snprintf(status,sizeof(status),"%s  %lu / %lu KiB",
                 stage==BOOT_DECODING ? "Decoding" : "Reading",
                 (unsigned long)(count/1024),(unsigned long)(total/1024));
    draw_box(32,380,576,36,100.2f,1,0.06f,0.13f,0.2f);
    if(total) {
        float width=576.0f*(float)count/(float)total;
        if(width>576) width=576;
        draw_box(32,413,width,3,100.5f,1,0.3f,0.9f,0.86f);
    }
    line(40,389,body,0.95f,0.98f,1,status,62);
    line(40,424,body,0.8f,0.92f,0.95f,
         !inventory.count && !worker ? "X Rescan    Y Details" : "A Boot / Retry    B Cancel    X Rescan",62);
    line(40,444,0.5f,0.55f,0.75f,0.82f,
         !inventory.count ? "No automatic boot until a core is available." :
         "Up/Down Choose core    Y Details    Start Stay in menu",94);
}
