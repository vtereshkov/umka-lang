#ifndef UMKA_API_H_INCLUDED
#define UMKA_API_H_INCLUDED

#ifdef _WIN32
  #if defined(UMKA_STATIC)
    #define UMKA_EXPORT
    #define UMKA_IMPORT
  #else
    #define UMKA_EXPORT __declspec(dllexport)
    #define UMKA_IMPORT __declspec(dllimport)
  #endif
#else
  // All modules on Unix are compiled with -fvisibility=hidden
  // All API symbols get visibility default
  // whether or not we're static linking or dynamic linking (with -fPIC)
  #define UMKA_EXPORT __attribute__((visibility("default"))) 
  #define UMKA_IMPORT __attribute__((visibility("default"))) 
#endif

#ifdef UMKA_BUILD
#define UMKA_EXTERN UMKA_EXPORT
#else
#define UMKA_EXTERN UMKA_IMPORT
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


void UMKA_EXTERN *umkaAlloc     (void);
bool UMKA_EXTERN umkaInit       (void *umka, const char *fileName, int storageSize, int stackSize, int argc, char **argv);
bool UMKA_EXTERN umkaCompile    (void *umka);
bool UMKA_EXTERN umkaRun        (void *umka);
bool UMKA_EXTERN umkaCall       (void *umka, int entryOffset, int numParamSlots, UmkaStackSlot *params, UmkaStackSlot *result);
void UMKA_EXTERN umkaFree       (void *umka);
void UMKA_EXTERN umkaGetError   (void *umka, UmkaError *err);
void UMKA_EXTERN umkaAsm        (void *umka, char *buf);
void UMKA_EXTERN umkaAddFunc    (void *umka, const char *name, UmkaExternFunc entry);
int  UMKA_EXTERN umkaGetFunc    (void *umka, const char *moduleName, const char *funcName);


#if defined(__cplusplus)
}
#endif

#endif // UMKA_API_H_INCLUDED
