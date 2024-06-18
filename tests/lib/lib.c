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


void sum(UmkaStackSlot *param, UmkaStackSlot *result) 
{
    int n = param[0].intVal;
    int callback = param[1].intVal;

    void *umka = result->ptrVal;
    UmkaAPI *api = umkaGetAPI(umka);

    int sum = 0;
    for (int i = 1; i <= n; i++)
    {
        UmkaStackSlot callbackParam[] = {{.intVal = i}};
        UmkaStackSlot callbackResult;
        api->umkaCall(umka, callback, 1, callbackParam, &callbackResult);
        sum += callbackResult.intVal;
    }

    result->intVal = sum;
}