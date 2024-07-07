#define __USE_MINGW_ANSI_STDIO 1

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "umka_common.h"
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


void rtlmemcpy(UmkaStackSlot *params, UmkaStackSlot *result)
{
    void *dest   = umkaGetParam(params, 0)->ptrVal;
    void *src    = umkaGetParam(params, 1)->ptrVal;
    int   count  = umkaGetParam(params, 2)->intVal;

    memcpy(dest, src, count);
}


void rtlstdin(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->ptrVal = stdin;
}


void rtlstdout(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->ptrVal = stdout;
}


void rtlstderr(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->ptrVal = stderr;
}


void rtlfopen(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const char *name = (const char *)umkaGetParam(params, 0)->ptrVal;
    const char *mode = (const char *)umkaGetParam(params, 1)->ptrVal;

    FILE *file = fopen(name, mode);
    umkaGetResult(params, result)->ptrVal = file;
}


void rtlfopenSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->ptrVal = NULL;
}


void rtlfclose(UmkaStackSlot *params, UmkaStackSlot *result)
{
    FILE *file = (FILE *)umkaGetParam(params, 0)->ptrVal;
    umkaGetResult(params, result)->intVal = fclose(file);
}


void rtlfcloseSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->intVal = EOF;
}


void rtlfread(UmkaStackSlot *params, UmkaStackSlot *result)
{
    void *buf  = umkaGetParam(params, 0)->ptrVal;
    int   size = umkaGetParam(params, 1)->intVal;
    int   cnt  = umkaGetParam(params, 2)->intVal;
    FILE *file = (FILE *)umkaGetParam(params, 3)->ptrVal;

    umkaGetResult(params, result)->intVal = fread(buf, size, cnt, file);
}


void rtlfreadSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->intVal = 0;
}


void rtlfwrite(UmkaStackSlot *params, UmkaStackSlot *result)
{
    void *buf  = umkaGetParam(params, 0)->ptrVal;
    int   size = umkaGetParam(params, 1)->intVal;
    int   cnt  = umkaGetParam(params, 2)->intVal;
    FILE *file = (FILE *)umkaGetParam(params, 3)->ptrVal;

    umkaGetResult(params, result)->intVal = fwrite(buf, size, cnt, file);
}


void rtlfwriteSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->intVal = 0;
}


void rtlfseek(UmkaStackSlot *params, UmkaStackSlot *result)
{
    FILE *file   = (FILE *)umkaGetParam(params, 0)->ptrVal;
    int   offset = umkaGetParam(params, 1)->intVal;
    int   origin = umkaGetParam(params, 2)->intVal;

    int originC = 0;
    if      (origin == 0) originC = SEEK_SET;
    else if (origin == 1) originC = SEEK_CUR;
    else if (origin == 2) originC = SEEK_END;

    umkaGetResult(params, result)->intVal = fseek(file, offset, originC);
}


void rtlfseekSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->intVal = -1;
}


void rtlftell(UmkaStackSlot *params, UmkaStackSlot *result)
{
    FILE *file = (FILE *)umkaGetParam(params, 0)->ptrVal;
    umkaGetResult(params, result)->intVal = ftell(file);
}


void rtlftellSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->intVal = -1;
}


void rtlremove(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const char *name = (const char *)umkaGetParam(params, 0)->ptrVal;
    umkaGetResult(params, result)->intVal = remove(name);
}


void rtlremoveSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->intVal = -1;
}


void rtlfeof(UmkaStackSlot *params, UmkaStackSlot *result)
{
    FILE *file = (FILE *)umkaGetParam(params, 0)->ptrVal;
    umkaGetResult(params, result)->intVal = feof(file);
}


void rtlfeofSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->intVal = -1;
}


void rtlfflush(UmkaStackSlot *params, UmkaStackSlot *result)
{
    FILE *file = (FILE *)umkaGetParam(params, 0)->ptrVal;
    umkaGetResult(params, result)->intVal = fflush(file);
}


void rtltime(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->intVal = time(NULL);
}


void rtlclock(UmkaStackSlot *params, UmkaStackSlot *result)
{
#ifdef _WIN32
    umkaGetResult(params, result)->realVal = (double)clock() / CLOCKS_PER_SEC;
#else
    // On Linux, clock() measures per-process time and may produce wrong actual time estimates
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    umkaGetResult(params, result)->realVal = (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
#endif
}


void rtllocaltime(UmkaStackSlot *params, UmkaStackSlot *result)
{
    time_t curTime = umkaGetParam(params, 0)->intVal;
    RTLDateTime *rtlDateTime = (RTLDateTime *)umkaGetResult(params, result)->ptrVal;

    struct tm *dateTime = localtime(&curTime);
    convToRTLDateTime(rtlDateTime, dateTime);
}


void rtlgmtime(UmkaStackSlot *params, UmkaStackSlot *result)
{
    time_t curTime = umkaGetParam(params, 0)->intVal;
    RTLDateTime *rtlDateTime = (RTLDateTime *)umkaGetResult(params, result)->ptrVal;

    struct tm *dateTime = gmtime(&curTime);
    convToRTLDateTime(rtlDateTime, dateTime);
}


void rtlmktime(UmkaStackSlot *params, UmkaStackSlot *result)
{
    RTLDateTime *rtlDateTime = (RTLDateTime *)umkaGetParam(params, 0)->ptrVal;

    struct tm dateTime;
    convFromRTLDateTime(&dateTime, rtlDateTime);

    umkaGetResult(params, result)->intVal = mktime(&dateTime);
}


void rtlgetenv(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const char *name = (const char *)umkaGetParam(params, 0)->ptrVal;
    void *umka = umkaGetInstance(result);

    const char *env = getenv(name);
    umkaGetResult(params, result)->ptrVal = umkaMakeStr(umka, env);
}


void rtlgetenvSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->ptrVal = NULL;
}


void rtlsystem(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const char *command = (const char *)umkaGetParam(params, 0)->ptrVal;
    umkaGetResult(params, result)->intVal = system(command);
}


void rtlsystemSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->intVal = -1;
}


void rtltrace(UmkaStackSlot *params, UmkaStackSlot *result)
{
    int depth = umkaGetParam(params, 0)->intVal;
    RTLErrPos *pos = (RTLErrPos *)umkaGetParam(params, 1)->ptrVal;

    void *umka = umkaGetInstance(result);

    char fileName[DEFAULT_STR_LEN + 1], fnName[DEFAULT_STR_LEN + 1];
    int line;

    if (umkaGetCallStack(umka, depth, DEFAULT_STR_LEN + 1, NULL, fileName, fnName, &line))
    {
        umkaDecRef(umka, pos->fileName);
        umkaDecRef(umka, pos->fnName);

        pos->fileName = umkaMakeStr(umka, fileName);
        pos->fnName   = umkaMakeStr(umka, fnName);
        pos->line     = line;

        umkaGetResult(params, result)->intVal = 0;
    }
    else
        umkaGetResult(params, result)->intVal = -1;
}

