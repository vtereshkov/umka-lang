#ifndef UMKA_API_H_INCLUDED
#define UMKA_API_H_INCLUDED

#include <stdint.h>


typedef union
{
    int64_t intVal;
    void *ptrVal;
    double realVal;
} UmkaStackSlot;


typedef void (*UmkaExternFunc)(UmkaStackSlot *params, UmkaStackSlot *result);


enum
{
    UMKA_MSG_LEN = 512
};


typedef struct
{
    char fileName[UMKA_MSG_LEN];
    int line, pos;
    char msg[UMKA_MSG_LEN];
} UmkaError;


int umkaInit(char *fileName, int storageCapacity, int argc, char **argv);
int umkaCompile(void);
int umkaRun(int stackSize);
int umkaFree(void);
int umkaGetError(UmkaError *err);
int umkaAsm(char *buf);
int umkaAddFunc(char *name, UmkaExternFunc entry);


#endif // UMKA_API_H_INCLUDED
