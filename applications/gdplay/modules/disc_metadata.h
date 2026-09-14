#ifndef NEXT_DISC_METADATA_H
#define NEXT_DISC_METADATA_H
#include <stdio.h>
#include <string.h>
/* IP.BIN fields are fixed-width bytes, never C strings. */
typedef struct {
    char title[129], region[40], vga[8], date[16], number[16], version[8], product[12];
} disc_metadata_t;
static void disc_field(char *dst,size_t cap,const unsigned char *src,size_t len) {
    size_t start=0,end=len,n=0;
    while(start<len && src[start]==' ') start++;
    while(end>start && (src[end-1]==' ' || src[end-1]==0)) end--;
    while(start<end && n+1<cap) {
        unsigned char c=src[start++];
        dst[n++]=(c>=32 && c<127)?(char)c:' ';
    }
    dst[n]=0;
}
static int disc_metadata_read(disc_metadata_t *out,const unsigned char *data,size_t size) {
    memset(out,0,sizeof(*out));
    if(size<256 || memcmp(data,"SEGA SEGAKATANA ",15)) return 0;
    disc_field(out->title,sizeof(out->title),data+128,128);
    if(!*out->title) snprintf(out->title,sizeof(out->title),"Dreamcast disc");
    disc_field(out->product,sizeof(out->product),data+64,10);
    disc_field(out->version,sizeof(out->version),data+74,6);
    disc_field(out->number,sizeof(out->number),data+43,5);
    snprintf(out->vga,sizeof(out->vga),"%s",data[61]=='1'?"Yes":"No");
    snprintf(out->date,sizeof(out->date),"%.4s-%.2s-%.2s",data+80,data+84,data+86);
    const char *names[]={"Japan","USA","Europe"};
    const char codes[]={'J','U','E'};
    for(int i=0;i<3;i++) if(memchr(data+48,codes[i],8)) {
        if(*out->region) strcat(out->region," / ");
        strcat(out->region,names[i]);
    }
    if(!*out->region) strcpy(out->region,"Unknown");
    return 1;
}
#endif
