#ifndef UMKA_RUNTIME_H_INCLUDED
#define UMKA_RUNTIME_H_INCLUDED

#include "umka_vm.h"


void rtlmemcpy (Slot *params, Slot *result);
void rtlfopen  (Slot *params, Slot *result);
void rtlfclose (Slot *params, Slot *result);
void rtlfread  (Slot *params, Slot *result);
void rtlfwrite (Slot *params, Slot *result);
void rtlfseek  (Slot *params, Slot *result);
void rtlftell  (Slot *params, Slot *result);
void rtlremove (Slot *params, Slot *result);
void rtlfeof   (Slot *params, Slot *result);
void rtltime   (Slot *params, Slot *result);
void rtlclock  (Slot *params, Slot *result);
void rtlgetenv (Slot *params, Slot *result);

#endif // UMKA_RUNTIME_H_INCLUDED
