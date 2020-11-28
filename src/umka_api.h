#ifndef UMKA_API_H_INCLUDED
#define UMKA_API_H_INCLUDED


#ifdef _WIN32  // MSVC++ only
    #if defined(UMKA_STATIC)
        #define UMKA_EXPORT
        #define UMKA_IMPORT
    #else
        #define UMKA_EXPORT __declspec(dllexport)
        #define UMKA_IMPORT __declspec(dllimport)
    #endif
#else
    #define UMKA_EXPORT __attribute__((visibility("default")))
    #define UMKA_IMPORT __attribute__((visibility("default")))
#endif

#ifdef UMKA_BUILD
    #define UMKA_API UMKA_EXPORT
#else
    #define UMKA_API UMKA_IMPORT
#endif


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


void UMKA_API *umkaAlloc     (void);
bool UMKA_API umkaInit       (void *umka, const char *fileName, const char *sourceString, int storageSize, int stackSize, int argc, char **argv);
bool UMKA_API umkaCompile    (void *umka);
bool UMKA_API umkaRun        (void *umka);
bool UMKA_API umkaCall       (void *umka, int entryOffset, int numParamSlots, UmkaStackSlot *params, UmkaStackSlot *result);
void UMKA_API umkaFree       (void *umka);
void UMKA_API umkaGetError   (void *umka, UmkaError *err);
void UMKA_API umkaAsm        (void *umka, char *buf);
void UMKA_API umkaAddFunc    (void *umka, const char *name, UmkaExternFunc entry);
int  UMKA_API umkaGetFunc    (void *umka, const char *moduleName, const char *funcName);


#if defined(__cplusplus)
}
#endif

#endif // UMKA_API_H_INCLUDED
