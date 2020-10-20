#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "umka_runtime.h"


void rtlfopen(Slot *params, Slot *result)
{
    const char *name = (const char *)params[1].ptrVal;
    const char *mode = (const char *)params[0].ptrVal;

    FILE *file = fopen(name, mode);
    result->ptrVal = (int64_t)file;
}


void rtlfclose(Slot *params, Slot *result)
{
    FILE *file = (FILE *)params[0].ptrVal;
    result->intVal = fclose(file);
}


void rtlfread(Slot *params, Slot *result)
{
    void *buf  = (void *)params[3].ptrVal;
    int   size = params[2].intVal;
    int   cnt  = params[1].intVal;
    FILE *file = (FILE *)params[0].ptrVal;

    result->intVal = fread(buf, size, cnt, file);
}


void rtlfwrite(Slot *params, Slot *result)
{
    void *buf  = (void *)params[3].ptrVal;
    int   size = params[2].intVal;
    int   cnt  = params[1].intVal;
    FILE *file = (FILE *)params[0].ptrVal;

    result->intVal = fwrite(buf, size, cnt, file);
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


void rtlremove(Slot *params, Slot *result)
{
    const char *name = (const char *)params[0].ptrVal;
    result->intVal = remove(name);
}


void rtltime(Slot *params, Slot *result)
{
    result->intVal = time(NULL);
}

