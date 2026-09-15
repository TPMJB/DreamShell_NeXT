#ifndef VMU_TRANSFER_H
#define VMU_TRANSFER_H

/* Read the complete save before opening an overwrite target. In particular,
 * a failed/short source read must not truncate an existing VMU save. */
static int vmu_transfer_file(const char *src, const char *dst, bool dci) {
    file_t in=FILEHND_INVALID,out=FILEHND_INVALID;
    uint8_t *data=NULL;
    ssize_t length;
    int result=CMD_ERROR, flags=O_RDONLY;
    if(!strcmp(src,dst) || vmu_ui_read_only(dst)) return CMD_ERROR;
    if(!strncmp(src,"/vmu/",5)) flags|=O_META;
    in=fs_open(src,flags);
    if(in==FILEHND_INVALID) goto done;
    length=fs_total(in);
    if(dci) {
        if(length<=32 || length>131104 || ((length-32)&3) || fs_seek(in,32,SEEK_SET)!=32) goto done;
        length-=32;
    }
    if(length<=0 || length>131072) goto done;
    data=memalign(32,(size_t)length);
    if(!data) goto done;
    size_t read_bytes=0;
    while(read_bytes<(size_t)length) {
        ssize_t got=fs_read(in,data+read_bytes,(size_t)length-read_bytes);
        if(got<=0 || (size_t)got>(size_t)length-read_bytes) goto done;
        read_bytes+=(size_t)got;
    }
    int close_result=fs_close(in); in=FILEHND_INVALID;
    if(close_result) goto done;
    if(dci) for(size_t i=0;i<(size_t)length;i+=4) {
        uint8_t a=data[i],b=data[i+1]; data[i]=data[i+3]; data[i+1]=data[i+2]; data[i+2]=b; data[i+3]=a;
    }
    flags=O_WRONLY|O_CREAT|O_TRUNC;
    if(!strncmp(dst,"/vmu/",5)) flags|=O_META;
    out=fs_open(dst,flags);
    if(out==FILEHND_INVALID) goto done;
    /* Treat a short write as failure; never turn it into apparent success. */
    if(fs_write(out,data,(size_t)length)!=length) goto done;
    if(!strncmp(dst,"/sd/",4) || !strncmp(dst,"/ide/",5)) {
        ssize_t complete=0;
        if(fs_complete(out,&complete)) goto done;
    }
    close_result=fs_close(out); out=FILEHND_INVALID;
    if(close_result) goto done;
    result=CMD_OK;
done:
    if(in!=FILEHND_INVALID) fs_close(in);
    if(out!=FILEHND_INVALID) fs_close(out);
    free(data);
    return result;
}
#endif
