#define __USE_MINGW_ANSI_STDIO 1

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "umka_runtime.h"


static void convToRTLDateTime(RTLDateTime *dest, const struct tm *src)
{
    dest->second    = src->tm_sec;
    dest->minute    = src->tm_min;
    dest->hour      = src->tm_hour;
    dest->day       = src->tm_mday;
    dest->month     = src->tm_mon + 1;
    dest->year      = src->tm_year + 1900;
    dest->dayOfWeek = src->tm_wday + 1;
    dest->dayOfYear = src->tm_yday + 1;
    dest->isDST     = src->tm_isdst != 0;
}


static void convFromRTLDateTime(struct tm *dest, const RTLDateTime *src)
{
    dest->tm_sec    = src->second;
    dest->tm_min    = src->minute;
    dest->tm_hour   = src->hour;
    dest->tm_mday   = src->day;
    dest->tm_mon    = src->month - 1;
    dest->tm_year   = src->year - 1900;
    dest->tm_wday   = src->dayOfWeek - 1;
    dest->tm_yday   = src->dayOfYear - 1;
    dest->tm_isdst  = src->isDST;
}


void rtlmemcpy(Slot *params, Slot *result)
{
    void *dest   = params[2].ptrVal;
    void *src    = params[1].ptrVal;
    int   count  = params[0].intVal;

    memcpy(dest, src, count);
}


void rtlfopen(Slot *params, Slot *result)
{
    const char *name = (const char *)params[1].ptrVal;
    const char *mode = (const char *)params[0].ptrVal;

    FILE *file = fopen(name, mode);
    result->ptrVal = file;
}


void rtlfopenSandbox(Slot *params, Slot *result)
{
    result->ptrVal = NULL;
}


void rtlfclose(Slot *params, Slot *result)
{
    FILE *file = (FILE *)params[0].ptrVal;
    result->intVal = fclose(file);
}


void rtlfcloseSandbox(Slot *params, Slot *result)
{
    result->intVal = EOF;
}


void rtlfread(Slot *params, Slot *result)
{
    void *buf  = params[3].ptrVal;
    int   size = params[2].intVal;
    int   cnt  = params[1].intVal;
    FILE *file = (FILE *)params[0].ptrVal;

    result->intVal = fread(buf, size, cnt, file);
}


void rtlfreadSandbox(Slot *params, Slot *result)
{
    result->intVal = 0;
}


void rtlfwrite(Slot *params, Slot *result)
{
    void *buf  = params[3].ptrVal;
    int   size = params[2].intVal;
    int   cnt  = params[1].intVal;
    FILE *file = (FILE *)params[0].ptrVal;

    result->intVal = fwrite(buf, size, cnt, file);
}


void rtlfwriteSandbox(Slot *params, Slot *result)
{
    result->intVal = 0;
}


void rtlfseek(Slot *params, Slot *result)
{
    FILE *file   = (FILE *)params[2].ptrVal;
    int   offset = params[1].intVal;
    int   origin = params[0].intVal;

    int originC = 0;
    if      (origin == 0) originC = SEEK_SET;
    else if (origin == 1) originC = SEEK_CUR;
    else if (origin == 2) originC = SEEK_END;

    result->intVal = fseek(file, offset, originC);
}


void rtlfseekSandbox(Slot *params, Slot *result)
{
    result->intVal = -1;
}


void rtlftell(Slot *params, Slot *result)
{
    FILE *file = (FILE *)params[0].ptrVal;
    result->intVal = ftell(file);
}


void rtlftellSandbox(Slot *params, Slot *result)
{
    result->intVal = -1;
}


void rtlremove(Slot *params, Slot *result)
{
    const char *name = (const char *)params[0].ptrVal;
    result->intVal = remove(name);
}


void rtlremoveSandbox(Slot *params, Slot *result)
{
    result->intVal = -1;
}


void rtlfeof(Slot *params, Slot *result)
{
    FILE *file = (FILE *)params[0].ptrVal;
    result->intVal = feof(file);
}


void rtlfeofSandbox(Slot *params, Slot *result)
{
    result->intVal = -1;
}


void rtltime(Slot *params, Slot *result)
{
    result->intVal = time(NULL);
}


void rtlclock(Slot *params, Slot *result)
{
#ifdef _WIN32
    result->realVal = (double)clock() / CLOCKS_PER_SEC;
#else
    // On Linux, clock() measures per-process time and may produce wrong actual time estimates
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    result->realVal = (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
#endif
}


void rtllocaltime(Slot *params, Slot *result)
{
    RTLDateTime *rtlDateTime = (RTLDateTime *)params[0].ptrVal;
    time_t curTime = params[1].intVal;

    struct tm *dateTime = localtime(&curTime);
    convToRTLDateTime(rtlDateTime, dateTime);

    result->ptrVal = rtlDateTime;
}


void rtlgmtime(Slot *params, Slot *result)
{
    RTLDateTime *rtlDateTime = (RTLDateTime *)params[0].ptrVal;
    time_t curTime = params[1].intVal;

    struct tm *dateTime = gmtime(&curTime);
    convToRTLDateTime(rtlDateTime, dateTime);

    result->ptrVal = rtlDateTime;
}


void rtlmktime(Slot *params, Slot *result)
{
    RTLDateTime *rtlDateTime = (RTLDateTime *)params[0].ptrVal;

    struct tm dateTime;
    convFromRTLDateTime(&dateTime, rtlDateTime);

    result->intVal = mktime(&dateTime);
}


void rtlgetenv(Slot *params, Slot *result)
{
    const char *name = (const char *)params[0].ptrVal;
    static char empty[] = "";

    char *val = getenv(name);
    if (!val)
        val = empty;
    result->ptrVal = val;
}


void rtlgetenvSandbox(Slot *params, Slot *result)
{
    result->ptrVal = "";
}


void rtlsystem(Slot *params, Slot *result)
{
    const char *command = (const char *)params[0].ptrVal;
    result->intVal = system(command);
}


void rtlsystemSandbox(Slot *params, Slot *result)
{
    result->intVal = -1;
}

