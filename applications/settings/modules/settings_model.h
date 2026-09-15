#ifndef NEXT_SETTINGS_MODEL_H
#define NEXT_SETTINGS_MODEL_H
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static int settings_clamp(int value,int min,int max) { return value<min?min:value>max?max:value; }
static int settings_days(int year,int month) {
    static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return month==1 && (year%4==0 && (year%100!=0 || year%400==0)) ? 29 : days[month];
}
static void settings_adjust_clock(struct tm *clock,int field,int step) {
    switch(field) {
        case 0: clock->tm_year=settings_clamp(clock->tm_year+step,98,186); break;
        case 1: clock->tm_mon=(clock->tm_mon+step+12)%12; break;
        case 2: clock->tm_mday=settings_clamp(clock->tm_mday+step,1,settings_days(clock->tm_year+1900,clock->tm_mon)); break;
        case 3: clock->tm_hour=(clock->tm_hour+step+24)%24; break;
        case 4: clock->tm_min=(clock->tm_min+step+60)%60; break;
    }
    clock->tm_mday=settings_clamp(clock->tm_mday,1,settings_days(clock->tm_year+1900,clock->tm_mon));
    clock->tm_sec=0; clock->tm_isdst=0;
}
static void settings_format_timezone(char *dst,size_t size,int minutes) {
    snprintf(dst,size,"UTC%c%02d:%02d",minutes<0?'-':'+',abs(minutes)/60,abs(minutes)%60);
}
#endif
