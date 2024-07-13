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

UmkaFuncContext callbackContext = {0};

void sumImpl(UmkaStackSlot *params, UmkaStackSlot *result) 
{
    int callback = ((UmkaClosure *)umkaGetParam(params, 0))->entryOffset;
    void *callbackType = umkaGetParam(params, 1)->ptrVal;
    int n = umkaGetParam(params, 2)->intVal;

    void *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);

    if (callbackContext.entryOffset != callback)
        api->umkaMakeFuncContext(umka, callbackType, callback, &callbackContext);

    int sum = 0;
    for (int i = 1; i <= n; i++)
    {
        umkaGetParam(callbackContext.params, 0)->intVal = i;
        api->umkaCall(umka, &callbackContext);
        sum += umkaGetResult(callbackContext.params, callbackContext.result)->intVal;
    }

    umkaGetResult(params, result)->intVal = sum;
}