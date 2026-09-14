/* DreamShell NeXT boot configuration. Copyright (c) 2026 TPMJB. */
#include "boot.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void boot_config_defaults(boot_config_t *config) {
    memset(config, 0, sizeof(*config));
    strcpy(config->order, "auto");
    config->autoboot = true;
}

static int family(const char *name, size_t size) {
    static const char *names[] = {"sd", "ide", "cd", "pc", "brd"};
    for(int i=0; i<5; ++i)
        if(strlen(names[i]) == size && !strncmp(name, names[i], size)) return i;
    return -1;
}

static int device_family(const char *device) {
    size_t size = strlen(device);
    if(size && device[size-1] >= '0' && device[size-1] <= '3') --size;
    return family(device, size);
}

bool boot_core_path_valid(const char *path) {
    if(!path || path[0] != '/' || strlen(path) >= BOOT_PATH_MAX) return false;
    const char *slash = strchr(path+1, '/');
    if(!slash || !slash[1] || slash-path > 8) return false;
    char device[9];
    memcpy(device, path+1, (size_t)(slash-path-1));
    device[slash-path-1] = 0;
    if(device_family(device) < 0) return false;
    for(const char *p=slash+1; *p;) {
        const char *end = strchr(p, '/');
        size_t size = end ? (size_t)(end-p) : strlen(p);
        if(!size || (size==1 && p[0]=='.') || (size==2 && p[0]=='.' && p[1]=='.')) return false;
        for(size_t i=0; i<size; ++i)
            if((unsigned char)p[i] < 32 || p[i]=='\\' || p[i]==':') return false;
        if(!end) break;
        p=end+1;
        if(!*p) return false;
    }
    size_t size = strlen(path);
    return size > 4 && !strcasecmp(path+size-4, ".bin");
}

boot_format_t boot_path_format(const char *path) {
    const char *name = strrchr(path, '/');
    name = name ? name+1 : path;
    if(!strcasecmp(name, "1DS_CORE.BIN")) return BOOT_SCRAMBLED;
    if(!strcasecmp(name, "ZDS_CORE.BIN")) return BOOT_GZIP;
    return BOOT_RAW;
}

static bool order_valid(const char *order) {
    if(!strcmp(order, "auto")) return true;
    unsigned seen=0;
    while(*order) {
        const char *end=strchr(order, ',');
        size_t size=end ? (size_t)(end-order) : strlen(order);
        int index=family(order,size);
        if(index<0 || (seen & (1u<<index))) return false;
        seen |= 1u<<index;
        if(!end) return true;
        order=end+1;
    }
    return false;
}

int boot_device_rank(const boot_config_t *config, const char *device) {
    if(!strcmp(config->order,"auto")) return 0;
    int wanted=device_family(device), rank=0;
    const char *p=config->order;
    while(*p) {
        const char *end=strchr(p, ',');
        if(family(p,end ? (size_t)(end-p) : strlen(p))==wanted) return rank;
        ++rank;
        if(!end) break;
        p=end+1;
    }
    return 5;
}

static char *trim(char *p) {
    while(isspace((unsigned char)*p)) ++p;
    char *end=p+strlen(p);
    while(end>p && isspace((unsigned char)end[-1])) *--end=0;
    return p;
}

unsigned boot_config_parse(const char *data, size_t size, boot_config_t *config) {
    boot_config_t parsed;
    boot_config_defaults(config);
    boot_config_defaults(&parsed);
    if(size>4096 || memchr(data,0,size)) return 1;
    size_t pos=0;
    unsigned line=0;
    while(pos<size) {
        char buffer[BOOT_PATH_MAX+40];
        size_t begin=pos;
        while(pos<size && data[pos]!='\n') ++pos;
        size_t length=pos-begin;
        if(pos<size) ++pos;
        ++line;
        if(length>=sizeof(buffer)) return line;
        memcpy(buffer,data+begin,length); buffer[length]=0;
        char *comment=strchr(buffer,'#'); if(comment) *comment=0;
        char *key=trim(buffer);
        if(!*key) continue;
        char *equals=strchr(key,'=');
        if(!equals) return line;
        *equals=0;
        char *value=trim(equals+1); key=trim(key);
        if(!strcmp(key,"boot_order")) {
            if(strlen(value)>=sizeof(parsed.order) || !order_valid(value)) return line;
            strcpy(parsed.order,value);
        } else if(!strcmp(key,"core_path") || !strcmp(key,"fallback_path")) {
            if(*value && !boot_core_path_valid(value)) return line;
            strcpy(!strcmp(key,"core_path") ? parsed.core_path : parsed.fallback_path,value);
        } else if(!strcmp(key,"autoboot") || !strcmp(key,"diagnostics")) {
            if(strcmp(value,"0") && strcmp(value,"1")) return line;
            if(!strcmp(key,"autoboot")) parsed.autoboot=(*value=='1');
            else parsed.diagnostics=(*value=='1');
        } else if(!strcmp(key,"boot_delay")) {
            if(!*value) return line;
            for(char *p=value; *p; ++p) if(!isdigit((unsigned char)*p)) return line;
            if(strlen(value)>2 || strtoul(value,NULL,10)>30) return line;
            parsed.delay_seconds=(unsigned)strtoul(value,NULL,10);
        } else return line;
    }
    *config=parsed;
    return 0;
}

void boot_countdown_start(boot_countdown_t *timer, uint64_t now,
                          const boot_config_t *config, bool manual, bool have_core) {
    timer->armed=config->autoboot && !manual && have_core;
    timer->deadline=now+(uint64_t)config->delay_seconds*1000;
}

bool boot_countdown_due(boot_countdown_t *timer, uint64_t now, bool input) {
    if(input) timer->armed=false;
    if(!timer->armed || now<timer->deadline) return false;
    timer->armed=false;
    return true;
}
