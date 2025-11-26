#define __USE_MINGW_ANSI_STDIO 1

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "umka_common.h"
#include "umka_runtime.h"


static void rtlOnFreeFile(UmkaStackSlot *params, UmkaStackSlot *result)
{
    File *file = umkaGetParam(params, 0)->ptrVal;
    if (file && file->stream)
    {
        fclose(file->stream);
        file->stream = NULL;
    }
}


static void rtlConvToDateTime(RTLDateTime *dest, const struct tm *src)
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


static void rtlConvFromDateTime(struct tm *dest, const RTLDateTime *src)
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
    void *dest = umkaGetParam(params, 0)->ptrVal;
    const void *src = umkaGetParam(params, 1)->ptrVal;
    const int count = umkaGetParam(params, 2)->intVal;

    memcpy(dest, src, count);
}


void rtlstdin(UmkaStackSlot *params, UmkaStackSlot *result)
{
    static File file;
    file.stream = stdin;
    umkaGetResult(params, result)->ptrVal = &file;
}


void rtlstdout(UmkaStackSlot *params, UmkaStackSlot *result)
{
    static File file;
    file.stream = stdout;
    umkaGetResult(params, result)->ptrVal = &file;
}


void rtlstderr(UmkaStackSlot *params, UmkaStackSlot *result)
{
    static File file;
    file.stream = stderr;
    umkaGetResult(params, result)->ptrVal = &file;
}


void rtlfopen(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const char *name = umkaGetParam(params, 0)->ptrVal;
    const char *mode = umkaGetParam(params, 1)->ptrVal;

    Umka *umka = umkaGetInstance(result);

    FILE *stream = fopen(name, mode);   
    File *file = stream ? umkaAllocData(umka, sizeof(File), rtlOnFreeFile) : NULL;
    if (file)
        file->stream = stream;

    umkaGetResult(params, result)->ptrVal = file;
}


void rtlfopenSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->ptrVal = NULL;
}


void rtlfclose(UmkaStackSlot *params, UmkaStackSlot *result)
{
    File *file = umkaGetParam(params, 0)->ptrVal;
    
    int64_t res = EOF;
    if (file && file->stream)
    {
        res = fclose(file->stream);
        file->stream = NULL;
    }    
    umkaGetResult(params, result)->intVal = res;
}


void rtlfcloseSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->intVal = EOF;
}


void rtlfread(UmkaStackSlot *params, UmkaStackSlot *result)
{
    void *buf = umkaGetParam(params, 0)->ptrVal;
    const int size = umkaGetParam(params, 1)->intVal;
    const int cnt = umkaGetParam(params, 2)->intVal;
    File *file = umkaGetParam(params, 3)->ptrVal;

    int64_t res = 0;
    if (file && file->stream)
        res = fread(buf, size, cnt, file->stream);

    umkaGetResult(params, result)->intVal = res;
}


void rtlfreadSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->intVal = 0;
}


void rtlfwrite(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const void *buf = umkaGetParam(params, 0)->ptrVal;
    const int size = umkaGetParam(params, 1)->intVal;
    const int cnt  = umkaGetParam(params, 2)->intVal;
    File *file = umkaGetParam(params, 3)->ptrVal;

    int64_t res = 0;
    if (file && file->stream)
        res = fwrite(buf, size, cnt, file->stream);    

    umkaGetResult(params, result)->intVal = res;
}


void rtlfwriteSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->intVal = 0;
}


void rtlfseek(UmkaStackSlot *params, UmkaStackSlot *result)
{
    File *file = umkaGetParam(params, 0)->ptrVal;
    const int offset = umkaGetParam(params, 1)->intVal;
    const int origin = umkaGetParam(params, 2)->intVal;

    int originC = 0;
    if      (origin == 0) originC = SEEK_SET;
    else if (origin == 1) originC = SEEK_CUR;
    else if (origin == 2) originC = SEEK_END;

    int64_t res = -1;
    if (file && file->stream)
        res = fseek(file->stream, offset, originC); 

    umkaGetResult(params, result)->intVal = res;
}


void rtlfseekSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->intVal = -1;
}


void rtlftell(UmkaStackSlot *params, UmkaStackSlot *result)
{
    File *file = umkaGetParam(params, 0)->ptrVal;

    int64_t res = -1;
    if (file && file->stream)
        res = ftell(file->stream); 

    umkaGetResult(params, result)->intVal = res;
}


void rtlftellSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->intVal = -1;
}


void rtlremove(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const char *name = umkaGetParam(params, 0)->ptrVal;
    umkaGetResult(params, result)->intVal = remove(name);
}


void rtlremoveSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->intVal = -1;
}


void rtlfeof(UmkaStackSlot *params, UmkaStackSlot *result)
{
    File *file = umkaGetParam(params, 0)->ptrVal;

    int64_t res = -1;
    if (file && file->stream)
        res = feof(file->stream); 

    umkaGetResult(params, result)->intVal = res;
}


void rtlfeofSandbox(UmkaStackSlot *params, UmkaStackSlot *result)
{
    umkaGetResult(params, result)->intVal = -1;
}


void rtlfflush(UmkaStackSlot *params, UmkaStackSlot *result)
{
    File *file = umkaGetParam(params, 0)->ptrVal;

    int64_t res = -1;
    if (file && file->stream)
        res = fflush(file->stream); 

    umkaGetResult(params, result)->intVal = res;
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
    const time_t curTime = umkaGetParam(params, 0)->intVal;
    RTLDateTime *rtlDateTime = umkaGetResult(params, result)->ptrVal;

    const struct tm *dateTime = localtime(&curTime);
    rtlConvToDateTime(rtlDateTime, dateTime);
}


void rtlgmtime(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const time_t curTime = umkaGetParam(params, 0)->intVal;
    RTLDateTime *rtlDateTime = umkaGetResult(params, result)->ptrVal;

    const struct tm *dateTime = gmtime(&curTime);
    rtlConvToDateTime(rtlDateTime, dateTime);
}


void rtlmktime(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const RTLDateTime *rtlDateTime = umkaGetParam(params, 0)->ptrVal;

    struct tm dateTime;
    rtlConvFromDateTime(&dateTime, rtlDateTime);

    umkaGetResult(params, result)->intVal = mktime(&dateTime);
}


void rtlgetenv(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const char *name = (const char *)umkaGetParam(params, 0)->ptrVal;
    Umka *umka = umkaGetInstance(result);

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
    const int depth = umkaGetParam(params, 0)->intVal;
    RTLErrPos *pos = umkaGetParam(params, 1)->ptrVal;

    Umka *umka = umkaGetInstance(result);

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

