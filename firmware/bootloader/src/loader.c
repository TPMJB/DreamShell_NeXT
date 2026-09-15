/* DreamShell NeXT core loading. Copyright (c) 2026 TPMJB.
 * No caller may execute an image unless boot_load returns BOOT_OK. */
#include "main.h"
#include <string.h>

const char *boot_error_message(boot_error_t error) {
    static const char *messages[]={"Ready", "Cannot open core", "Invalid core size",
        "Not enough memory", "Core read failed", "Core read incomplete",
        "Core size changed", "Cannot close core", "Load cancelled",
        "Compressed core is damaged", "Cannot decode scrambled core"};
    return (unsigned)error<sizeof(messages)/sizeof(*messages) ? messages[error] : "Core load failed";
}

static bool valid_size(ssize_t size) {
    return size>=4 && size<=BOOT_CORE_MAX && !(size & 3);
}

boot_error_t boot_load(const char *path, boot_format_t format,
                       boot_progress_t progress, void *arg, boot_image_t *image) {
    file_t fd=FILEHND_INVALID;
    gzFile gz=NULL;
    boot_error_t error=BOOT_OK;
    uint8_t *data=NULL, extra;
    memset(image,0,sizeof(*image));
    fd=fs_open(path,O_RDONLY);
    if(fd==FILEHND_INVALID) { error=BOOT_OPEN; goto done; }
    ssize_t total=fs_total(fd);
    if(format==BOOT_GZIP) {
        uint8_t trailer[4];
        if(total<18 || total>BOOT_CORE_MAX+65536 || fs_seek(fd,-4,SEEK_END)!=total-4 ||
           fs_read(fd,trailer,4)!=4) { error=BOOT_SIZE; goto done; }
        total=(uint32_t)trailer[0] | ((uint32_t)trailer[1]<<8) |
              ((uint32_t)trailer[2]<<16) | ((uint32_t)trailer[3]<<24);
        int closed=fs_close(fd); fd=FILEHND_INVALID;
        if(closed) { error=BOOT_CLOSE; goto done; }
    }
    if(!valid_size(total)) { error=BOOT_SIZE; goto done; }
    image->size=(uint32_t)total;
    /* aligned_alloc requires the allocation size to be an alignment multiple. */
    data=aligned_alloc(32,(image->size+31u)&~31u);
    if(!data) { error=BOOT_MEMORY; goto done; }
    if(format==BOOT_GZIP) {
        gz=gzopen(path,"rb");
        if(!gz) { error=BOOT_OPEN; goto done; }
        /* gzread otherwise accepts uncompressed input transparently. */
        if(gzdirect(gz)) { error=BOOT_GZIP_ERROR; goto done; }
    }
    while(image->count<image->size) {
        if(progress && !progress(BOOT_READING,image->count,image->size,arg)) {
            error=BOOT_CANCELLED; goto done;
        }
        uint32_t remaining=image->size-image->count;
        size_t request=remaining<BOOT_READ_CHUNK ? remaining : BOOT_READ_CHUNK;
        ssize_t got=gz ? gzread(gz,data+image->count,(unsigned)request) :
                         fs_read(fd,data+image->count,request);
        if(got<0) { error=gz ? BOOT_GZIP_ERROR : BOOT_READ; goto done; }
        if(!got) { error=BOOT_SHORT_READ; goto done; }
        if((size_t)got>request) { error=BOOT_CHANGED; goto done; }
        image->count+=(uint32_t)got;
    }
    /* Force EOF and gzip trailer/CRC validation, with no remaining-buffer write. */
    ssize_t got=gz ? gzread(gz,&extra,1) : fs_read(fd,&extra,1);
    if(got<0) { error=gz ? BOOT_GZIP_ERROR : BOOT_READ; goto done; }
    if(got) { error=BOOT_CHANGED; goto done; }
    if(gz) {
        int gzerr=Z_OK;
        gzerror(gz,&gzerr);
        if(!gzeof(gz) || (gzerr!=Z_OK && gzerr!=Z_STREAM_END)) {
            error=BOOT_GZIP_ERROR; goto done;
        }
        int closed=gzclose(gz); gz=NULL;
        if(closed!=Z_OK) { error=BOOT_GZIP_ERROR; goto done; }
    } else {
        int closed=fs_close(fd); fd=FILEHND_INVALID;
        if(closed) { error=BOOT_CLOSE; goto done; }
    }
    if(format==BOOT_SCRAMBLED) {
        if(progress && !progress(BOOT_DECODING,image->count,image->size,arg)) {
            error=BOOT_CANCELLED; goto done;
        }
        uint8_t *decoded=aligned_alloc(32,(image->size+31u)&~31u);
        if(!decoded) { error=BOOT_MEMORY; goto done; }
        if(descramble(data,decoded,image->size)) {
            free(decoded); error=BOOT_DESCRAMBLE; goto done;
        }
        free(data); data=decoded;
    }
    if(progress && !progress(BOOT_COMPLETE,image->count,image->size,arg))
        error=BOOT_CANCELLED;
done:
    if(fd!=FILEHND_INVALID) fs_close(fd);
    if(gz) gzclose(gz);
    if(error) free(data);
    else image->data=data;
    image->error=error;
    return error;
}
