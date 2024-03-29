#include "../../src/umka_api.h"


void add(UmkaStackSlot *params, UmkaStackSlot *result)
{
    double a = params[1].realVal;
    double b = params[0].realVal;
    result->realVal = a + b;
}


void sub(UmkaStackSlot *params, UmkaStackSlot *result)
{
    double a = params[1].realVal;
    double b = params[0].realVal;
    result->realVal = a - b;
}


void hello(UmkaStackSlot *params, UmkaStackSlot *result)
{
    void *umka = result->ptrVal;
    UmkaAPI *api = umkaGetAPI(umka);
    result->ptrVal = api->umkaMakeStr(umka, "Hello");
}