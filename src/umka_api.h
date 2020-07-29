#ifndef UMKA_API_H_INCLUDED
#define UMKA_API_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>


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


bool umkaInit(char *fileName, int storageSize, int stackSize, int argc, char **argv);
bool umkaCompile(void);
bool umkaRun(void);
bool umkaCall(int entryOffset, int numParamSlots, UmkaStackSlot *params, UmkaStackSlot *result);
void umkaFree(void);
void umkaGetError(UmkaError *err);
void umkaAsm(char *buf);
void umkaAddFunc(char *name, UmkaExternFunc entry);
int  umkaGetFunc(char *moduleName, char *funcName);

#endif // UMKA_API_H_INCLUDED
