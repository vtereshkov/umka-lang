#include "../../src/umka_api.h"


UMKA_EXPORT void add(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);

    double a = api->umkaGetParam(params, 0)->realVal;
    double b = api->umkaGetParam(params, 1)->realVal;
    api->umkaGetResult(params, result)->realVal = a + b;
}


UMKA_EXPORT void mulVec(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka); 

    double a = api->umkaGetParam(params, 0)->realVal;
    double* v = (double *)api->umkaGetParam(params, 1);
    double* out = api->umkaGetResult(params, result)->ptrVal;

    out[0] = a * v[0];
    out[1] = a * v[1];
}


UMKA_EXPORT void hello(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    
    api->umkaGetResult(params, result)->ptrVal = api->umkaMakeStr(umka, "Hello");
}

UmkaFuncContext callbackContext = {0};

UMKA_EXPORT void sumImpl(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);    

    UmkaClosure *callback = (UmkaClosure *)api->umkaGetParam(params, 0);
    void *callbackType = api->umkaGetParam(params, 1)->ptrVal;
    int n = api->umkaGetParam(params, 2)->intVal;

    if (callbackContext.entryOffset != callback->entryOffset)
    {
        api->umkaMakeFuncContext(umka, callbackType, callback->entryOffset, &callbackContext);
        *api->umkaGetUpvalue(callbackContext.params) = callback->upvalue;
    }

    int sum = 0;
    for (int i = 1; i <= n; i++)
    {
        api->umkaGetParam(callbackContext.params, 0)->intVal = i;
        api->umkaIncRef(umka, callback->upvalue.data);

        api->umkaCall(umka, &callbackContext);
        sum += api->umkaGetResult(callbackContext.params, callbackContext.result)->intVal;
    }

    api->umkaGetResult(params, result)->intVal = sum;
}