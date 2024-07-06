#ifndef UMKA_RUNTIME_H_INCLUDED
#define UMKA_RUNTIME_H_INCLUDED

#include "umka_api.h"


typedef struct
{
    int64_t second, minute, hour;
    int64_t day, month, year;
    int64_t dayOfWeek, dayOfYear;
    bool isDST;
} RTLDateTime;


typedef struct
{
    char *fileName;
    char *fnName;
    int64_t line;
} RTLErrPos;


void rtlmemcpy          (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlstdin           (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlstdout          (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlstderr          (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlfopen           (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlfopenSandbox    (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlfclose          (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlfcloseSandbox   (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlfread           (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlfreadSandbox    (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlfwrite          (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlfwriteSandbox   (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlfseek           (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlfseekSandbox    (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlftell           (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlftellSandbox    (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlremove          (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlremoveSandbox   (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlfeof            (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlfeofSandbox     (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlfflush          (UmkaStackSlot *params, UmkaStackSlot *result);
void rtltime            (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlclock           (UmkaStackSlot *params, UmkaStackSlot *result);
void rtllocaltime       (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlgmtime          (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlmktime          (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlgetenv          (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlgetenvSandbox   (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlsystem          (UmkaStackSlot *params, UmkaStackSlot *result);
void rtlsystemSandbox   (UmkaStackSlot *params, UmkaStackSlot *result);
void rtltrace           (UmkaStackSlot *params, UmkaStackSlot *result);

#endif // UMKA_RUNTIME_H_INCLUDED
