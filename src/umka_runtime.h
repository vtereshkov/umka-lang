#ifndef UMKA_RUNTIME_H_INCLUDED
#define UMKA_RUNTIME_H_INCLUDED

#include "umka_vm.h"


void rtlfopen  (Slot *params, Slot *result);
void rtlfclose (Slot *params, Slot *result);
void rtlfread  (Slot *params, Slot *result);
void rtlfwrite (Slot *params, Slot *result);
void rtlfseek  (Slot *params, Slot *result);
void rtlremove (Slot *params, Slot *result);
void rtltime   (Slot *params, Slot *result);
void rtlmalloc (Slot *params, Slot *result);
void rtlfree   (Slot *params, Slot *result);


#endif // UMKA_RUNTIME_H_INCLUDED
