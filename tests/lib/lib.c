#include "../../src/umka_api.h"


void add(UmkaStackSlot *params, UmkaStackSlot *result)
{
    double a = umkaGetParam(params, 0)->realVal;
    double b = umkaGetParam(params, 1)->realVal;
    umkaGetResult(params, result)->realVal = a + b;
}


void mulVec(UmkaStackSlot *params, UmkaStackSlot *result)
{
    double a = umkaGetParam(params, 0)->realVal;
    double* v = (double *)umkaGetParam(params, 1);
    double* out = umkaGetResult(params, result)->ptrVal;
    out[0] = a * v[0];
    out[1] = a * v[1];
}


void hello(UmkaStackSlot *params, UmkaStackSlot *result)
{
    void *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    umkaGetResult(params, result)->ptrVal = api->umkaMakeStr(umka, "Hello");
}

UmkaStackSlot *callbackParams = NULL;
UmkaStackSlot *callbackResult = NULL;

void sumImpl(UmkaStackSlot *params, UmkaStackSlot *result) 
{
    int callback = ((UmkaClosure *)umkaGetParam(params, 0))->entryOffset;
    void *callbackType = umkaGetParam(params, 1)->ptrVal;
    int n = umkaGetParam(params, 2)->intVal;

    void *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);

    if (!callbackParams || !callbackResult)
        api->umkaAllocParams(umka, callbackType, &callbackParams, &callbackResult);

    int sum = 0;
    for (int i = 1; i <= n; i++)
    {
        umkaGetParam(callbackParams, 0)->intVal = i;
        api->umkaCall(umka, callback, callbackParams, callbackResult);
        sum += umkaGetResult(callbackParams, callbackResult)->intVal;
    }

    umkaGetResult(params, result)->intVal = sum;
}