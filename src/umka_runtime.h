#ifndef UMKA_RUNTIME_H_INCLUDED
#define UMKA_RUNTIME_H_INCLUDED

#include "umka_vm.h"


typedef struct
{
    int64_t second, minute, hour;
    int64_t day, month, year;
    int64_t dayOfWeek, dayOfYear;
    bool isDST;
} RTLDateTime;


void rtlmemcpy          (Slot *params, Slot *result);
void rtlfopen           (Slot *params, Slot *result);
void rtlfopenSandbox    (Slot *params, Slot *result);
void rtlfclose          (Slot *params, Slot *result);
void rtlfcloseSandbox   (Slot *params, Slot *result);
void rtlfread           (Slot *params, Slot *result);
void rtlfreadSandbox    (Slot *params, Slot *result);
void rtlfwrite          (Slot *params, Slot *result);
void rtlfwriteSandbox   (Slot *params, Slot *result);
void rtlfseek           (Slot *params, Slot *result);
void rtlfseekSandbox    (Slot *params, Slot *result);
void rtlftell           (Slot *params, Slot *result);
void rtlftellSandbox    (Slot *params, Slot *result);
void rtlremove          (Slot *params, Slot *result);
void rtlremoveSandbox   (Slot *params, Slot *result);
void rtlfeof            (Slot *params, Slot *result);
void rtlfeofSandbox     (Slot *params, Slot *result);
void rtltime            (Slot *params, Slot *result);
void rtlclock           (Slot *params, Slot *result);
void rtllocaltime       (Slot *params, Slot *result);
void rtlgmtime          (Slot *params, Slot *result);
void rtlmktime          (Slot *params, Slot *result);
void rtlgetenv          (Slot *params, Slot *result);
void rtlgetenvSandbox   (Slot *params, Slot *result);
void rtlsystem          (Slot *params, Slot *result);
void rtlsystemSandbox   (Slot *params, Slot *result);

#endif // UMKA_RUNTIME_H_INCLUDED
