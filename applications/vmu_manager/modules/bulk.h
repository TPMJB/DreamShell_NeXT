#ifndef VMU_BULK_H
#define VMU_BULK_H

/* A fixed snapshot avoids invalidating UI rows while files are copied. A VMU
 * cannot have more than 256 directory entries or blocks. */
#define VMU_BULK_MAX 256
enum { VMU_BULK_OK, VMU_BULK_SOURCE, VMU_BULK_DESTINATION, VMU_BULK_SPACE,
       VMU_BULK_READ, VMU_BULK_WRITE, VMU_BULK_CANCELLED };
typedef struct {
    vmu_dir_t entry;
    char name[13];
    bool skip;
} vmu_bulk_item_t;
typedef struct {
    vmu_bulk_item_t items[VMU_BULK_MAX];
    int count, pending, copied, skipped, error;
    char failed_name[13];
} vmu_bulk_plan_t;
typedef bool (*vmu_bulk_progress_t)(int done, int total, const char *name, void *arg);

static bool vmu_bulk_destination(const char *path) {
    /* The PC-link driver does not implement exclusive creation. */
    return path && ((!strncmp(path,"/sd",3) && (!path[3] || path[3]=='/')) ||
                    (!strncmp(path,"/ide",4) && (!path[4] || path[4]=='/')) ||
                    !strncmp(path,"/vmu/",5));
}

static bool vmu_bulk_path(char *path, size_t size, const char *base, const char *name) {
    return snprintf(path,size,"%s/%s.vms",base,name)<(int)size;
}

static bool vmu_bulk_prepare(maple_device_t *source, maple_device_t *target,
                             const char *base, vmu_bulk_plan_t *plan) {
    vmu_dir_t *entries=NULL, *destination=NULL;
    int count=0, target_count=0, blocks=0;
    char path[NAME_MAX];
    memset(plan,0,sizeof(*plan));
    if(!source) { plan->error=VMU_BULK_SOURCE; return false; }
    if(source==target || !vmu_bulk_destination(base) || vmu_ui_read_only(base) ||
       (!strncmp(base,"/vmu",4) && !target)) {
        plan->error=VMU_BULK_DESTINATION; return false;
    }
    if(vmufs_readdir(source,&entries,&count)<0 || count<0 || count>VMU_BULK_MAX || (count && !entries)) {
        plan->error=VMU_BULK_SOURCE; goto done;
    }
    if(target) {
        if(vmufs_readdir(target,&destination,&target_count)<0 || target_count<0 ||
           target_count>VMU_BULK_MAX || (target_count && !destination)) {
            plan->error=VMU_BULK_DESTINATION; goto done;
        }
    }
    for(int i=0;i<count;++i) {
        if(!entries[i].filetype) continue;
        vmu_bulk_item_t *item=&plan->items[plan->count];
        item->entry=entries[i];
        memcpy(item->name,entries[i].filename,12); item->name[12]=0;
        snprintf(plan->failed_name,sizeof(plan->failed_name),"%s",item->name);
        if(!*item->name || !strcmp(item->name,".") || !strcmp(item->name,"..") ||
           strchr(item->name,'/') || strchr(item->name,'\\') ||
           !item->entry.filesize || item->entry.filesize>VMU_BULK_MAX ||
           (item->entry.filetype!=0x33 && item->entry.filetype!=0xcc)) {
            plan->error=VMU_BULK_SOURCE; goto done;
        }
        if(target) {
            for(int j=0;j<target_count;++j)
                if(destination[j].filetype && !strncmp(destination[j].filename,item->name,12)) item->skip=true;
        } else {
            char filename[17]; struct stat st;
            snprintf(filename,sizeof(filename),"%s.vms",item->name);
            if(!vmu_ui_folder_name(filename) || !vmu_bulk_path(path,sizeof(path),base,item->name)) {
                plan->error=VMU_BULK_DESTINATION; goto done;
            }
            errno=0;
            if(fs_stat(path,&st,0)==0) item->skip=true;
            else if(errno!=ENOENT) { plan->error=VMU_BULK_DESTINATION; goto done; }
        }
        if(!item->skip) { ++plan->pending; blocks+=item->entry.filesize; }
        ++plan->count;
    }
    if(target) {
        int available=vmufs_free_blocks(target);
        if(available<0) plan->error=VMU_BULK_DESTINATION;
        else if(blocks>available) plan->error=VMU_BULK_SPACE;
    }
done:
    free(entries); free(destination);
    if(!plan->error) plan->failed_name[0]=0;
    return plan->error==VMU_BULK_OK;
}

/* Only storage files use O_EXCL. The VMU VFS ignores that flag, so card-to-card
 * copies use vmufs_write without VMUFS_OVERWRITE instead. */
static int vmu_bulk_write_file(const char *path, const void *data, int size) {
    file_t fd=fs_open(path,O_WRONLY|O_CREAT|O_EXCL);
    if(fd==FILEHND_INVALID) return errno==EEXIST?1:-1;
    bool ok=fs_write(fd,data,(size_t)size)==size;
    if(ok && (!strncmp(path,"/sd/",4) || !strncmp(path,"/ide/",5))) {
        ssize_t complete=0;
        if(fs_complete(fd,&complete)) ok=false;
    }
    if(fs_close(fd)) ok=false;
    if(!ok) fs_unlink(path); /* Only the newly created partial file is ours. */
    return ok?0:-1;
}

static void vmu_bulk_copy(maple_device_t *source, maple_device_t *target,
                          const char *base, vmu_bulk_plan_t *plan,
                          vmu_bulk_progress_t progress, void *arg) {
    if(plan->error) return;
    for(int i=0;i<plan->count;++i) {
        vmu_bulk_item_t *item=&plan->items[i];
        void *data=NULL; int size=0, result;
        if(progress && !progress(i,plan->count,item->name,arg)) {
            plan->error=VMU_BULK_CANCELLED; break;
        }
        if(item->skip) { ++plan->skipped; continue; }
        snprintf(plan->failed_name,sizeof(plan->failed_name),"%s",item->name);
        if(vmufs_read_dirent(source,&item->entry,&data,&size)<0 || !data ||
           size!=(int)item->entry.filesize*512) {
            free(data); plan->error=VMU_BULK_READ; break;
        }
        if(target) {
            int flags=(item->entry.filetype==0xcc?VMUFS_VMUGAME:0) |
                      (item->entry.copyprotect?VMUFS_NOCOPY:0);
            result=vmufs_write(target,item->name,data,size,flags);
            if(result==-2) result=1; /* Existing file: never overwrite it. */
        } else {
            char path[NAME_MAX];
            result=vmu_bulk_path(path,sizeof(path),base,item->name)?vmu_bulk_write_file(path,data,size):-1;
        }
        free(data);
        if(result<0) { plan->error=VMU_BULK_WRITE; break; }
        if(result==1) ++plan->skipped; else ++plan->copied;
        plan->failed_name[0]=0;
    }
    if(!plan->error && progress) progress(plan->count,plan->count,"",arg);
}
#endif
