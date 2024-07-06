#include "../../src/umka_api.h"


void add(UmkaStackSlot *params, UmkaStackSlot *result)
{
    double a = umkaGetParam(params, 0)->realVal;
    double b = umkaGetParam(params, 1)->realVal;
    umkaGetResult(result)->realVal = a + b;
}


void mulVec(UmkaStackSlot *params, UmkaStackSlot *result)
{
    double a = umkaGetParam(params, 0)->realVal;
    double* v = (double *)umkaGetParam(params, 1);
    double* out = umkaAllocResult(params);
    out[0] = a * v[0];
    out[1] = a * v[1];
    umkaGetResult(result)->ptrVal = out;
}


void hello(UmkaStackSlot *params, UmkaStackSlot *result)
{
    void *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    umkaGetResult(result)->ptrVal = api->umkaMakeStr(umka, "Hello");
}


void sum(UmkaStackSlot *params, UmkaStackSlot *result) 
{
    int callback = ((UmkaClosure *)umkaGetParam(params, 0))->entryOffset;
    int n = umkaGetParam(params, 1)->intVal;

    void *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);

    int sum = 0;
    for (int i = 1; i <= n; i++)
    {
        UmkaStackSlot callbackParam[] = {{.intVal = i}};
        UmkaStackSlot callbackResult;
        api->umkaCall(umka, callback, 1, callbackParam, &callbackResult);
        sum += callbackResult.intVal;
    }

    umkaGetResult(result)->intVal = sum;
}