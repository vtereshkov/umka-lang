#ifndef UMKA_API_H_INCLUDED
#define UMKA_API_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>


#if defined(__cplusplus)
extern "C" {
#endif


typedef union
{
    int64_t intVal;
    uint64_t uintVal;
    int64_t ptrVal;
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


void *umkaAlloc     (void);
bool umkaInit       (void *umka, const char *fileName, int storageSize, int stackSize, int argc, char **argv);
bool umkaCompile    (void *umka);
bool umkaRun        (void *umka);
bool umkaCall       (void *umka, int entryOffset, int numParamSlots, UmkaStackSlot *params, UmkaStackSlot *result);
void umkaFree       (void *umka);
void umkaGetError   (void *umka, UmkaError *err);
void umkaAsm        (void *umka, char *buf);
void umkaAddFunc    (void *umka, const char *name, UmkaExternFunc entry);
int  umkaGetFunc    (void *umka, const char *moduleName, const char *funcName);


#if defined(__cplusplus)
}
#endif

#endif // UMKA_API_H_INCLUDED
