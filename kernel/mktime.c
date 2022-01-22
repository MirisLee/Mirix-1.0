/* 
 * Mirix 1.0/kernel/mktime.c
 * (C) 2022 Miris Lee
 */

#include <time.h>

#define MINUTE  60
#define HOUR    (60 * MINUTE)
#define DAY     (24 * HOUR)
#define YEAR    (365 * DAY)

static int month[12] = {
    DAY * 0, DAY * 31, DAY * 60,
    DAY * 91, DAY * 121, DAY * 152,
    DAY * 182, DAY * 213, DAY * 244,
    DAY * 274, DAY * 305, DAY * 335
};

long kernel_mktime(struct tm *tm) {
    long sec;
    int year;

    year = tm->tm_year + 30;
    sec = YEAR * year + DAY * ((year + 1) / 4);
    sec += month[tm->tm_mon];
    if (tm->tm_mon >= 2 && (year + 2) % 4) sec -= DAY;
    sec += DAY * (tm->tm_mday - 1);
    sec += HOUR * tm->tm_hour;
    sec += MINUTE * tm->tm_min;
    sec += tm->tm_sec;
    return sec;
}